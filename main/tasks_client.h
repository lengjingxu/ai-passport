#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "tasks_model.h"

// 阻塞式请求，须在工作任务中调用。
esp_err_t tasks_client_fetch(task_item_t *out, int max, int *count);

// Streaming PCM. The worker owns the handle and must close it on every path.
#include "esp_http_client.h"
esp_err_t tasks_feedback_open(const char *task_id, esp_http_client_handle_t *out);
esp_err_t tasks_feedback_write(esp_http_client_handle_t client, const void *pcm, size_t bytes);
esp_err_t tasks_feedback_finish(esp_http_client_handle_t client);
