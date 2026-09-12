#pragma once

// 任务列表的纯逻辑：条目结构、选择循环、状态归类、单行预览。
// 与 ESP-IDF/LVGL 无关，host tests 直接覆盖。
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TASKS_MODEL_MAX     8
#define TASK_ID_LEN         40
#define TASK_TITLE_LEN      80
#define TASK_STATUS_LEN     16
#define TASK_MESSAGE_LEN    192

typedef enum {
    TASK_CHIP_QUEUED = 0,
    TASK_CHIP_RUNNING,
    TASK_CHIP_DONE,
    TASK_CHIP_FAILED,
    TASK_CHIP_WAITING,
    TASK_CHIP_UNKNOWN,
} task_chip_t;

typedef struct {
    char id[TASK_ID_LEN];
    char title[TASK_TITLE_LEN];
    char status[TASK_STATUS_LEN];
    char message[TASK_MESSAGE_LEN];
    int64_t updated_at;                 // epoch 秒；服务器缺省时为 0
} task_item_t;

typedef struct {
    task_item_t items[TASKS_MODEL_MAX];
    int count;
    int selected;
} tasks_model_t;

void tasks_model_init(tasks_model_t *m);

// 用新条目整体替换；内容有变化时返回 true（selected 保持在同一 id 上）。
bool tasks_model_set_items(tasks_model_t *m, const task_item_t *items, int count);

void tasks_model_move(tasks_model_t *m, int delta);
const task_item_t *tasks_model_current(const tasks_model_t *m);

task_chip_t tasks_model_chip(const char *status);
typedef enum { TASK_READ_PREVIOUS, TASK_READ_NEXT, TASK_RECORD, TASK_RECORD_AGAIN, TASK_SEND, TASK_BUSY } task_detail_action_t;
// button: -1 = up, 0 = OK, 1 = down; independent of the hardware button driver.
task_detail_action_t tasks_detail_action(const char *status, int button, uint32_t recording_token);
bool tasks_review_token(const char *status, uint32_t *token);

// 取 message 的第一行；超出 cap-1 字节时按 UTF-8 字符边界截断（省略号交给 LVGL LONG_DOT 渲染）。
// 返回写入字节数。
int tasks_model_preview(const char *message, char *out, size_t cap);
