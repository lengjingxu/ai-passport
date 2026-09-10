"""Compile the production streaming writer against a short-write HTTP double."""
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/tasks_client.c').read_text()
writer = source[source.index('static esp_err_t write_all'):]
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
#define ESP_ERR_NO_MEM -3
#define APP_BRIDGE_URL "http://bridge.invalid"
#define TASK_ID_LEN 64
#define HTTP_METHOD_POST 1
#define IPPROTO_TCP 6
#define TCP_NODELAY 1
#define ESP_LOGE(...) ((void)0)
typedef struct { const char *url; int timeout_ms, buffer_size, buffer_size_tx; } esp_http_client_config_t;
static int open_error, socket_error, socket_fd=7, no_delay;
static char last_url[512];
static void *esp_http_client_init(const esp_http_client_config_t *cfg) {
    snprintf(last_url, sizeof(last_url), "%s", cfg->url);return (void *)1;
}
static void esp_http_client_set_method(void *client, int method) {(void)client;assert(method==HTTP_METHOD_POST);}
static void esp_http_client_set_header(void *client, const char *key, const char *value) {(void)client;(void)key;(void)value;}
static int esp_http_client_open(void *client, int length) {(void)client;assert(length==-1);return open_error;}
static int esp_http_client_get_socket(void *client) {(void)client;return socket_fd;}
static int setsockopt(int fd, int level, int option, const void *value, size_t size) {
    assert(fd==7 && level==IPPROTO_TCP && option==TCP_NODELAY && size==sizeof(int));
    no_delay=*(const int *)value;return socket_error;
}
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
    void *client = NULL;
    assert(tasks_feedback_open("test &", &client) == ESP_OK && no_delay == 1);
    assert(strstr(last_url, "task_id=%74%65%73%74%20%26&hz=16000"));
    socket_error=-1;
    assert(tasks_feedback_open("test", &client) == ESP_FAIL);
    socket_error=0;socket_fd=-1;
    assert(tasks_feedback_open("test", &client) == ESP_FAIL);
    socket_fd=7;open_error=ESP_FAIL;no_delay=0;
    assert(tasks_feedback_open("test", &client) == ESP_FAIL && !no_delay);
    puts("Recording transport: PASS (short writes, framing, failures, acknowledgement)");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'transport.c'
    exe = Path(tmp) / 'transport'
    c.write_text(harness + writer + checks)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
