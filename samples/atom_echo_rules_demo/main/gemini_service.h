/**
 * @file gemini_service.h
 * @brief Minimal Gemini STT/LLM/TTS demo hooks for the Atom Echo sample.
 *
 * The production firmware talks to Google's Gemini APIs. This sample keeps
 * the same structure but returns canned responses so developers can focus on
 * wiring device logic before adding API keys.
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *assistant_name;
} gemini_service_config_t;

esp_err_t gemini_service_init(const gemini_service_config_t *cfg);
esp_err_t gemini_service_run_demo(const char *spoken_prompt, char *out_text, size_t out_text_len);
const char *gemini_service_last_summary(void);
bool gemini_service_is_ready(void);

#ifdef __cplusplus
}
#endif
