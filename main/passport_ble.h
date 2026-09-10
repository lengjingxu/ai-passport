#pragma once
#include "esp_err.h"
#include "tasks_model.h"

esp_err_t passport_ble_start(void);
esp_err_t passport_ble_stop(void);
// Called by the page worker. No BLE callback accesses LVGL.
bool passport_ble_snapshot(task_item_t *items, int *count);
void passport_ble_status(char *out, size_t cap);
esp_err_t passport_ble_open_task(const char *id);

bool passport_ble_connected(void);

// Recording sender only; waits for the central indication acknowledgement.
esp_err_t passport_ble_voice_send(const void *data, size_t size);
