#pragma once

#include "aws_iot_bridge.h"
#include "intent_router.h"
#include "korvo_audio.h"
#include "led_controller.h"
#include "spotify_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct voice_pipeline *voice_pipeline_handle_t;

typedef struct {
    korvo_audio_t *audio;
    intent_router_t *router;
    spotify_client_t *spotify;
    aws_iot_bridge_t *aws_bridge;
    led_controller_t *leds;
    int sample_rate_hz;
    int capture_ms;
    const char *tts_voice;
    bool use_realtime_streaming;  // Use OpenAI Realtime API with continuous streaming
    bool skip_wake_word;           // Skip wake word detection, stream continuously
    bool enable_wakenet_local;     // Enable WakeNet in parallel for local control
    const char *wakenet_model;     // WakeNet model name (e.g., "wn9_hiesp")
    int wakenet_threshold;          // WakeNet detection threshold (0-100)
    bool use_gemini;                // Use Gemini AI instead of OpenAI for STT-LLM-TTS
} voice_pipeline_config_t;

// Callback for local wake word detection (parallel to OpenAI)
typedef void (*voice_pipeline_wake_callback_t)(const char *wake_word, int word_index, void *ctx);

voice_pipeline_handle_t voice_pipeline_create(const voice_pipeline_config_t *cfg);
esp_err_t voice_pipeline_start(voice_pipeline_handle_t handle);
void voice_pipeline_handle_wake(voice_pipeline_handle_t handle);
void voice_pipeline_handle_button(voice_pipeline_handle_t handle, int button_id);
void voice_pipeline_set_wake_callback(voice_pipeline_handle_t handle, voice_pipeline_wake_callback_t callback, void *ctx);

#ifdef __cplusplus
}
#endif
