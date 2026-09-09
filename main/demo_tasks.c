// main/demo_tasks.c —— Cindy 任务面板：列表 / 详情 / 录音三视图 + 轮询与录音工作任务。
//
// 按键(页面内)：UP/DOWN 移动选中或切换任务；OK 短按 进详情 / 开始录音 / 停止并发送；
// OK 长按由 main.c 统一返回菜单，录音中返回即放弃本次录音。
// 录音为 16kHz/16bit/mono PCM，按剩余最大连续堆自动限长(最长 6s)，直接 POST 给 bridge。
// 屏幕字体不含中文字形，界面文案一律英文。
#include "demo.h"
#include "app_wifi.h"
#include "tasks_client.h"
#include "tasks_model.h"
#include "ui_pixel.h"

#include "bsp_audio.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#define REC_HZ          16000
#define REC_BPS         (REC_HZ * 2)                 // 16bit mono = 32KB/s
#define REC_MAX_BYTES   (6 * REC_BPS)
#define POLL_PERIOD_MS  3000
#define WORKER_TICK_MS  150

typedef enum { VIEW_LIST = 0, VIEW_DETAIL, VIEW_RECORD } view_t;
typedef enum { CMD_NONE = 0, CMD_RECORD, CMD_STOP_SEND } cmd_t;

static const char *TAG = "demo_tasks";

static TaskHandle_t s_worker;
static volatile bool s_exit;
static volatile cmd_t s_cmd;
static volatile bool s_recording;

static tasks_model_t s_model;
static view_t s_view;
static char s_line[96];                 // 屏幕右上角状态行（IP / 错误）

static lv_obj_t *s_scr;
static lv_obj_t *s_line_label;
static lv_obj_t *s_box_list, *s_box_detail, *s_box_record;
static lv_obj_t *s_cards[TASKS_MODEL_MAX];
static lv_obj_t *s_rec_sec, *s_rec_bar, *s_rec_hint;

static const uint32_t CHIP_COLORS[] = {
    [TASK_CHIP_QUEUED]  = 0x78909C,
    [TASK_CHIP_RUNNING] = UI_YELLOW,
    [TASK_CHIP_DONE]    = UI_GRASS,
    [TASK_CHIP_FAILED]  = UI_RED,
    [TASK_CHIP_UNKNOWN] = 0x78909C,
};

static lv_obj_t *make_box(lv_obj_t *parent, int y) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(box, 0, y);
    lv_obj_set_size(box, 240, 320 - y);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    return box;
}

static void view_show(view_t v) {
    lv_obj_add_flag(s_box_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_box_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_box_record, LV_OBJ_FLAG_HIDDEN);
    s_view = v;
    lv_obj_t *target = v == VIEW_LIST ? s_box_list
                     : v == VIEW_DETAIL ? s_box_detail : s_box_record;
    lv_obj_remove_flag(target, LV_OBJ_FLAG_HIDDEN);
}

static void list_highlight(void) {
    for (int i = 0; i < s_model.count; i++) {
        if (s_cards[i]) ui_pixel_set_selected(s_cards[i], i == s_model.selected, true);
    }
    if (s_model.count > 0 && s_cards[s_model.selected]) {
        lv_obj_scroll_to_view(s_cards[s_model.selected], LV_ANIM_OFF);
    }
}

static void list_rebuild(void) {
    lv_obj_clean(s_box_list);
    for (int i = 0; i < TASKS_MODEL_MAX; i++) s_cards[i] = NULL;

    if (s_model.count == 0) {
        lv_obj_t *empty = ui_pixel_label(s_box_list, "No tasks yet",
                                         &lv_font_montserrat_14, 0x5A6B7A);
        lv_obj_center(empty);
        return;
    }
    for (int i = 0; i < s_model.count; i++) {
        const task_item_t *it = &s_model.items[i];
        lv_obj_t *card = ui_pixel_panel_create(s_box_list, 12, 6 + i * 60, 216, 54, UI_PAPER);

        lv_obj_t *title = ui_pixel_label(card, it->title, &lv_font_montserrat_14, UI_INK);
        lv_obj_set_width(title, 128);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *badge = ui_pixel_label(card, it->status,
                                         &lv_font_montserrat_14, CHIP_COLORS[tasks_model_chip(it->status)]);
        lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, 0, 0);

        char prev[64];
        tasks_model_preview(it->message, prev, sizeof(prev));
        lv_obj_t *msg = ui_pixel_label(card, prev, &lv_font_montserrat_14, 0x5A6B7A);
        lv_obj_set_width(msg, 200);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_DOT);
        lv_obj_align(msg, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        s_cards[i] = card;
    }
    list_highlight();
}

