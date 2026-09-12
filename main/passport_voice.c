#include "passport_voice.h"
#include "passport_ble.h"
#include "esp_opus_enc.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "passport_voice";

// Xiaozhi's 16 kHz framing with complexity 0. Forty milliseconds halves the
// indication rate versus 20 ms while reducing each codec call's PCM.
// At 16 kbit/s every Opus packet fits in the 120-byte BLE payload bound.
#define VOICE_FRAME_SAMPLES 640
#define VOICE_MAX_PACKET_PAYLOAD 120

static esp_err_t map_encoder_error(esp_audio_err_t result)
{
    switch (result) {
    case ESP_AUDIO_ERR_MEM_LACK: return ESP_ERR_NO_MEM;
    case ESP_AUDIO_ERR_INVALID_PARAMETER: return ESP_ERR_INVALID_ARG;
    case ESP_AUDIO_ERR_NOT_SUPPORT: return ESP_ERR_NOT_SUPPORTED;
    default: return ESP_FAIL;
    }
}

struct passport_voice {
    void *encoder;
    uint32_t token;
    uint16_t sequence;
    size_t used;
    int16_t pcm[VOICE_FRAME_SAMPLES];
    uint8_t encoded[1275];
};
static esp_err_t send_packet(passport_voice_t *v, uint8_t kind, const void *data, size_t size)
{
    uint8_t packet[200] = {kind};
    if (size > VOICE_MAX_PACKET_PAYLOAD || size > sizeof(packet) - 7) return ESP_ERR_INVALID_SIZE;
    for (int i = 0; i < 4; i++) packet[1 + i] = v->token >> (i * 8);
    packet[5] = v->sequence; packet[6] = v->sequence >> 8;
    if (size) memcpy(packet + 7, data, size);
    return passport_ble_voice_send(packet, size + 7);
}
static esp_err_t encode_frame(passport_voice_t *v)
{
    esp_audio_enc_in_frame_t in = {.buffer = (uint8_t *)v->pcm, .len = sizeof(v->pcm)};
    esp_audio_enc_out_frame_t out = {.buffer = v->encoded, .len = sizeof(v->encoded)};
    esp_audio_err_t result = esp_opus_enc_process(v->encoder, &in, &out);
    if (result != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Opus encode failed: audio=%d free=%u largest=%u",
                 result, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return map_encoder_error(result);
    }
    if (!out.encoded_bytes || out.encoded_bytes > VOICE_MAX_PACKET_PAYLOAD) return ESP_ERR_INVALID_SIZE;
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
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_40_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_VOIP, .complexity = 0,
        .enable_fec = false, .enable_dtx = false, .enable_vbr = false,
    };
    esp_audio_err_t result = esp_opus_enc_open(&cfg, sizeof(cfg), &v->encoder);
    if (result != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Opus open failed: audio=%d free=%u largest=%u",
                 result, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        free(v); return map_encoder_error(result);
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
        size_t n = samples < VOICE_FRAME_SAMPLES - v->used ? samples : VOICE_FRAME_SAMPLES - v->used;
        memcpy(v->pcm + v->used, pcm, n * 2); v->used += n; pcm += n; samples -= n;
        if (v->used == VOICE_FRAME_SAMPLES) { esp_err_t err = encode_frame(v); if (err != ESP_OK) return err; }
    }
    return ESP_OK;
}
esp_err_t passport_voice_finish(passport_voice_t *v)
{
    // Flush a partial frame, then a full silent frame so the encoder's
    // lookahead cannot truncate the user's final syllable.
    memset(v->pcm + v->used, 0, (VOICE_FRAME_SAMPLES - v->used) * 2);
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
