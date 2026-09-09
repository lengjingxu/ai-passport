#pragma once

// 本地配置（Wi-Fi 凭证、bridge 地址）。不入库；缺失时给出可操作的编译错误。
#if __has_include("app_config.h")
#include "app_config.h"
#else
#pragma message("main/app_config.h not found; building with placeholder Wi-Fi/bridge config")
#define APP_WIFI_SSID       "your-ssid"
#define APP_WIFI_PASSWORD   "your-password"
#define APP_BRIDGE_URL      "http://192.168.1.100:8787"
#endif
