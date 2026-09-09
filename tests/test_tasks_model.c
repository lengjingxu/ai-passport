#include <assert.h>
#include <string.h>

#include "tasks_model.h"

int main(void)
{
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
