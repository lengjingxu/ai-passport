#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct passport_voice passport_voice_t;
esp_err_t passport_voice_open(const char *task_id, passport_voice_t **out);
esp_err_t passport_voice_write(passport_voice_t *voice, const int16_t *pcm, size_t bytes);
esp_err_t passport_voice_finish(passport_voice_t *voice);
void passport_voice_close(passport_voice_t *voice, bool cancel);
uint32_t passport_voice_token(const passport_voice_t *voice);
