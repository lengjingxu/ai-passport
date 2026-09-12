#include "passport_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    unsigned char action[PASSPORT_ACTION_BYTES];
    assert(passport_encode_action(PASSPORT_CONFIRM, "session-1", 0x12345678, action) == 46);
    assert(action[0] == 2 && action[1] == 5 && !strcmp((char *)action + 2, "session-1"));
    assert(action[42] == 0x78 && action[43] == 0x56 && action[44] == 0x34 && action[45] == 0x12);
    assert(passport_encode_action(0, "session-1", 0, action) == -1);
    assert(passport_encode_action(PASSPORT_CONFIRM, "", 0, action) == -1);
    unsigned char frame[4 + PASSPORT_ITEM_BYTES] = {0};
    frame[0] = (sizeof(frame) - 2) & 255;
    frame[1] = (sizeof(frame) - 2) >> 8;
    frame[2] = 1; frame[3] = 1;
    strcpy((char *)frame + 4, "session-1");
    strcpy((char *)frame + 4 + TASK_ID_LEN, "Task title");
    strcpy((char *)frame + 4 + TASK_ID_LEN + TASK_TITLE_LEN, "waiting");
    strcpy((char *)frame + 4 + TASK_ID_LEN + TASK_TITLE_LEN + TASK_STATUS_LEN, "Needs your input");
    task_item_t items[TASKS_MODEL_MAX];
    int count = -1;
    for (size_t split = 1; split < sizeof(frame); ++split) {
        passport_decoder_t d = {0};
        assert(passport_decode(&d, frame, split, items, &count) == 0);
        assert(passport_decode(&d, frame + split, sizeof(frame) - split, items, &count) == 1);
        assert(count == 1 && !strcmp(items[0].id, "session-1"));
        assert(!strcmp(items[0].message, "Needs your input"));
        assert(tasks_model_chip(items[0].status) == TASK_CHIP_WAITING);
    }
    passport_decoder_t d = {0};
    for (size_t i = 0; i < sizeof(frame); ++i)
        assert(passport_decode(&d, frame + i, 1, items, &count) == (i + 1 == sizeof(frame)));
    const unsigned char empty[] = {2, 0, 1, 0};
    assert(passport_decode(&d, empty, sizeof(empty), items, &count) == 1 && count == 0);
    const unsigned char oversized[] = {255,255};
    assert(passport_decode(&d, oversized, 2, items, &count) == -1);
    frame[2] = 2;
    assert(passport_decode(&d, frame, sizeof(frame), items, &count) == -1);
    frame[2] = 1; frame[3] = 9;
    assert(passport_decode(&d, frame, sizeof(frame), items, &count) == -1);
    frame[3] = 1; memset(frame + 4, 'x', TASK_ID_LEN);
    assert(passport_decode(&d, frame, sizeof(frame), items, &count) == -1);
    assert(passport_decode(&d, empty, sizeof(empty), items, &count) == 1);
    // Disconnect discards a partial frame. A fresh connection accepts its own snapshot.
    assert(passport_decode(&d, frame, 8, items, &count) == 0);
    d.used = 0;
    assert(passport_decode(&d, empty, sizeof(empty), items, &count) == 1);
    puts("Passport protocol: PASS (fragmentation, limits, validation, reconnect)");
}