static void detail_show(void) {
    const task_item_t *it = tasks_model_current(&s_model);
    if (!it) { view_show(VIEW_LIST); return; }

    lv_obj_clean(s_box_detail);
    lv_obj_t *panel = ui_pixel_panel_create(s_box_detail, 12, 6, 216, 214, UI_PAPER);

    lv_obj_t *title = ui_pixel_label(panel, it->title, &lv_font_montserrat_14, UI_INK);
    lv_obj_set_width(title, 190);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *status = ui_pixel_label(panel, it->status,
                                      &lv_font_montserrat_14, CHIP_COLORS[tasks_model_chip(it->status)]);
    lv_obj_align(status, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *msg = ui_pixel_label(panel, it->message, &lv_font_montserrat_14, 0x3A4A5A);
    lv_obj_set_width(msg, 190);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(msg, LV_ALIGN_TOP_LEFT, 0, 26);

    lv_obj_t *hint = ui_pixel_label(panel, "OK: record  U/D: switch",
                                    &lv_font_montserrat_14, UI_SKY_DARK);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    view_show(VIEW_DETAIL);
}

static void record_show(void) {
    lv_obj_clean(s_box_record);
    s_rec_sec = ui_pixel_label(s_box_record, "0s", &lv_font_montserrat_20, UI_INK);
    lv_obj_align(s_rec_sec, LV_ALIGN_TOP_MID, 0, 40);

    s_rec_bar = lv_bar_create(s_box_record);
    lv_obj_set_size(s_rec_bar, 200, 14);
    lv_obj_align(s_rec_bar, LV_ALIGN_TOP_MID, 0, 90);
    lv_bar_set_range(s_rec_bar, 0, 100);

    s_rec_hint = ui_pixel_label(s_box_record, "OK: stop & send", &lv_font_montserrat_14, UI_SKY_DARK);
    lv_obj_align(s_rec_hint, LV_ALIGN_TOP_MID, 0, 140);
    view_show(VIEW_RECORD);
}

static void status_refresh(void) {
    if (s_line_label) lv_label_set_text(s_line_label, s_line);
}

static void ip_text(char *out, size_t cap) {
    esp_netif_t *netif = esp_netif_get_default_netif();
    esp_netif_ip_info_t info;
    if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        snprintf(out, cap, "Wi-Fi...");
        return;
    }
    snprintf(out, cap, "WiFi " IPSTR, IP2STR(&info.ip));
}

static void do_poll(void) {
    task_item_t items[TASKS_MODEL_MAX];
    int count = 0;
    esp_err_t err = tasks_client_fetch(items, TASKS_MODEL_MAX, &count);
    if (!bsp_lvgl_lock(800)) return;
    if (s_exit || !s_box_list) {
        bsp_lvgl_unlock();
        return;
    }

    if (err == ESP_OK) {
        char ip[32];
        ip_text(ip, sizeof(ip));
        snprintf(s_line, sizeof(s_line), "%s  tasks:%d", ip, count);
        if (tasks_model_set_items(&s_model, items, count)) list_rebuild();
    } else {
        snprintf(s_line, sizeof(s_line), "bridge unreachable");
    }
    status_refresh();
    bsp_lvgl_unlock();
}

static size_t rec_cap(void) {
    size_t room = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (room >= REC_MAX_BYTES + 64 * 1024) return REC_MAX_BYTES;
    if (room >= 4 * REC_BPS + 64 * 1024) return 4 * REC_BPS;
    if (room >= 2 * REC_BPS + 48 * 1024) return 2 * REC_BPS;
    return 0;
}

static void do_record(void) {
    size_t cap = rec_cap();
    char task_id[TASK_ID_LEN] = "";
    if (!bsp_lvgl_lock(800)) { s_cmd = CMD_NONE; return; }
    const task_item_t *it = tasks_model_current(&s_model);
    if (it) strncpy(task_id, it->id, sizeof(task_id) - 1);
    if (cap == 0 || task_id[0] == 0) {
        snprintf(s_line, sizeof(s_line), cap == 0 ? "low memory for record" : "no task selected");
        status_refresh();
        bsp_lvgl_unlock();
        s_cmd = CMD_NONE;
        return;
    }
    record_show();
    bsp_lvgl_unlock();

    uint8_t *buf = malloc(cap);
    if (!buf) { s_cmd = CMD_NONE; return; }
    if (bsp_audio_set_format(REC_HZ, 16, 1) != ESP_OK) {
        free(buf);
        s_cmd = CMD_NONE;
        if (bsp_lvgl_lock(500)) { snprintf(s_line, sizeof(s_line), "audio unavailable"); status_refresh(); bsp_lvgl_unlock(); }
        return;
    }

    s_recording = true;
    int fill = 0;
    int16_t chunk[256];                     // 512B = 16ms，短读让停止按键及时生效
    int ui_skip = 0;
    while (!s_exit && s_cmd == CMD_RECORD && fill < (int)cap) {
        if (bsp_audio_read(chunk, sizeof(chunk)) != ESP_OK) break;
        int peak = 0;
        for (size_t i = 0; i < sizeof(chunk) / sizeof(chunk[0]); i++) {
            int v = chunk[i] < 0 ? -chunk[i] : chunk[i];
            if (v > peak) peak = v;
        }
        memcpy(buf + fill, chunk, sizeof(chunk));
        fill += sizeof(chunk);
        if (++ui_skip >= 3 && bsp_lvgl_lock(200)) {     // ~50ms 刷新一次，控制锁竞争
            ui_skip = 0;
            int level = peak * 100 / 32768;
            lv_bar_set_value(s_rec_bar, level > 100 ? 100 : level, LV_ANIM_OFF);
            lv_label_set_text_fmt(s_rec_sec, "%ds / %ds", fill / REC_BPS, (int)(cap / REC_BPS));
            bsp_lvgl_unlock();
        }
    }
    bool submit = !s_exit && s_cmd == CMD_STOP_SEND && fill > 0;
    s_cmd = CMD_NONE;
    s_recording = false;

    if (submit) {
        if (bsp_lvgl_lock(500)) { lv_label_set_text(s_rec_hint, "Sending..."); bsp_lvgl_unlock(); }
        esp_err_t serr = tasks_client_post_feedback(task_id, buf, fill, REC_HZ);
        if (bsp_lvgl_lock(500)) {
            lv_label_set_text(s_rec_hint, serr == ESP_OK ? "Submitted" : "Send failed");
            bsp_lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(900));
    }
    free(buf);
    if (!s_exit && bsp_lvgl_lock(500)) {
        detail_show();
        bsp_lvgl_unlock();
    }
}

