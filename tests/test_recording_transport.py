"""Compile the production streaming writer against a short-write HTTP double."""
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/tasks_client.c').read_text()
writer = source[source.index('static esp_err_t write_all'):source.index('esp_err_t tasks_feedback_open')]
writer += source[source.index('esp_err_t tasks_feedback_write'):]
harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
typedef int esp_err_t;
typedef void *esp_http_client_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
static char wire[2048];
static int used, mode, status = 201, headers;
static int esp_http_client_write(void *client, const char *data, int bytes) {
    (void)client;
    if (mode) return mode == 1 ? 0 : -1;
    int n = bytes > 7 ? 7 : bytes;
    memcpy(wire + used, data, n);
    used += n;
    return n;
}
static int esp_http_client_fetch_headers(void *client) { (void)client; return headers; }
static int esp_http_client_get_status_code(void *client) { (void)client; return status; }
'''
checks = r'''
int main(void) {
    char pcm[512]; memset(pcm, 42, sizeof(pcm));
    assert(tasks_feedback_write(NULL, pcm, sizeof(pcm)) == ESP_OK);
    assert(used == 519 && !memcmp(wire, "200\r\n", 5));
    assert(!memcmp(wire + 5, pcm, 512));
    assert(!memcmp(wire + 517, "\r\n", 2));
    assert(tasks_feedback_finish(NULL) == ESP_OK);
    assert(!memcmp(wire + 519, "0\r\n\r\n", 5));
    for (mode = 1; mode <= 2; ++mode) {
        assert(tasks_feedback_write(NULL, pcm, 2) == ESP_FAIL);
        assert(tasks_feedback_finish(NULL) == ESP_FAIL);
    }
    mode = 0; status = 400;
    assert(tasks_feedback_finish(NULL) == ESP_FAIL);
    status = 201; headers = -1;
    assert(tasks_feedback_finish(NULL) == ESP_FAIL);
    assert(tasks_feedback_write(NULL, pcm, 513) == ESP_ERR_INVALID_ARG);
    assert(tasks_feedback_write(NULL, pcm, 1) == ESP_ERR_INVALID_ARG);
    assert(tasks_feedback_write(NULL, pcm, 0) == ESP_ERR_INVALID_ARG);
    puts("Recording transport: PASS (short writes, framing, failures, acknowledgement)");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'transport.c'
    exe = Path(tmp) / 'transport'
    c.write_text(harness + writer + checks)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
