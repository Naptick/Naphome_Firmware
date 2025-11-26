#pragma once

#include <stdbool.h>
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

// Callback for streaming SSE events
typedef void (*openai_stream_callback_t)(const char *text, bool is_done, void *ctx);

// Callback for realtime transcription events
typedef void (*openai_realtime_transcript_cb_t)(const char *text, bool is_final, void *ctx);
typedef void (*openai_realtime_error_cb_t)(esp_err_t error, void *ctx);

esp_err_t openai_transcribe_wav(const int16_t *pcm_samples, size_t sample_count, int sample_rate_hz, openai_transcription_t *result);
esp_err_t openai_tts_generate(const char *text, const char *voice, uint8_t *out_wav, size_t max_out, size_t *bytes_written);
esp_err_t openai_generate_text_response(const char *prompt, char *out_text, size_t out_len);

// Stream audio to OpenAI with SSE responses
esp_err_t openai_stream_audio(const int16_t *pcm_samples, size_t sample_count, int sample_rate_hz,
                              openai_stream_callback_t callback, void *callback_ctx);

// Realtime API: Stream AFE-processed audio to OpenAI via WebSocket
typedef struct openai_realtime_stream *openai_realtime_handle_t;

openai_realtime_handle_t openai_realtime_start(int sample_rate_hz,
                                                openai_realtime_transcript_cb_t transcript_cb,
                                                openai_realtime_error_cb_t error_cb,
                                                void *cb_ctx);
esp_err_t openai_realtime_send_audio(openai_realtime_handle_t handle, const int16_t *pcm_samples, size_t sample_count);
esp_err_t openai_realtime_stop(openai_realtime_handle_t handle);

#ifdef __cplusplus
}
#endif
