#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// Worker-owned lifecycle. Creates STA/DHCP and releases it on page exit.
esp_err_t app_wifi_start(void);
void app_wifi_stop(void);
void app_wifi_status(char *out, size_t cap);

// 等待拿到 IP。超时返回 false。
bool app_wifi_wait(size_t timeout_ms);

bool app_wifi_is_connected(void);
