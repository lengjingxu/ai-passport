// main/demo_tasks.c —— Cindy 任务面板：列表 / 详情 / 录音三视图 + 轮询与录音工作任务。
//
// 按键(页面内)：UP/DOWN 移动选中或切换任务；OK 短按 进详情 / 开始录音 / 停止并发送；
// OK 长按由 main.c 统一返回菜单，录音中返回即放弃本次录音。
// 16kHz/16bit/mono PCM streams through a bounded queue to the bridge (maximum 30s).
// 屏幕字体不含中文字形，界面文案一律英文。
#include "demo.h"
#include "app_wifi.h"
#include "tasks_client.h"
#include "tasks_model.h"
#include "ui_pixel.h"

#include "bsp_audio.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#define REC_HZ          16000
#define REC_BPS         (REC_HZ * 2)                 // 16bit mono = 32KB/s
#define REC_MAX_BYTES   (30 * REC_BPS)
#define POLL_PERIOD_MS  3000
#define WORKER_TICK_MS  150

typedef enum { VIEW_LIST = 0, VIEW_DETAIL, VIEW_RECORD } view_t;
typedef enum { CMD_NONE = 0, CMD_RECORD, CMD_STOP_SEND } cmd_t;

static const char *TAG = "demo_tasks";

static atomic_bool s_worker_running;
static atomic_bool s_exit;
static _Atomic(cmd_t) s_cmd;
static atomic_bool s_recording;

static tasks_model_t s_model;
static view_t s_view;
static char s_line[96];                 // 屏幕右上角状态行（IP / 错误）

static lv_obj_t *s_scr;
static lv_obj_t *s_line_label;
static lv_obj_t *s_battery_label;
static lv_obj_t *s_nav_label;
static lv_obj_t *s_box_list, *s_box_detail, *s_box_record;
static lv_obj_t *s_cards[TASKS_MODEL_MAX];
static lv_obj_t *s_rec_sec, *s_rec_bar, *s_rec_hint;

static const uint32_t CHIP_COLORS[] = {
    [TASK_CHIP_QUEUED]  = 0x78909C,
    [TASK_CHIP_RUNNING] = UI_SKY_DARK,
    [TASK_CHIP_DONE]    = UI_GRASS,
    [TASK_CHIP_FAILED]  = UI_RED,
    [TASK_CHIP_UNKNOWN] = 0x78909C,
};

static lv_obj_t *make_box(lv_obj_t *parent, int y) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(box, 0, y);
    lv_obj_set_size(box, 240, 194);
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
    lv_label_set_text(s_nav_label, v == VIEW_LIST ? "U/D: select   OK: open"
                     : v == VIEW_DETAIL ? "U/D: task   OK: record" : "Hold OK: cancel & exit");
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
        lv_obj_t *empty = ui_pixel_label(s_box_list, "No tasks yet\nWaiting for bridge",
                                         &lv_font_montserrat_14, 0x5A6B7A);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 110);
        ui_pixel_mascot_create(s_box_list, 101, 44);
        return;
    }
    for (int i = 0; i < s_model.count; i++) {
        const task_item_t *it = &s_model.items[i];
        lv_obj_t *card = ui_pixel_panel_create(s_box_list, 12, 6 + i * 88, 216, 80, UI_PAPER);

        lv_obj_t *title = ui_pixel_label(card, it->title, &lv_font_montserrat_14, UI_INK);
        lv_obj_set_width(title, 194);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *badge = ui_pixel_label(card, it->status,
                                         &lv_font_montserrat_14, CHIP_COLORS[tasks_model_chip(it->status)]);
        lv_obj_set_width(badge, 194);
        lv_label_set_long_mode(badge, LV_LABEL_LONG_DOT);
        lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 0, 20);

        char prev[64];
        tasks_model_preview(it->message, prev, sizeof(prev));
        lv_obj_t *msg = ui_pixel_label(card, prev, &lv_font_montserrat_14, 0x5A6B7A);
        lv_obj_set_width(msg, 194);
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
    lv_obj_t *panel = ui_pixel_panel_create(s_box_detail, 12, 6, 216, 182, UI_PAPER);

    lv_obj_t *title = ui_pixel_label(panel, it->title, &lv_font_montserrat_14, UI_INK);
    lv_obj_set_width(title, 190);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *status = ui_pixel_label(panel, it->status,
                                      &lv_font_montserrat_14, CHIP_COLORS[tasks_model_chip(it->status)]);
    lv_obj_set_width(status, 190);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 0, 23);

    lv_obj_t *msg = ui_pixel_label(panel, it->message, &lv_font_montserrat_14, 0x3A4A5A);
    lv_obj_set_width(msg, 190);
    lv_obj_set_height(msg, 108);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_SCROLL);
    lv_obj_align(msg, LV_ALIGN_TOP_LEFT, 0, 48);

    view_show(VIEW_DETAIL);
}

