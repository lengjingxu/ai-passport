#include "tasks_client.h"

#include "app_config_loader.h"
#include "tasks_model.h"

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "tasks_client";

static esp_err_t read_body(esp_http_client_handle_t client, char **out, int *out_len) {
    const int total = 8 * 1024;
    char *buf = malloc(total + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    int len = 0, n = 0;
    while (len < total && (n = esp_http_client_read(client, buf + len, total - len)) > 0) {
        len += n;
    }
    if (n < 0 || !esp_http_client_is_complete_data_received(client)) {
        free(buf);
        return ESP_ERR_INVALID_RESPONSE;
    }
    buf[len] = 0;
    *out = buf;
    *out_len = len;
    return ESP_OK;
}

esp_err_t tasks_client_fetch(task_item_t *out, int max, int *count) {
    *count = 0;
    char url[160];
    snprintf(url, sizeof(url), "%s/tasks", APP_BRIDGE_URL);

    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 3000 };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    char *body = NULL;
    int body_len = 0;
    esp_err_t err = esp_http_client_open(client, 0);
    int status = 0;
    if (err == ESP_OK) {
        if (esp_http_client_fetch_headers(client) < 0) err = ESP_FAIL;
        status = esp_http_client_get_status_code(client);
        if (err == ESP_OK && status >= 200 && status < 300) {
            err = read_body(client, &body, &body_len);
        } else if (err == ESP_OK) {
            err = ESP_FAIL;
        }
        esp_http_client_close(client);
    }
    if (err != ESP_OK && status >= 400) {
        ESP_LOGE(TAG, "GET /tasks HTTP %d", status);
    }
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    esp_http_client_cleanup(client);

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        ESP_LOGE(TAG, "invalid tasks JSON");
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *arr = cJSON_IsArray(root) ? root : cJSON_GetObjectItem(root, "tasks");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    int n = 0;
    cJSON *entry;
    cJSON_ArrayForEach(entry, arr) {
        if (n >= max) break;
        task_item_t *item = &out[n];
        memset(item, 0, sizeof(*item));
        const cJSON *f;
        if ((f = cJSON_GetObjectItem(entry, "id"))) {
            char *s = cJSON_GetStringValue(f);
            if (s) strncpy(item->id, s, TASK_ID_LEN - 1);
        }
        if ((f = cJSON_GetObjectItem(entry, "title"))) {
            char *s = cJSON_GetStringValue(f);
            if (s) strncpy(item->title, s, TASK_TITLE_LEN - 1);
        }
        if ((f = cJSON_GetObjectItem(entry, "status"))) {
            char *s = cJSON_GetStringValue(f);
            if (s) strncpy(item->status, s, TASK_STATUS_LEN - 1);
        }
        if ((f = cJSON_GetObjectItem(entry, "message"))) {
            char *s = cJSON_GetStringValue(f);
            if (s) strncpy(item->message, s, TASK_MESSAGE_LEN - 1);
        }
        if ((f = cJSON_GetObjectItem(entry, "updated_at"))) {
            item->updated_at = (int64_t)cJSON_GetNumberValue(f);
        }
        if (item->id[0] != 0) n++;
    }
    cJSON_Delete(root);
    *count = n;
    return ESP_OK;
}

static esp_err_t write_all(esp_http_client_handle_t client, const void *data, size_t bytes) {
    const char *p = data;
    while (bytes) {
        int n = esp_http_client_write(client, p, bytes);
        if (n <= 0) return ESP_FAIL;
        p += n;
        bytes -= n;
    }
    return ESP_OK;
}

esp_err_t tasks_feedback_open(const char *task_id, esp_http_client_handle_t *out) {
    // Percent-encode the UTF-8 identifier before putting it into the query.
    char id[TASK_ID_LEN * 3], *end = id;
    for (const unsigned char *p = (const unsigned char *)task_id; *p; ++p) {
        if (end + 3 >= id + sizeof(id)) return ESP_ERR_INVALID_ARG;
        end += sprintf(end, "%%%02X", *p);
    }
    *end = 0;
    char url[sizeof(id) + 192];
    snprintf(url, sizeof(url), "%s/feedback?task_id=%s&hz=16000&bits=16&ch=1", APP_BRIDGE_URL, id);
    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 2000,
                                    .buffer_size = 512, .buffer_size_tx = 512 };
    *out = esp_http_client_init(&cfg);
    if (!*out) return ESP_ERR_NO_MEM;
    esp_http_client_set_method(*out, HTTP_METHOD_POST);
    esp_http_client_set_header(*out, "Content-Type", "application/octet-stream");
    return esp_http_client_open(*out, -1);
}

esp_err_t tasks_feedback_write(esp_http_client_handle_t client, const void *pcm, size_t bytes) {
    if (!bytes || bytes > 512 || bytes % 2) return ESP_ERR_INVALID_ARG;
    char frame[520];
    int n = snprintf(frame, sizeof(frame), "%x\r\n", (unsigned)bytes);
    memcpy(frame + n, pcm, bytes);
    memcpy(frame + n + bytes, "\r\n", 2);
    return write_all(client, frame, n + bytes + 2);
}

esp_err_t tasks_feedback_finish(esp_http_client_handle_t client) {
    if (write_all(client, "0\r\n\r\n", 5) != ESP_OK ||
        esp_http_client_fetch_headers(client) < 0) return ESP_FAIL;
    // The bridge acknowledges only after the complete WAV has been saved.
    return esp_http_client_get_status_code(client) == 201 ? ESP_OK : ESP_FAIL;
}
