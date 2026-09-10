"""Exercise the real STA lifecycle with host-side ESP-IDF test doubles."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

API = r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
typedef int esp_err_t;
typedef int esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef unsigned EventBits_t;
typedef unsigned *EventGroupHandle_t;
typedef struct { int unused; } esp_netif_t;
typedef struct { int unused; } wifi_init_config_t;
typedef struct { struct { unsigned char ssid[32], password[64]; } sta; } wifi_config_t;
typedef struct { int reason; } wifi_event_sta_disconnected_t;
typedef void (*handler_t)(void *, esp_event_base_t, int32_t, void *);
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_ARG 2
#define WIFI_EVENT 1
#define IP_EVENT 2
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 10
#define WIFI_EVENT_STA_CONNECTED 11
#define WIFI_EVENT_STA_DISCONNECTED 12
#define IP_EVENT_STA_GOT_IP 20
#define IP_EVENT_STA_LOST_IP 21
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 0
#define WIFI_IF_STA 0
#define BIT0 1
#define pdFALSE 0
#define pdMS_TO_TICKS(x) (x)
#define WIFI_INIT_CONFIG_DEFAULT() {0}
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
extern handler_t wh, ih;
extern int netifs, groups, initialized, started, connections, fail_config;
static inline const char *esp_err_to_name(int err) { return err ? "TEST_ERROR" : "ESP_OK"; }
static inline int demo_radio_nvs_prepare(void) { return 0; }
static inline int demo_radio_network_prepare(void) { return 0; }
static inline EventGroupHandle_t xEventGroupCreate(void) { groups++; return calloc(1, sizeof(unsigned)); }
static inline void vEventGroupDelete(EventGroupHandle_t e) { groups--; free(e); }
static inline void xEventGroupClearBits(EventGroupHandle_t e, unsigned b) { *e &= ~b; }
static inline void xEventGroupSetBits(EventGroupHandle_t e, unsigned b) { *e |= b; }
static inline unsigned xEventGroupGetBits(EventGroupHandle_t e) { return *e; }
static inline unsigned xEventGroupWaitBits(EventGroupHandle_t e, unsigned b, int c, int a, size_t t) {
    (void)b; (void)c; (void)a; (void)t; return *e;
}
static inline esp_netif_t *esp_netif_create_default_wifi_sta(void) { netifs++; return calloc(1, sizeof(esp_netif_t)); }
static inline void esp_netif_destroy_default_wifi(esp_netif_t *n) { netifs--; free(n); }
static inline int esp_wifi_init(wifi_init_config_t *c) { (void)c; assert(netifs == 1); initialized++; return 0; }
static inline int esp_wifi_deinit(void) { initialized--; return 0; }
static inline int esp_wifi_connect(void) { connections++; return 0; }
static inline int esp_wifi_start(void) { assert(netifs == 1 && wh && ih); started++; wh(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL); return 0; }
static inline int esp_wifi_stop(void) { started--; return 0; }
static inline int esp_wifi_set_storage(int a) { (void)a; return 0; }
static inline int esp_wifi_set_mode(int a) { (void)a; return 0; }
static inline int esp_wifi_set_config(int a, wifi_config_t *c) {
    (void)a; assert(c->sta.ssid[0]); return fail_config;
}
static inline int esp_event_handler_instance_register(int b, int id, handler_t h, void *arg, void **out) {
    (void)id; (void)arg; if (b == WIFI_EVENT) wh=h; else ih=h; *out=(void *)(intptr_t)b; return 0;
}
static inline int esp_event_handler_instance_unregister(int b, int id, void *h) {
    (void)id; (void)h; if (b == WIFI_EVENT) wh=NULL; else ih=NULL; return 0;
}
'''
TEST = r'''
#include "api.h"
#include "app_wifi.h"
handler_t wh, ih;
int netifs, groups, initialized, started, connections, fail_config;
static void released(void) {
    assert(!netifs && !groups && !initialized && !started && !wh && !ih);
    assert(!app_wifi_is_connected());
}
int main(void) {
    char status[96];
    for (int i=0;i<3;i++) {
        assert(app_wifi_start()==ESP_OK);
        assert(app_wifi_start()==ESP_OK);
        assert(netifs==1 && groups==1 && initialized==1 && started==1);
        assert(!app_wifi_is_connected());
        app_wifi_status(status,sizeof(status)); assert(strstr(status,"connecting"));
        wh(NULL,WIFI_EVENT,WIFI_EVENT_STA_CONNECTED,NULL);
        app_wifi_status(status,sizeof(status)); assert(strstr(status,"getting IP"));
        ih(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
        assert(app_wifi_is_connected());
        ih(NULL,IP_EVENT,IP_EVENT_STA_LOST_IP,NULL);
        assert(!app_wifi_is_connected());
        ih(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
        int before=connections;
        wifi_event_sta_disconnected_t event={202};
        wh(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&event);
        assert(!app_wifi_is_connected() && connections==before+1);
        app_wifi_status(status,sizeof(status)); assert(strstr(status,"202"));
        app_wifi_stop(); released(); app_wifi_stop(); released();
    }
    fail_config=ESP_ERR_INVALID_ARG;
    assert(app_wifi_start()==ESP_ERR_INVALID_ARG); released();
    app_wifi_status(status,sizeof(status)); assert(strstr(status,"TEST_ERROR"));
    fail_config=0;
    assert(app_wifi_start()==ESP_OK);
    app_wifi_stop(); released();
}
'''

class WifiLifecycleTests(unittest.TestCase):
    def test_station_dhcp_disconnect_cleanup_and_reentry(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            for name in ('app_wifi.c', 'app_wifi.h', 'app_config_loader.h'):
                (temp / name).write_bytes((ROOT / 'main' / name).read_bytes())
            (temp / 'api.h').write_text(API)
            (temp / 'app_config.h').write_text(
                '#define APP_WIFI_SSID "test-network"\n'
                '#define APP_WIFI_PASSWORD "test-password"\n')
            headers = ('esp_err.h', 'demo_radio.h', 'esp_event.h', 'esp_log.h',
                       'esp_netif.h', 'esp_wifi.h', 'esp_wifi_default.h',
                       'freertos/FreeRTOS.h', 'freertos/event_groups.h')
            for name in headers:
                path = temp / name
                path.parent.mkdir(exist_ok=True)
                path.write_text('#include "api.h"\n')
            (temp / 'test.c').write_text(TEST)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(temp), str(temp / 'app_wifi.c'),
                            str(temp / 'test.c'), '-o', str(temp / 'test')], check=True)
            subprocess.run([str(temp / 'test')], check=True)

if __name__ == '__main__':
    unittest.main()
