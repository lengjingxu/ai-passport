// Cindy appliance: boot into the task home; dispatch buttons to its navigation.
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_pins.h"      // 错误日志里要打印 BSP_LCD_* 引脚号
#include "demo.h"
#include "ui_pixel.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "main";

typedef struct { bsp_btn_t btn; bsp_btn_ev_t ev; } key_event_t;
static QueueHandle_t s_keys;

// The button task only queues input; page lifecycle runs in the application task.
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    key_event_t event = { btn, ev };
    if (xQueueSend(s_keys, &event, 0) != pdTRUE) ESP_LOGW(TAG, "Key queue full");
}

static void handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!bsp_lvgl_lock(500)) return;

    demo_tasks_key(btn, ev);
    bsp_lvgl_unlock();
}

void app_main(void) {
    ESP_LOGI(TAG, "Cindy Passport starting");
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "休眠唤醒原因: %d", wakeup);
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    // Cindy requires a working display; report initialization errors explicitly.
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,Cindy 无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    s_keys = xQueueCreate(8, sizeof(key_event_t));
    if (!s_keys || bsp_button_init(on_key, NULL) != ESP_OK ||
        bsp_audio_init() != ESP_OK || bsp_battery_init() != ESP_OK) {
        ESP_LOGE(TAG, "Cindy hardware initialization failed");
        if (bsp_lvgl_lock(1000)) {
            lv_obj_t *screen = ui_pixel_screen_create("CINDY");
            lv_obj_t *panel = ui_pixel_panel_create(screen, 12, 90, 216, 150, UI_PAPER);
            lv_obj_t *label = ui_pixel_label(panel,
                "Startup failed\nRestart the device\nCheck hardware logs",
                &lv_font_montserrat_14, UI_RED);
            lv_obj_center(label);
            lv_screen_load(screen);
            bsp_lvgl_unlock();
        }
        return;
    }
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "Cindy startup: display lock unavailable");
        return;
    }
    demo_tasks_ble_enter();
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "Cindy ready");
    key_event_t event;
    while (xQueueReceive(s_keys, &event, portMAX_DELAY) == pdTRUE) {
        handle_key(event.btn, event.ev);
    }
}