static void record_show(void) {
    lv_obj_clean(s_box_record);
    lv_obj_t *panel = ui_pixel_panel_create(s_box_record, 12, 6, 216, 182, UI_PAPER);
    const task_item_t *it = tasks_model_current(&s_model);
    lv_obj_t *title = ui_pixel_label(panel, it ? it->title : "", &lv_font_montserrat_14, UI_INK);
    lv_obj_set_width(title, 194);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *label = ui_pixel_label(panel, "VOICE FEEDBACK", &lv_font_montserrat_14, UI_RED);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 25);
    s_rec_sec = ui_pixel_label(panel, "0s / 30s", &lv_font_montserrat_20, UI_INK);
    lv_obj_align(s_rec_sec, LV_ALIGN_TOP_MID, 0, 53);
    s_rec_bar = lv_bar_create(panel);
    lv_obj_set_size(s_rec_bar, 190, 14);
    lv_obj_align(s_rec_bar, LV_ALIGN_TOP_MID, 0, 91);
    lv_obj_set_style_bg_color(s_rec_bar, lv_color_hex(UI_MUTED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_rec_bar, lv_color_hex(UI_GRASS), LV_PART_INDICATOR);
    lv_bar_set_range(s_rec_bar, 0, 100);
    s_rec_hint = ui_pixel_label(panel, "OK: stop & send", &lv_font_montserrat_14, UI_SKY_DARK);
    lv_obj_align(s_rec_hint, LV_ALIGN_TOP_MID, 0, 129);
    view_show(VIEW_RECORD);
}

static void status_refresh(void) {
    if (s_line_label) lv_label_set_text(s_line_label, s_line);
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
        snprintf(s_line, sizeof(s_line), "Bridge connected | %d tasks", count);
        if (tasks_model_set_items(&s_model, items, count)) {
            list_rebuild();
            if (s_view == VIEW_DETAIL) detail_show();
        }
    } else {
        snprintf(s_line, sizeof(s_line), "Bridge error: %s", esp_err_to_name(err));
    }
    status_refresh();
    bsp_lvgl_unlock();
}

typedef struct {
    size_t bytes;
    int16_t pcm[256];
} record_chunk_t;

typedef struct {
    QueueHandle_t queue;
    SemaphoreHandle_t done;
    esp_http_client_handle_t client;
    atomic_bool abort;
    atomic_int error;
    size_t sent_bytes;
    uint32_t max_write_ms;
} record_upload_t;

static void upload_recording(void *arg) {
    record_upload_t *upload = arg;
    record_chunk_t chunk;
    while (!upload->abort && !s_exit) {
        if (!xQueueReceive(upload->queue, &chunk, pdMS_TO_TICKS(50))) continue;
        if (upload->abort || s_exit) break;
        int64_t started_us = esp_timer_get_time();
        esp_err_t err = chunk.bytes ? tasks_feedback_write(upload->client, chunk.pcm, chunk.bytes)
                                   : tasks_feedback_finish(upload->client);
        uint32_t elapsed_ms = (esp_timer_get_time() - started_us) / 1000;
        if (elapsed_ms > upload->max_write_ms) upload->max_write_ms = elapsed_ms;
        if (err == ESP_OK) upload->sent_bytes += chunk.bytes;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "record upload %s failed: %s", chunk.bytes ? "write" : "finish", esp_err_to_name(err));
            upload->error = err;
        }
        if (err != ESP_OK || !chunk.bytes) break;
    }
    esp_http_client_cleanup(upload->client);
    // No access to caller-owned state after signalling completion.
    xSemaphoreGive(upload->done);
    vTaskDelete(NULL);
}

