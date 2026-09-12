#include "tasks_model.h"

#include <string.h>

static void copy_str(char *dst, size_t cap, const char *src) {
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

void tasks_model_init(tasks_model_t *m) {
    memset(m, 0, sizeof(*m));
}

bool tasks_model_set_items(tasks_model_t *m, const task_item_t *items, int count) {
    if (count < 0) count = 0;
    if (count > TASKS_MODEL_MAX) count = TASKS_MODEL_MAX;

    char keep_id[TASK_ID_LEN] = "";
    if (m->count > 0 && m->selected >= 0 && m->selected < m->count) {
        copy_str(keep_id, sizeof(keep_id), m->items[m->selected].id);
    }

    bool changed = (m->count != count);
    for (int i = 0; i < count && !changed; i++) {
        const task_item_t *a = &m->items[i];
        const task_item_t *b = &items[i];
        changed = strcmp(a->id, b->id) != 0 ||
                  strcmp(a->title, b->title) != 0 ||
                  strcmp(a->status, b->status) != 0 ||
                  strcmp(a->message, b->message) != 0 ||
                  a->updated_at != b->updated_at;
    }
    if (!changed) return false;

    for (int i = 0; i < count; i++) m->items[i] = items[i];
    m->count = count;

    m->selected = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(m->items[i].id, keep_id) == 0) {
            m->selected = i;
            break;
        }
    }
    return true;
}

void tasks_model_move(tasks_model_t *m, int delta) {
    if (m->count <= 0) return;
    int count = m->count;
    m->selected = ((m->selected + delta) % count + count) % count;
}

const task_item_t *tasks_model_current(const tasks_model_t *m) {
    if (m->count <= 0 || m->selected < 0 || m->selected >= m->count) return NULL;
    return &m->items[m->selected];
}

task_chip_t tasks_model_chip(const char *status) {
    if (!status) return TASK_CHIP_UNKNOWN;
    if (!strncmp(status, "draft:", 6)) return TASK_CHIP_WAITING;
    if (!strncmp(status, "retry:", 6) || !strncmp(status, "error:", 6)) return TASK_CHIP_FAILED;
    if (!strncmp(status, "asr:", 4) || !strcmp(status, "sending")) return TASK_CHIP_RUNNING;
    if (strcmp(status, "waiting") == 0) return TASK_CHIP_WAITING;
    if (strcmp(status, "queued") == 0) return TASK_CHIP_QUEUED;
    if (strcmp(status, "running") == 0 || strcmp(status, "in_progress") == 0) return TASK_CHIP_RUNNING;
    if (strcmp(status, "done") == 0 || strcmp(status, "completed") == 0) return TASK_CHIP_DONE;
    if (strcmp(status, "failed") == 0 || strcmp(status, "error") == 0) return TASK_CHIP_FAILED;
    return TASK_CHIP_UNKNOWN;
}

bool tasks_review_token(const char *status, uint32_t *token) {
    if ((!strncmp(status, "draft:", 6) || !strncmp(status, "retry:", 6)) && strlen(status) == 14) {
        uint32_t value = 0;
        for (int i = 6; i < 14; i++) {
            unsigned char c = status[i];
            int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (digit < 0) return false;
            value = value * 16 + digit;
        }
        *token = value; return true;
    }
    return false;
}

task_detail_action_t tasks_detail_action(const char *status, int button, uint32_t recording_token) {
    if (!strncmp(status, "asr:", 4) || !strcmp(status, "sending")) return TASK_BUSY;
    uint32_t token;
    if (tasks_review_token(status, &token)) {
        if (token != recording_token) return TASK_BUSY;
        return button < 0 ? TASK_RECORD_AGAIN : button > 0 ? TASK_READ_NEXT : TASK_SEND;
    }
    return button < 0 ? TASK_READ_PREVIOUS : button > 0 ? TASK_READ_NEXT : TASK_RECORD;
}

int tasks_model_preview(const char *message, char *out, size_t cap) {
    if (cap == 0) return 0;
    if (!message) message = "";
    size_t line = 0;
    while (message[line] != 0 && message[line] != '\n') line++;
    size_t n = line < cap - 1 ? line : cap - 1;
    // Preserve the UTF-8 boundary when the first line exceeds the byte budget.
    if (n < line) while (n > 0 && ((unsigned char)message[n] & 0xc0) == 0x80) n--;
    memcpy(out, message, n);
    out[n] = 0;
    return (int)n;
}

task_home_action_t tasks_home_action(bool recording, bool command_pending, bool sending) {
    if (recording) return TASK_HOME_CANCEL_RECORDING;
    if (command_pending || sending) return TASK_HOME_STAY;
    return TASK_HOME_RETURN;
}

const char *tasks_home_hint(bool connected, bool stale) {
    if (!connected) return "连接电脑端 Cindy\n设置 > 快捷键 > 配件\n启用 Cindy Passport";
    if (stale) return "同步已暂停\n请检查电脑端 Cindy";
    return "已连接 Cindy\n新任务会自动出现在这里";
}
