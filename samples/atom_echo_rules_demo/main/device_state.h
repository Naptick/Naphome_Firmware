#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void device_state_init(void);
void device_state_set_wifi(bool connected, const char *ssid);
void device_state_set_aws(bool connected);
void device_state_set_spotify(bool ready);
void device_state_set_gemini(bool ready, const char *summary);
void device_state_set_i2c_summary(const char *summary_json);
void device_state_set_context(void *led_handle,
                              bool lights_enabled,
                              bool aws_connected,
                              bool muted,
                              bool audio_playing);

char *device_state_to_json(void);  // Caller must free()
esp_err_t gemini_execute_function_call(const char *function_name,
                                       const char *arguments_json,
                                       char *response_text,
                                       size_t response_len);

#ifdef __cplusplus
}
#endif
