#pragma once

#include "esp_err.h"
#include "korvo1.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    korvo1_t mic;
    int sample_rate_hz;
} korvo_audio_t;

esp_err_t korvo_audio_init(korvo_audio_t *ctx, int sample_rate_hz);
esp_err_t korvo_audio_capture(korvo_audio_t *ctx, int16_t *buffer, size_t samples_requested, size_t *samples_read, TickType_t timeout_ticks);
void korvo_audio_shutdown(korvo_audio_t *ctx);

#ifdef __cplusplus
}
#endif
