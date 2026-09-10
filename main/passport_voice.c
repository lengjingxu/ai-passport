#include "passport_voice.h"
#include "passport_ble.h"
#include "esp_opus_enc.h"
#include "esp_random.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Xiaozhi's 16 kHz / 60 ms framing with complexity 0. Fixed 16 kbit/s
// bounds every Opus packet to 120 bytes, including during microphone noise.
struct passport_voice {
    void *encoder;
    uint32_t token;
    uint16_t sequence;
    size_t used;
    int16_t pcm[960];
    uint8_t encoded[1275];
};
static esp_err_t send_packet(passport_voice_t *v, uint8_t kind, const void *data, size_t size)
{
    uint8_t packet[200] = {kind};
    if (size > sizeof(packet) - 7) return ESP_ERR_INVALID_SIZE;
    for (int i = 0; i < 4; i++) packet[1 + i] = v->token >> (i * 8);
    packet[5] = v->sequence; packet[6] = v->sequence >> 8;
    if (size) memcpy(packet + 7, data, size);
    return passport_ble_voice_send(packet, size + 7);
}
static esp_err_t encode_frame(passport_voice_t *v)
{
    esp_audio_enc_in_frame_t in = {.buffer = (uint8_t *)v->pcm, .len = sizeof(v->pcm)};
    esp_audio_enc_out_frame_t out = {.buffer = v->encoded, .len = sizeof(v->encoded)};
    if (esp_opus_enc_process(v->encoder, &in, &out) != ESP_AUDIO_ERR_OK) return ESP_FAIL;
    if (!out.encoded_bytes || out.encoded_bytes > 120) return ESP_ERR_INVALID_SIZE;
    esp_err_t err = send_packet(v, 2, v->encoded, out.encoded_bytes);
    if (err == ESP_OK) { v->sequence++; v->used = 0; }
    return err;
}
esp_err_t passport_voice_open(const char *id, passport_voice_t **out)
{
    *out = NULL;
    if (!id || !id[0] || strlen(id) >= TASK_ID_LEN) return ESP_ERR_INVALID_ARG;
    passport_voice_t *v = calloc(1, sizeof(*v));
    if (!v) return ESP_ERR_NO_MEM;
    v->token = esp_random();
    esp_opus_enc_config_t cfg = {
        .sample_rate = 16000, .channel = 1, .bits_per_sample = 16, .bitrate = 16000,
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_VOIP, .complexity = 0,
        .enable_fec = false, .enable_dtx = false, .enable_vbr = false,
    };
    if (esp_opus_enc_open(&cfg, sizeof(cfg), &v->encoder) != ESP_AUDIO_ERR_OK) {
        free(v); return ESP_ERR_NO_MEM;
    }
    int input_size, output_size;
    if (esp_opus_enc_get_frame_size(v->encoder, &input_size, &output_size) != ESP_AUDIO_ERR_OK ||
        input_size != sizeof(v->pcm) || output_size <= 0 || output_size > sizeof(v->encoded)) {
        passport_voice_close(v, false); return ESP_ERR_INVALID_SIZE;
    }
    char task_id[TASK_ID_LEN] = {0}; strcpy(task_id, id);
    esp_err_t err = send_packet(v, 1, task_id, sizeof(task_id));
    if (err != ESP_OK) { passport_voice_close(v, false); return err; }
    *out = v; return ESP_OK;
}
esp_err_t passport_voice_write(passport_voice_t *v, const int16_t *pcm, size_t bytes)
{
    if (bytes % 2) return ESP_ERR_INVALID_ARG;
    size_t samples = bytes / 2;
    while (samples) {
        size_t n = samples < 960 - v->used ? samples : 960 - v->used;
        memcpy(v->pcm + v->used, pcm, n * 2); v->used += n; pcm += n; samples -= n;
        if (v->used == 960) { esp_err_t err = encode_frame(v); if (err != ESP_OK) return err; }
    }
    return ESP_OK;
}
esp_err_t passport_voice_finish(passport_voice_t *v)
{
    // Flush a partial frame, then a full silent frame so the encoder's
    // lookahead cannot truncate the user's final syllable.
    memset(v->pcm + v->used, 0, (960 - v->used) * 2);
    esp_err_t err = encode_frame(v);
    if (err != ESP_OK) return err;
    memset(v->pcm, 0, sizeof(v->pcm));
    err = encode_frame(v);
    return err == ESP_OK ? send_packet(v, 3, NULL, 0) : err;
}
void passport_voice_close(passport_voice_t *v, bool cancel)
{
    if (!v) return;
    if (cancel) (void)send_packet(v, 4, NULL, 0);
    esp_opus_enc_close(v->encoder); free(v);
}
