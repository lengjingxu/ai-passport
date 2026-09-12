"""Run the real recording controller with deterministic audio/RTOS doubles.

The microphone supplies an already-buffered burst. The lower-priority sender
can run only when the producer waits. This reproduces the zero-wait queue bug
without timing-sensitive host threads or pretending to validate the board.
"""
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/demo_tasks.c').read_text()
start = source.index('typedef struct {\n    size_t bytes;')
controller = source[start:source.index('static void worker_task', start)]
harness = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_TIMEOUT -3
#define MALLOC_CAP_8BIT 0
#define REC_HZ 16000
#define REC_BPS 32000
#define REC_MAX_BYTES (30 * REC_BPS)
#define TASK_ID_LEN 64
#define LV_ANIM_OFF 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY 99999
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef int esp_err_t;
typedef int wifi_ps_type_t;
#define WIFI_PS_NONE 0
static int wifi_ps = 1, fail_wifi, fail_open;
static int esp_wifi_get_ps(int *mode) {*mode=wifi_ps;return 0;}
static int esp_wifi_set_ps(int mode) {if (fail_wifi && mode==0) return ESP_FAIL;wifi_ps=mode;return 0;}
static int64_t esp_timer_get_time(void) {static int64_t now;return now+=16000;}

typedef void *esp_http_client_handle_t;
typedef struct { unsigned char items[8][520]; size_t size; int count; } queue_t;
typedef queue_t *QueueHandle_t;
typedef void *SemaphoreHandle_t;
static queue_t *queue;
static void *upload_arg;
static int allocations, closed, submitted, waits, reads, stop_after, stalled, cancel;
static size_t sent;
static int fail_voice_write;
static atomic_bool s_exit, s_recording, s_cancel_record;
static int home_returns;
#define VIEW_LIST 0
static void view_show(int view) {assert(view == VIEW_LIST); home_returns++;}
static atomic_bool s_waiting_review, s_waiting_send;
static char s_action_task[TASK_ID_LEN];
static uint32_t s_voice_token;
static esp_err_t s_action_error;
static bool s_ble_mode;
typedef void passport_voice_t;
static int passport_voice_open(const char *id, passport_voice_t **out) {(void)id;*out=(void *)1;return 0;}
static uint32_t passport_voice_token(const passport_voice_t *v) {(void)v;return 5;}
static int passport_voice_write(passport_voice_t *v, const int16_t *pcm, size_t n) {(void)v;(void)pcm;if (!fail_voice_write) sent += n;return fail_voice_write ? ESP_FAIL : 0;}
static int passport_voice_finish(passport_voice_t *v) {(void)v;submitted++;return 0;}
static void passport_voice_close(passport_voice_t *v, bool cancel) {(void)v;(void)cancel;closed++;}
enum { CMD_NONE, CMD_RECORD, CMD_STOP_SEND };
static int s_cmd, s_model, s_rec_hint, s_rec_bar, s_rec_sec;
static char s_line[96];
typedef struct { char id[64]; } task_item_t;
static task_item_t item = {"test"};
static const task_item_t *tasks_model_current(void *m) { (void)m; return &item; }
static int bsp_lvgl_lock(int ms) { (void)ms; return 1; }
static void bsp_lvgl_unlock(void) {}
static void record_show(void) {}
static void detail_show(void) {}
static void status_refresh(void) {}
static void lv_label_set_text(int obj, const char *s) { (void)obj; (void)s; }
static void lv_label_set_text_fmt(int obj, const char *s, ...) { (void)obj; (void)s; }
static void lv_bar_set_value(int obj, int value, int anim) { (void)obj; (void)value; (void)anim; }
static const char *esp_err_to_name(int err) { return err == ESP_ERR_TIMEOUT ? "ESP_ERR_TIMEOUT" : "ESP_OK"; }
static int bsp_audio_set_format(int hz, int bits, int ch) { assert(hz==16000 && bits==16 && ch==1); return 0; }
static int bsp_audio_read(void *pcm, size_t bytes) {
    memset(pcm, 42, bytes);
    if (++reads == stop_after) {
        if (cancel == 2) s_cancel_record = true;
        else if (cancel) s_exit = true;
        else s_cmd = CMD_STOP_SEND;
    }
    return ESP_OK;
}
static QueueHandle_t xQueueCreate(int count, size_t size) {
    assert(count == 8 && size <= 520);
    queue = calloc(1, sizeof(*queue)); queue->size = size; allocations++;
    return queue;
}
static SemaphoreHandle_t xSemaphoreCreateBinary(void) { allocations++; return (void *)1; }
static void drain_one(queue_t *q) {
    size_t bytes; memcpy(&bytes, q->items[0], sizeof(bytes));
    if (bytes) sent += bytes; else submitted++;
    --q->count;
    memmove(q->items, q->items + 1, q->count * sizeof(q->items[0]));
}
static int xQueueSend(queue_t *q, const void *chunk, int ticks) {
    if (q->count == 8 && ticks) {
        assert(ticks <= 20); waits++;
        if (!stalled) drain_one(q);
    }
    if (q->count == 8) return 0;
    memcpy(q->items[q->count++], chunk, q->size);
    return 1;
}
static int xQueueReceive(queue_t *q, void *chunk, int ticks) { (void)q;(void)chunk;(void)ticks; return 0; }
static int xTaskCreate(void (*fn)(void *), const char *name, int stack, void *arg, int priority, void *handle) {
    (void)fn;(void)name;(void)stack;(void)priority;(void)handle;
    upload_arg=arg;return pdPASS;
}
static void xSemaphoreGive(void *s) {(void)s;}
static void xSemaphoreTake(void *s, int ticks);
static void vTaskDelete(void *t) {(void)t;}
static void vQueueDelete(queue_t *q) {free(q);allocations--;}
static void vSemaphoreDelete(void *s) {(void)s;allocations--;}
static int tasks_feedback_open(const char *id, void **out) {(void)id;assert(wifi_ps == WIFI_PS_NONE);*out=(void *)1;return fail_open ? ESP_FAIL : 0;}
static int tasks_feedback_write(void *client, const void *pcm, size_t bytes) {(void)client;(void)pcm;(void)bytes;return 0;}
static int tasks_feedback_finish(void *client) {(void)client;return 0;}
static void esp_http_client_cleanup(void *client) {(void)client;closed++;}
'''
checks = r'''
static void xSemaphoreTake(void *s, int ticks) {
    (void)s; (void)ticks;
    record_upload_t *upload = upload_arg;
    if (!upload->abort && !s_exit) while (queue->count) drain_one(queue);
    if (upload->voice) passport_voice_close(upload->voice, upload->abort);
    else esp_http_client_cleanup(upload->client);
}
static void reset(int chunks, int blocked, int cancelled) {
    s_exit=false;s_recording=false;s_cancel_record=false;s_cmd=CMD_RECORD;home_returns=0;
    assert(wifi_ps == 1); fail_wifi=fail_open=0;
    closed=submitted=waits=reads=0;sent=0;stop_after=chunks;stalled=blocked;cancel=cancelled;fail_voice_write=0;
}
int main(void) {
    // Repeated recordings with a burst larger than the send queue.
    for (int run=0; run<3; ++run) {
        reset(12, 0, 0); do_record();
        printf("burst: %s, submitted=%d, bytes=%zu\n", s_line, submitted, sent); fflush(stdout);
        assert(submitted == 1 && sent == 12 * 512 && waits > 0);
        assert(!allocations && closed == 1 && !s_recording);
    }
    reset(12, 1, 0); do_record();
    assert(submitted == 0 && strstr(s_line, "ESP_ERR_TIMEOUT"));
    assert(!allocations && closed == 1);
    reset(12, 0, 1); do_record();
    assert(submitted == 0 && !allocations && closed == 1);
    reset(2000, 0, 0); do_record();
    assert(submitted == 1 && sent == REC_MAX_BYTES && !allocations);
    reset(12, 0, 0); fail_open=1; do_record();
    assert(wifi_ps == 1 && !allocations && closed == 1 && !submitted);
    reset(12, 0, 0); fail_wifi=1; do_record();
    assert(wifi_ps == 1 && !allocations && closed == 0 && !submitted);
    s_ble_mode=true;
    reset(12, 0, 0); do_record();
    assert(submitted == 1 && sent == 12 * 512 && !allocations && closed == 1);
    reset(12, 0, 1); do_record();
    assert(submitted == 0 && !allocations && closed == 1);
    reset(12, 0, 2); do_record();
    assert(submitted == 0 && !s_exit && home_returns == 1 && closed == 1);
    assert(!s_cancel_record && !allocations);
    reset(12, 0, 0); fail_voice_write=1; do_record();
    assert(submitted == 0 && !allocations && closed == 1);
    puts("Recording queue: PASS (bursts, congestion, cancellation, limit, repeated cleanup)");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'recording.c'
    exe = Path(tmp) / 'recording'
    c.write_text(harness + controller + checks)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
