#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "tasks_model.h"

// 阻塞式请求，须在工作任务中调用。
esp_err_t tasks_client_fetch(task_item_t *out, int max, int *count);

// 把录音 PCM 提交给指定任务。bytes 为字节数。
esp_err_t tasks_client_post_feedback(const char *task_id, const void *pcm,
                                     size_t bytes, uint32_t hz);
