#pragma once
#include "tasks_model.h"

// v1: little-endian payload length, version, count, then fixed-width task fields.
#define PASSPORT_ITEM_BYTES (TASK_ID_LEN + TASK_TITLE_LEN + TASK_STATUS_LEN + TASK_MESSAGE_LEN)
#define PASSPORT_PAYLOAD_MAX (2 + TASKS_MODEL_MAX * PASSPORT_ITEM_BYTES)
#define PASSPORT_ACTION_BYTES (2 + TASK_ID_LEN + 4)
typedef enum {
    PASSPORT_OPEN = 1, PASSPORT_PREVIOUS, PASSPORT_NEXT,
    PASSPORT_RETRY, PASSPORT_CONFIRM, PASSPORT_CANCEL,
} passport_action_t;
int passport_encode_action(passport_action_t action, const char *id, uint32_t token,
                           unsigned char out[PASSPORT_ACTION_BYTES]);
typedef struct {
    unsigned char data[PASSPORT_PAYLOAD_MAX + 2];
    size_t used;
} passport_decoder_t;
// Returns -1 for invalid framing, 0 for incomplete, 1 for a complete snapshot.
// One write may not cross a frame boundary; reset after disconnect/error.
int passport_decode(passport_decoder_t *d, const void *bytes, size_t len,
                    task_item_t *out, int *count);
