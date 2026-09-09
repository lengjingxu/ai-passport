#pragma once

// 本地配置（Wi-Fi 凭证、bridge 地址）。不入库；缺失时给出可操作的编译错误。
#if __has_include("app_config.h")
#include "app_config.h"
#else
#error "复制 main/app_config.h.example 为 main/app_config.h，填写 Wi-Fi 与 APP_BRIDGE_URL"
#endif