static void do_record(void) {
    char task_id[TASK_ID_LEN] = "";
    if (!bsp_lvgl_lock(800)) { s_cmd = CMD_NONE; return; }
    const task_item_t *it = tasks_model_current(&s_model);
    if (it) strncpy(task_id, it->id, sizeof(task_id) - 1);
    if (!task_id[0]) { bsp_lvgl_unlock(); s_cmd = CMD_NONE; return; }
    record_show();
    lv_label_set_text(s_rec_hint, "Connecting...");
    bsp_lvgl_unlock();

    ESP_LOGI(TAG, "record start: free=%u largest=%u queue=8x512",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    record_upload_t upload = {0};
    bool started = false;
    bool restore_wifi = false;
    wifi_ps_type_t saved_ps = WIFI_PS_NONE;
    size_t fill = 0;
    esp_err_t err = ESP_ERR_NO_MEM;
    upload.queue = xQueueCreate(8, sizeof(record_chunk_t));
    upload.done = xSemaphoreCreateBinary();
    if (!upload.queue || !upload.done) goto cleanup;
    err = esp_wifi_get_ps(&saved_ps);
    if (err != ESP_OK) goto cleanup;
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) goto cleanup;
    restore_wifi = true;
    err = tasks_feedback_open(task_id, &upload.client);
    if (err != ESP_OK) goto cleanup;
    err = bsp_audio_set_format(REC_HZ, 16, 1);
    if (err != ESP_OK) goto cleanup;
    if (xTaskCreate(upload_recording, "record_upload", 4096, &upload, 4, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    started = true;
    s_recording = true;
    if (bsp_lvgl_lock(200)) { lv_label_set_text(s_rec_hint, "OK: stop and send"); bsp_lvgl_unlock(); }
    record_chunk_t chunk = { .bytes = sizeof(chunk.pcm) };
    while (!s_exit && s_cmd == CMD_RECORD && fill < REC_MAX_BYTES) {
        if (upload.error != ESP_OK) { err = upload.error; break; }
        chunk.bytes = REC_MAX_BYTES - fill < sizeof(chunk.pcm) ? REC_MAX_BYTES - fill : sizeof(chunk.pcm);
        err = bsp_audio_read(chunk.pcm, chunk.bytes);
        if (err != ESP_OK) break;
        // A buffered microphone burst can fill the queue before the lower-priority
        // uploader runs. Waiting yields to it; sustained congestion still aborts.
        if (!xQueueSend(upload.queue, &chunk, pdMS_TO_TICKS(20))) {
            ESP_LOGE(TAG, "record queue full after 20ms: captured=%u", (unsigned)fill);
            err = ESP_ERR_TIMEOUT;
            break;
        }
        fill += chunk.bytes;
        if (fill % 4096 == 0 && bsp_lvgl_lock(10)) {
            int peak = 0;
            for (size_t i = 0; i < chunk.bytes / 2; ++i) {
                int v = chunk.pcm[i] < 0 ? -chunk.pcm[i] : chunk.pcm[i];
                if (v > peak) peak = v;
            }
            lv_bar_set_value(s_rec_bar, peak * 100 / 32768, LV_ANIM_OFF);
            lv_label_set_text_fmt(s_rec_sec, "%us / 30s", (unsigned)(fill / REC_BPS));
            bsp_lvgl_unlock();
        }
    }
    bool submit = err == ESP_OK && !s_exit && fill &&
                  (s_cmd == CMD_STOP_SEND || fill == REC_MAX_BYTES);
    if (submit) {
        if (bsp_lvgl_lock(100)) { lv_label_set_text(s_rec_hint, "Sending..."); bsp_lvgl_unlock(); }
        chunk.bytes = 0;
        // Keep cancellation responsive while the sender drains its bounded queue.
        while (!xQueueSend(upload.queue, &chunk, pdMS_TO_TICKS(20))) {
            if (s_exit || upload.error != ESP_OK) { submit = false; break; }
        }
    }
    upload.abort = !submit;
    xSemaphoreTake(upload.done, portMAX_DELAY);
    if (upload.error != ESP_OK) err = upload.error;
cleanup:
    if (!started && upload.client) esp_http_client_cleanup(upload.client);
    if (upload.queue) vQueueDelete(upload.queue);
    if (upload.done) vSemaphoreDelete(upload.done);
    if (restore_wifi) {
        esp_err_t restore_err = esp_wifi_set_ps(saved_ps);
        if (restore_err != ESP_OK) {
            ESP_LOGE(TAG, "record Wi-Fi restore failed: %s", esp_err_to_name(restore_err));
            if (err == ESP_OK) err = restore_err;
        }
    }
    s_cmd = CMD_NONE;
    s_recording = false;
    ESP_LOGI(TAG, "record end: captured=%u sent=%u max_write_ms=%u result=%s free=%u largest=%u",
             (unsigned)fill, (unsigned)upload.sent_bytes, (unsigned)upload.max_write_ms, esp_err_to_name(err), (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (!s_exit && bsp_lvgl_lock(500)) {
        snprintf(s_line, sizeof(s_line), err == ESP_OK ? (fill ? "Recording submitted" : "Recording cancelled")
                                                     : "Record failed: %s", esp_err_to_name(err));
        if (err == ESP_ERR_TIMEOUT) {
            snprintf(s_line, sizeof(s_line), "ESP_ERR_TIMEOUT\n%uB / %ums",
                     (unsigned)upload.sent_bytes, (unsigned)upload.max_write_ms);
        }
        detail_show();
        status_refresh();
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
            last_poll = esp_timer_get_time() / 1000;
        } else if (!s_recording) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now - last_poll >= POLL_PERIOD_MS) {
                last_poll = now;
                int battery = bsp_battery_soc();
                if (bsp_lvgl_lock(200)) {
                    if (battery >= 0) lv_label_set_text_fmt(s_battery_label, "%d%%", battery);
                    else lv_label_set_text(s_battery_label, "--%");
                    bsp_lvgl_unlock();
                }
                if (app_wifi_is_connected()) {
                    do_poll();
                } else if (bsp_lvgl_lock(200)) {
                    app_wifi_status(s_line, sizeof(s_line));
                    status_refresh();
                    bsp_lvgl_unlock();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(WORKER_TICK_MS));
    }
    app_wifi_stop();
    s_worker_running = false;
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
    s_line_label = ui_pixel_label(s_scr, s_line, &lv_font_montserrat_14, UI_INK);
    lv_obj_set_width(s_line_label, 224);
    lv_obj_set_height(s_line_label, 36);
    lv_label_set_long_mode(s_line_label, LV_LABEL_LONG_DOT);
    lv_obj_align(s_line_label, LV_ALIGN_TOP_LEFT, 8, 46);
    s_battery_label = ui_pixel_label(s_scr, "--%", &lv_font_montserrat_14, UI_INK);
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -8, 25);

    lv_obj_t *source = ui_pixel_label(s_scr, "Source: bridge file", &lv_font_montserrat_14, UI_INK);
    lv_obj_align(source, LV_ALIGN_TOP_LEFT, 8, 282);
    s_nav_label = ui_pixel_label(s_scr, "U/D: select   OK: open", &lv_font_montserrat_14, UI_INK);
    lv_obj_align(s_nav_label, LV_ALIGN_TOP_LEFT, 8, 299);

    s_box_list = make_box(s_scr, 88);
    lv_obj_add_flag(s_box_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_box_list, LV_DIR_VER);
    s_box_detail = make_box(s_scr, 88);
    s_box_record = make_box(s_scr, 88);
    lv_obj_add_flag(s_box_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_box_record, LV_OBJ_FLAG_HIDDEN);

    list_rebuild();

    s_worker_running = true;
    if (xTaskCreate(worker_task, "tasks_work", 8192, NULL, 5, NULL) != pdPASS) {
        s_worker_running = false;
        snprintf(s_line, sizeof(s_line), "Task worker: no memory");
        status_refresh();
        ESP_LOGE(TAG, "worker task create failed");
    }
    lv_screen_load(s_scr);
}

void demo_tasks_exit(void) {
    s_exit = true;
    while (s_worker_running) {
        bsp_lvgl_unlock();                  // 放锁让 worker 完成最后的 UI 清理
        vTaskDelay(pdMS_TO_TICKS(20));
        while (!bsp_lvgl_lock(100)) vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_scr) lv_obj_delete(s_scr);
    s_scr = NULL;
    s_line_label = NULL;
    s_battery_label = NULL;
    s_nav_label = NULL;
    s_box_list = s_box_detail = s_box_record = NULL;
    s_rec_sec = s_rec_bar = s_rec_hint = NULL;
    for (int i = 0; i < TASKS_MODEL_MAX; i++) s_cards[i] = NULL;
}

void demo_tasks_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    // The application input dispatcher already holds the LVGL lock.
    if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK && s_cmd == CMD_RECORD) {
        s_cmd = CMD_STOP_SEND;
        return;
    }
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
}
