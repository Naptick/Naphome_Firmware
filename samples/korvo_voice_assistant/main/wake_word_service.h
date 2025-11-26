#pragma once

#include "aws_iot_bridge.h"
#include "esp_err.h"
#include "korvo_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wake_word_callback_t)(void *ctx);

typedef struct {
    korvo_audio_t *audio;
    aws_iot_bridge_t *aws_bridge;
    int sensitivity;
    int simulated_interval_ms;
    int frame_samples;
    int activation_frames;
    int cooldown_ms;
} wake_word_service_config_t;

typedef struct wake_word_service wake_word_service_t;

wake_word_service_t *wake_word_service_start(const wake_word_service_config_t *cfg,
                                             wake_word_callback_t cb,
                                             void *cb_ctx);
void wake_word_service_stop(wake_word_service_t *service);

#ifdef __cplusplus
}
#endif
