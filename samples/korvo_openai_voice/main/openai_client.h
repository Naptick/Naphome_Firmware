#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPENAI_MAX_TRANSCRIPT_CHARS 512

typedef struct {
    char text[OPENAI_MAX_TRANSCRIPT_CHARS];
} openai_transcription_t;

esp_err_t openai_transcribe_wav(const int16_t *pcm_samples, size_t sample_count, int sample_rate_hz, openai_transcription_t *result);
esp_err_t openai_tts_generate(const char *text, const char *voice, uint8_t *out_wav, size_t max_out, size_t *bytes_written);

#ifdef __cplusplus
}
#endif
