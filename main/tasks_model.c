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
    if (strcmp(status, "waiting") == 0) return TASK_CHIP_WAITING;
    if (strcmp(status, "queued") == 0) return TASK_CHIP_QUEUED;
    if (strcmp(status, "running") == 0 || strcmp(status, "in_progress") == 0) return TASK_CHIP_RUNNING;
    if (strcmp(status, "done") == 0 || strcmp(status, "completed") == 0) return TASK_CHIP_DONE;
    if (strcmp(status, "failed") == 0 || strcmp(status, "error") == 0) return TASK_CHIP_FAILED;
    return TASK_CHIP_UNKNOWN;
}

int tasks_model_preview(const char *message, char *out, size_t cap) {
    if (cap == 0) return 0;
    if (!message) message = "";
    size_t line = 0;
    while (message[line] != 0 && message[line] != '\n') line++;
    size_t n = line < cap - 1 ? line : cap - 1;
    memcpy(out, message, n);
    out[n] = 0;
    return (int)n;
}
