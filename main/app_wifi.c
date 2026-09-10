#include "app_wifi.h"
#include "app_config_loader.h"
#include "demo_radio.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#define WIFI_CONNECTED_BIT BIT0
static const char *TAG = "app_wifi";
static EventGroupHandle_t s_events;
static esp_netif_t *s_netif;
static esp_event_handler_instance_t s_wifi_handler, s_ip_handler;
static bool s_initialized, s_started;
static atomic_bool s_stopping;
static atomic_int s_reason, s_error;
static atomic_bool s_associated;

static void connect_station(void) {
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) s_error = err;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (s_stopping) return;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        connect_station();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        s_associated = true;
        s_reason = 0;
        s_error = ESP_OK;
        ESP_LOGI(TAG, "AP associated; waiting for DHCP");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_associated = false;
        s_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected: reason=%d", (int)s_reason);
        connect_station();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_error = ESP_OK;
        s_reason = 0;
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "DHCP complete; bridge polling enabled");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Station lost its IP address");
    }
}

void app_wifi_stop(void) {
    s_stopping = true;
    if (s_wifi_handler) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        s_wifi_handler = NULL;
    }
    if (s_ip_handler) {
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, s_ip_handler);
        s_ip_handler = NULL;
    }
    if (s_started) esp_wifi_stop();
    if (s_initialized) esp_wifi_deinit();
    s_started = s_initialized = false;
    if (s_netif) {
        esp_netif_destroy_default_wifi(s_netif);
        s_netif = NULL;
    }
    if (s_events) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }
}

esp_err_t app_wifi_start(void) {
    if (s_started) return ESP_OK;
    s_stopping = false;
    s_associated = false;
    s_reason = 0;
    s_error = ESP_OK;
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) goto fail;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) goto fail;
    s_events = xEventGroupCreate();
    if (!s_events) { err = ESP_ERR_NO_MEM; goto fail; }
    // Own a station netif and DHCP client for the whole Tasks page lifetime.
    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) { err = ESP_ERR_NO_MEM; goto fail; }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) goto fail;
    s_initialized = true;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) goto fail;
    err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, &s_ip_handler);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto fail;
    wifi_config_t wifi_cfg = { 0 };
    size_t ssid_len = strlen(APP_WIFI_SSID), pass_len = strlen(APP_WIFI_PASSWORD);
    if (!ssid_len || ssid_len > sizeof(wifi_cfg.sta.ssid) || pass_len > sizeof(wifi_cfg.sta.password)) {
        err = ESP_ERR_INVALID_ARG;
        goto fail;
    }
    memcpy(wifi_cfg.sta.ssid, APP_WIFI_SSID, ssid_len);
    memcpy(wifi_cfg.sta.password, APP_WIFI_PASSWORD, pass_len);
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_started = true;
    return ESP_OK;
fail:
    app_wifi_stop();
    s_error = err;
    ESP_LOGE(TAG, "Wi-Fi initialization: %s", esp_err_to_name(err));
    return err;
}

bool app_wifi_wait(size_t timeout_ms) {
    if (!s_started) return false;
    return (xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                               pdMS_TO_TICKS(timeout_ms)) & WIFI_CONNECTED_BIT) != 0;
}

bool app_wifi_is_connected(void) {
    return s_events && (xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT) != 0;
}

void app_wifi_status(char *out, size_t cap) {
    int error = s_error, reason = s_reason;
    if (error != ESP_OK) snprintf(out, cap, "WiFi error: %s", esp_err_to_name(error));
    else if (app_wifi_is_connected()) snprintf(out, cap, "WiFi connected");
    else if (reason) snprintf(out, cap, "WiFi retry: reason %d", reason);
    else snprintf(out, cap, "%s", s_associated ? "WiFi: getting IP" : "WiFi: connecting");
}
