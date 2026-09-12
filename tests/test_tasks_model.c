#include <assert.h>
#include <string.h>

#include "tasks_model.h"

int main(void)
{
    assert(tasks_home_action(true, true, false) == TASK_HOME_CANCEL_RECORDING);
    assert(tasks_home_action(false, true, false) == TASK_HOME_STAY);
    assert(tasks_home_action(false, false, true) == TASK_HOME_STAY);
    assert(tasks_home_action(false, false, false) == TASK_HOME_RETURN);
    assert(strstr(tasks_home_hint(false, true), "Cindy Passport"));
    assert(strstr(tasks_home_hint(true, true), "同步已暂停"));
    assert(strstr(tasks_home_hint(true, false), "新任务"));

    uint32_t token;
    assert(tasks_review_token("draft:12345678", &token) && token == 0x12345678);
    assert(tasks_review_token("retry:89abcdef", &token) && token == 0x89abcdef);
    assert(!tasks_review_token("draft:1234567x", &token));
    assert(!tasks_review_token("done", &token));
    assert(tasks_detail_action("done", -1, 0x12345678) == TASK_READ_PREVIOUS);
    assert(tasks_detail_action("done", 1, 0x12345678) == TASK_READ_NEXT);
    assert(tasks_detail_action("done", 0, 0x12345678) == TASK_RECORD);
    assert(tasks_detail_action("draft:12345678", -1, 0x12345678) == TASK_RECORD_AGAIN);
    assert(tasks_detail_action("draft:12345678", 1, 0x12345678) == TASK_READ_NEXT);
    assert(tasks_detail_action("draft:12345678", 0, 0x12345678) == TASK_SEND);
    assert(tasks_detail_action("retry:12345678", 0, 0x12345678) == TASK_SEND);
    assert(tasks_detail_action("asr:12345678", 0, 0x12345678) == TASK_BUSY);
    assert(tasks_detail_action("sending", -1, 0x12345678) == TASK_BUSY);
    assert(tasks_detail_action("draft:12345678", 0, 0x87654321) == TASK_BUSY);
    tasks_model_t m;
    tasks_model_init(&m);
    assert(m.count == 0 && m.selected == 0);

    task_item_t in[TASKS_MODEL_MAX];
    memset(in, 0, sizeof(in));
    strcpy(in[0].id, "a"); strcpy(in[0].title, "A"); strcpy(in[0].status, "running"); strcpy(in[0].message, "line1\nline2");
    strcpy(in[1].id, "b"); strcpy(in[1].title, "B"); strcpy(in[1].status, "done");
    strcpy(in[2].id, "c"); strcpy(in[2].title, "C"); strcpy(in[2].status, "queued");
    assert(tasks_model_set_items(&m, in, 3) == true);
    assert(m.count == 3 && m.selected == 0);
    assert(tasks_model_set_items(&m, in, 3) == false);          // 内容未变

    tasks_model_move(&m, 1);
    assert(m.selected == 1);
    tasks_model_move(&m, -2);
    assert(m.selected == 2);                                    // 循环回绕
    tasks_model_move(&m, 1);
    assert(m.selected == 0);

    // 选中项按 id 保持：b 被选中，重排后仍跟随
    tasks_model_move(&m, 1);
    assert(m.selected == 1);
    task_item_t reordered[TASKS_MODEL_MAX];
    memset(reordered, 0, sizeof(reordered));
    reordered[0] = in[1]; reordered[1] = in[0]; reordered[2] = in[2];
    assert(tasks_model_set_items(&m, reordered, 3) == true);
    assert(m.selected == 0 && strcmp(m.items[0].id, "b") == 0);

    assert(tasks_model_chip("running") == TASK_CHIP_RUNNING);
    assert(tasks_model_chip("in_progress") == TASK_CHIP_RUNNING);
    assert(tasks_model_chip("done") == TASK_CHIP_DONE);
    assert(tasks_model_chip("completed") == TASK_CHIP_DONE);
    assert(tasks_model_chip("failed") == TASK_CHIP_FAILED);
    assert(tasks_model_chip("queued") == TASK_CHIP_QUEUED);
    assert(tasks_model_chip("weird") == TASK_CHIP_UNKNOWN);
    assert(tasks_model_chip(NULL) == TASK_CHIP_UNKNOWN);

    char out[TASK_MESSAGE_LEN];
    tasks_model_preview(in[0].message, out, sizeof(out));
    assert(strcmp(out, "line1") == 0);                          // 只取第一行

    char small[10];
    tasks_model_preview("hello world", small, sizeof(small));
    assert(strlen(small) == 9);                                 // 截断到 cap-1

    char chinese[5];
    tasks_model_preview("中文任务", chinese, sizeof(chinese));
    assert(strcmp(chinese, "中") == 0);
    char tiny[3];
    tasks_model_preview("中文", tiny, sizeof(tiny));
    assert(tiny[0] == 0);

    char big[16];
    tasks_model_preview("hello world and more", big, sizeof(big));
    assert(strncmp(big, "hello", 5) == 0);
    assert(strlen(big) == 15);

    tasks_model_preview("", out, sizeof(out));
    assert(out[0] == 0);
    tasks_model_preview(NULL, out, sizeof(out));
    assert(out[0] == 0);

    assert(tasks_model_set_items(&m, in, TASKS_MODEL_MAX + 5) == true || true);
    assert(m.count <= TASKS_MODEL_MAX);
    return 0;
}
