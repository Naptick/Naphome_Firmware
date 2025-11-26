#include "gemini_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "gemini_service";

static bool s_ready;
static char s_assistant_name[32];
static char s_last_summary[256];

esp_err_t gemini_service_init(const gemini_service_config_t *cfg)
{
    const char *name = (cfg && cfg->assistant_name && cfg->assistant_name[0]) ? cfg->assistant_name : "Naphome";
    strlcpy(s_assistant_name, name, sizeof(s_assistant_name));
    s_last_summary[0] = '\0';
    s_ready = true;

    ESP_LOGI(TAG,
             "Gemini demo initialised for assistant \"%s\". "
             "Replace gemini_service_run_demo() with real API calls when ready.",
             s_assistant_name);
    return ESP_OK;
}

bool gemini_service_is_ready(void)
{
    return s_ready;
}

const char *gemini_service_last_summary(void)
{
    return s_last_summary;
}

esp_err_t gemini_service_run_demo(const char *spoken_prompt, char *out_text, size_t out_text_len)
{
    if (!s_ready || !out_text || out_text_len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *prompt = (spoken_prompt && spoken_prompt[0]) ? spoken_prompt : "status update";

    ESP_LOGI(TAG, "🎙️ [Gemini] Simulating STT for \"%s\"", prompt);
    ESP_LOGI(TAG, "🤖 [Gemini] Simulating LLM reasoning");
    snprintf(out_text, out_text_len,
             "Hi! This is %s. I heard you say \"%s\". Sensors look good and the room is comfortable.",
             s_assistant_name,
             prompt);

    ESP_LOGI(TAG, "🔉 [Gemini] Simulating TTS playback with %zu byte response", strlen(out_text));

    snprintf(s_last_summary, sizeof(s_last_summary), "Responded to \"%s\" with canned status update.", prompt);
    ESP_LOGD(TAG, "Gemini summary: %s", s_last_summary);
    return ESP_OK;
}
