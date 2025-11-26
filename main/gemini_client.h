#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GEMINI_MAX_TRANSCRIPT_CHARS 512

typedef struct {
    char text[GEMINI_MAX_TRANSCRIPT_CHARS];
} gemini_transcription_t;

// Callback for realtime transcription events
typedef void (*gemini_realtime_transcript_cb_t)(const char *text, bool is_final, void *ctx);
typedef void (*gemini_realtime_error_cb_t)(esp_err_t error, void *ctx);

// STT: Speech-to-Text using Google Speech-to-Text API
esp_err_t gemini_transcribe_wav(const int16_t *pcm_samples, size_t sample_count, int sample_rate_hz, gemini_transcription_t *result);

// LLM: Chat completions using Gemini API with function calling support
esp_err_t gemini_generate_text_response(const char *prompt, char *out_text, size_t out_len);
esp_err_t gemini_generate_text_response_with_tools(const char *prompt, const char *device_state_json, char *out_text, size_t out_len);

// TTS: Text-to-Speech using Google Text-to-Speech API
esp_err_t gemini_tts_generate(const char *text, const char *voice, uint8_t *out_wav, size_t max_out, size_t *bytes_written);

// Realtime API: Stream audio to Gemini via WebSocket (if supported)
// For now, we'll use batch STT, but this can be extended for streaming
typedef struct gemini_realtime_stream *gemini_realtime_handle_t;

gemini_realtime_handle_t gemini_realtime_start(int sample_rate_hz,
                                                gemini_realtime_transcript_cb_t transcript_cb,
                                                gemini_realtime_error_cb_t error_cb,
                                                void *cb_ctx);
esp_err_t gemini_realtime_send_audio(gemini_realtime_handle_t handle, const int16_t *pcm_samples, size_t sample_count);
esp_err_t gemini_realtime_stop(gemini_realtime_handle_t handle);

#ifdef __cplusplus
}
#endif
