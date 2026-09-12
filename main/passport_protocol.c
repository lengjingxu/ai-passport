#include "passport_protocol.h"
#include <string.h>

int passport_encode_action(passport_action_t action, const char *id, uint32_t token,
                           unsigned char out[PASSPORT_ACTION_BYTES])
{
    if (action < PASSPORT_OPEN || action > PASSPORT_CANCEL || !id || !id[0] || strlen(id) >= TASK_ID_LEN) return -1;
    memset(out, 0, PASSPORT_ACTION_BYTES);
    out[0] = 2; out[1] = action;
    memcpy(out + 2, id, strlen(id));
    for (int i = 0; i < 4; i++) out[2 + TASK_ID_LEN + i] = token >> (8 * i);
    return PASSPORT_ACTION_BYTES;
}

int passport_decode(passport_decoder_t *d, const void *bytes, size_t len,
                    task_item_t *out, int *count)
{
    if (!len || len > sizeof(d->data) - d->used) goto invalid;
    memcpy(d->data + d->used, bytes, len);
    d->used += len;
    if (d->used < 2) return 0;
    size_t n = d->data[0] | ((size_t)d->data[1] << 8);
    if (n < 2 || n > PASSPORT_PAYLOAD_MAX || d->used > n + 2) goto invalid;
    if (d->used < n + 2) return 0;
    int total = d->data[3];
    if (d->data[2] != 1 || total > TASKS_MODEL_MAX ||
        n != 2 + (size_t)total * PASSPORT_ITEM_BYTES) goto invalid;
    const unsigned char *p = d->data + 4;
    for (int i = 0; i < total; ++i) {
        // Check every fixed field before copying; malformed frames never publish.
        const size_t widths[] = {TASK_ID_LEN, TASK_TITLE_LEN, TASK_STATUS_LEN, TASK_MESSAGE_LEN};
        size_t offset = 0;
        for (size_t j = 0; j < 4; ++j) {
            if (!memchr(p + offset, 0, widths[j])) goto invalid;
            offset += widths[j];
        }
        if (!p[0]) goto invalid;
        for (int j = 0; j < i; ++j) if (!strcmp(out[j].id, (const char *)p)) goto invalid;
        memset(&out[i], 0, sizeof(out[i]));
        memcpy(out[i].id, p, TASK_ID_LEN); p += TASK_ID_LEN;
        memcpy(out[i].title, p, TASK_TITLE_LEN); p += TASK_TITLE_LEN;
        memcpy(out[i].status, p, TASK_STATUS_LEN); p += TASK_STATUS_LEN;
        memcpy(out[i].message, p, TASK_MESSAGE_LEN); p += TASK_MESSAGE_LEN;
    }
    *count = total;
    d->used = 0;
    return 1;
invalid:
    d->used = 0;
    return -1;
}
