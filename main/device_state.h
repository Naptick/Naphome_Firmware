#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Set device state context (called from main to provide access to global state)
// led_handle should be cast from led_controller_t*
void device_state_set_context(void *led_handle, bool lights_enabled, bool aws_connected, bool muted, bool audio_playing);

// Generate JSON string of current device state
// Returns allocated string (caller must free) or NULL on error
char *device_state_to_json(void);

// Parse and execute Gemini function call
// Returns response text to send back to Gemini
esp_err_t gemini_execute_function_call(const char *function_name, const char *arguments_json, char *response_text, size_t response_len);

#ifdef __cplusplus
}
#endif
