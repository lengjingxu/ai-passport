#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// STA 连接的公共入口：幂等，首次调用时初始化 NVS/netif/事件循环并启动连接。
// 失败不重试连接；断线后由事件回调自动重连。
esp_err_t app_wifi_start(void);

// 等待拿到 IP。超时返回 false。
bool app_wifi_wait(size_t timeout_ms);

bool app_wifi_is_connected(void);