static void worker_task(void *arg) {
    (void)arg;
    int64_t last_poll = 0;
    esp_err_t werr = app_wifi_start();
    if (werr != ESP_OK) ESP_LOGE(TAG, "wifi start: %s", esp_err_to_name(werr));

    while (!s_exit) {
        if (s_cmd == CMD_RECORD) {
            do_record();
        } else if (!s_recording) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now - last_poll >= POLL_PERIOD_MS) {
                last_poll = now;
                if (app_wifi_is_connected()) {
                    do_poll();
                } else if (bsp_lvgl_lock(200)) {
                    ip_text(s_line, sizeof(s_line));
                    status_refresh();
                    bsp_lvgl_unlock();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(WORKER_TICK_MS));
    }
    s_worker = NULL;
    vTaskDelete(NULL);
}

void demo_tasks_enter(void) {
    tasks_model_init(&s_model);
    s_exit = false;
    s_cmd = CMD_NONE;
    s_recording = false;
    s_view = VIEW_LIST;
    snprintf(s_line, sizeof(s_line), "Wi-Fi...");

    s_scr = ui_pixel_screen_create("CINDY");
    s_line_label = ui_pixel_label(s_scr, s_line, &lv_font_montserrat_14, UI_SKY_DARK);
    lv_obj_align(s_line_label, LV_ALIGN_TOP_RIGHT, -8, 46);

    s_box_list = make_box(s_scr, 68);
    s_box_detail = make_box(s_scr, 68);
    s_box_record = make_box(s_scr, 68);
    lv_obj_add_flag(s_box_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_box_record, LV_OBJ_FLAG_HIDDEN);

    list_rebuild();

    if (xTaskCreate(worker_task, "tasks_work", 8192, NULL, 5, &s_worker) != pdPASS) {
        s_worker = NULL;
        ESP_LOGE(TAG, "worker task create failed");
    }
    lv_screen_load(s_scr);
}

void demo_tasks_exit(void) {
    s_exit = true;
    int waited = 0;
    while (s_worker && waited < 4000) {
        bsp_lvgl_unlock();                  // 放锁让 worker 完成最后的 UI 清理
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
        while (!bsp_lvgl_lock(100)) vTaskDelay(pdMS_TO_TICKS(10));
    }
    s_scr = NULL;
    s_line_label = NULL;
    s_box_list = s_box_detail = s_box_record = NULL;
    s_rec_sec = s_rec_bar = s_rec_hint = NULL;
    for (int i = 0; i < TASKS_MODEL_MAX; i++) s_cards[i] = NULL;
}

void demo_tasks_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!bsp_lvgl_lock(500)) return;
    if (ev == BSP_BTN_CLICK && !s_recording) {
        int delta = btn == BSP_BTN_UP ? -1 : 1;
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            if (s_view == VIEW_LIST) {
                tasks_model_move(&s_model, delta);
                list_highlight();
            } else if (s_view == VIEW_DETAIL) {
                tasks_model_move(&s_model, delta);
                detail_show();
            }
        } else if (btn == BSP_BTN_OK) {
            if (s_view == VIEW_LIST && s_model.count > 0) {
                detail_show();
            } else if (s_view == VIEW_DETAIL) {
                s_cmd = CMD_RECORD;
            } else if (s_view == VIEW_RECORD && s_cmd == CMD_RECORD) {
                s_cmd = CMD_STOP_SEND;
            }
        }
    }
    bsp_lvgl_unlock();
}
