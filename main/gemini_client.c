#include "gemini_client.h"
#include "device_state.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include "esp_wifi.h"
#include "spotify_player.h"

#ifdef GEMINI_ENABLED
#include "gemini_secrets.h"
#else
#define GEMINI_API_KEY_STRING ""
#endif

static const char *TAG = "gemini_client";

// Google API endpoints
static const char *SPEECH_TO_TEXT_URL = "https://speech.googleapis.com/v1/speech:recognize";
static const char *GEMINI_API_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent";
static const char *TEXT_TO_SPEECH_URL = "https://texttospeech.googleapis.com/v1/text:synthesize";

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} http_buffer_t;

static esp_err_t http_buffer_append(http_buffer_t *buf, const uint8_t *chunk, size_t chunk_len)
{
    if (!chunk || chunk_len == 0) {
        return ESP_OK;
    }
    if (buf->len + chunk_len > buf->cap) {
        size_t new_cap = buf->cap ? buf->cap * 2 : 4096;
        while (new_cap < buf->len + chunk_len) {
            new_cap *= 2;
        }
        uint8_t *new_mem = realloc(buf->data, new_cap);
        ESP_RETURN_ON_FALSE(new_mem, ESP_ERR_NO_MEM, TAG, "alloc");
        buf->data = new_mem;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, chunk, chunk_len);
    buf->len += chunk_len;
    return ESP_OK;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (!evt || !evt->user_data) {
        return ESP_FAIL;
    }
    http_buffer_t *buf = (http_buffer_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        return http_buffer_append(buf, (const uint8_t *)evt->data, evt->data_len);
    }
    return ESP_OK;
}

static esp_err_t build_wav_from_pcm(const int16_t *pcm, size_t sample_count, int sample_rate_hz, uint8_t **out_buf, size_t *out_len)
{
    ESP_RETURN_ON_FALSE(pcm && out_buf && out_len, ESP_ERR_INVALID_ARG, TAG, "bad args");
    const uint16_t num_channels = 1;
    const uint16_t bits_per_sample = 16;
    size_t data_bytes = sample_count * sizeof(int16_t);
    size_t total_bytes = 44 + data_bytes;
    uint8_t *wav = malloc(total_bytes);
    ESP_RETURN_ON_FALSE(wav, ESP_ERR_NO_MEM, TAG, "wav alloc");

    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4);
    uint32_t chunk_size = total_bytes - 8;
    memcpy(header + 4, &chunk_size, 4);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    uint32_t subchunk1_size = 16;
    memcpy(header + 16, &subchunk1_size, 4);
    uint16_t audio_format = 1;
    memcpy(header + 20, &audio_format, 2);
    memcpy(header + 22, &num_channels, 2);
    uint32_t sample_rate = sample_rate_hz;
    memcpy(header + 24, &sample_rate, 4);
    uint32_t byte_rate = sample_rate * num_channels * bits_per_sample / 8;
    memcpy(header + 28, &byte_rate, 4);
    uint16_t block_align = num_channels * bits_per_sample / 8;
    memcpy(header + 32, &block_align, 2);
    memcpy(header + 34, &bits_per_sample, 2);
    memcpy(header + 36, "data", 4);
    uint32_t subchunk2_size = data_bytes;
    memcpy(header + 40, &subchunk2_size, 4);

    memcpy(wav, header, sizeof(header));
    memcpy(wav + sizeof(header), pcm, data_bytes);

    *out_buf = wav;
    *out_len = total_bytes;
    return ESP_OK;
}

static esp_err_t base64_encode_alloc(const uint8_t *input, size_t input_len, char **out_str)
{
    ESP_RETURN_ON_FALSE(input && out_str, ESP_ERR_INVALID_ARG, TAG, "bad args");
    size_t target_len = ((input_len + 2) / 3) * 4 + 1;
    char *encoded = malloc(target_len);
    ESP_RETURN_ON_FALSE(encoded, ESP_ERR_NO_MEM, TAG, "b64 alloc");
    size_t written = 0;
    int ret = mbedtls_base64_encode((unsigned char *)encoded, target_len, &written, input, input_len);
    if (ret != 0) {
        free(encoded);
        return ESP_FAIL;
    }
    encoded[written] = '\0';
    *out_str = encoded;
    return ESP_OK;
}

// STT: Speech-to-Text using Google Speech-to-Text API
esp_err_t gemini_transcribe_wav(const int16_t *pcm_samples, size_t sample_count, int sample_rate_hz, gemini_transcription_t *result)
{
    ESP_RETURN_ON_FALSE(pcm_samples && sample_count && result, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(sample_rate_hz > 0, ESP_ERR_INVALID_ARG, TAG, "sample_rate_hz must be > 0");
    
    float duration_sec = sample_rate_hz > 0 ? (float)sample_count / sample_rate_hz : 0.0f;
    ESP_LOGI(TAG, "🔊 [Gemini STT] Starting transcription: %zu samples @ %d Hz (%.2f sec)", 
             sample_count, sample_rate_hz, duration_sec);
    
    uint8_t *wav = NULL;
    size_t wav_len = 0;
    ESP_RETURN_ON_ERROR(build_wav_from_pcm(pcm_samples, sample_count, sample_rate_hz, &wav, &wav_len), TAG, "wav");
    ESP_LOGD(TAG, "🔊 [Gemini STT] WAV created: %zu bytes", wav_len);
    
    char *b64_audio = NULL;
    ESP_RETURN_ON_ERROR(base64_encode_alloc(wav, wav_len, &b64_audio), TAG, "b64");
    ESP_LOGD(TAG, "🔊 [Gemini STT] Base64 encoded: %zu chars", strlen(b64_audio));
    free(wav);
    
    // Build JSON request for Google Speech-to-Text API
    cJSON *root = cJSON_CreateObject();
    cJSON *config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "encoding", "LINEAR16");
    cJSON_AddNumberToObject(config, "sampleRateHertz", sample_rate_hz);
    cJSON_AddStringToObject(config, "languageCode", "en-US");
    cJSON_AddItemToObject(root, "config", config);
    
    cJSON *audio = cJSON_CreateObject();
    cJSON_AddStringToObject(audio, "content", b64_audio);
    cJSON_AddItemToObject(root, "audio", audio);
    
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(b64_audio);
    ESP_RETURN_ON_FALSE(payload, ESP_ERR_NO_MEM, TAG, "payload");
    
    // Build URL with API key
    char url[512];
    snprintf(url, sizeof(url), "%s?key=%s", SPEECH_TO_TEXT_URL, GEMINI_API_KEY_STRING);
    ESP_LOGD(TAG, "🔊 [Gemini STT] Request URL: %s", SPEECH_TO_TEXT_URL);
    ESP_LOGD(TAG, "🔊 [Gemini STT] Payload size: %zu bytes", strlen(payload));
    
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
    };
    
    http_buffer_t response = {0};
    cfg.user_data = &response;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(client, ESP_ERR_NO_MEM, TAG, "http client");
    
    ESP_LOGI(TAG, "📤 [Gemini STT] Sending audio via HTTP POST: %zu bytes payload (%.2f sec audio)", 
             strlen(payload), duration_sec);
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(esp_http_client_set_method(client, HTTP_METHOD_POST), cleanup, TAG, "method");
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Content-Type", "application/json"), cleanup, TAG, "ct hdr");
    ESP_GOTO_ON_ERROR(esp_http_client_set_post_field(client, payload, strlen(payload)), cleanup, TAG, "set body");
    
    int64_t start_time = esp_timer_get_time();
    ESP_LOGI(TAG, "📤 [Gemini STT] HTTP POST in progress...");
    ESP_GOTO_ON_ERROR(esp_http_client_perform(client), cleanup, TAG, "perform");
    int64_t elapsed_us = esp_timer_get_time() - start_time;
    
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "🔊 [Gemini STT] HTTP response: %d (took %lld ms)", status, elapsed_us / 1000);
    if (status / 100 != 2) {
        ESP_LOGE(TAG, "❌ [Gemini STT] HTTP error %d", status);
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    response.data = realloc(response.data, response.len + 1);
    if (!response.data) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    response.data[response.len] = '\0';
    
    // Parse response: {"results":[{"alternatives":[{"transcript":"text"}]}]}
    ESP_LOGD(TAG, "🔊 [Gemini STT] Response size: %zu bytes", response.len);
    cJSON *json = cJSON_Parse((const char *)response.data);
    if (!json) {
        ESP_LOGE(TAG, "❌ [Gemini STT] Failed to parse JSON response");
        ESP_LOGE(TAG, "❌ [Gemini STT] Response: %.*s", (int)response.len < 500 ? (int)response.len : 500, response.data);
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    cJSON *results = cJSON_GetObjectItem(json, "results");
    if (cJSON_IsArray(results) && cJSON_GetArraySize(results) > 0) {
        cJSON *first_result = cJSON_GetArrayItem(results, 0);
        cJSON *alternatives = cJSON_GetObjectItem(first_result, "alternatives");
        if (cJSON_IsArray(alternatives) && cJSON_GetArraySize(alternatives) > 0) {
            cJSON *first_alt = cJSON_GetArrayItem(alternatives, 0);
            cJSON *transcript = cJSON_GetObjectItem(first_alt, "transcript");
            if (cJSON_IsString(transcript)) {
                strncpy(result->text, transcript->valuestring, sizeof(result->text) - 1);
                result->text[sizeof(result->text) - 1] = '\0';
                ESP_LOGI(TAG, "✅ [Gemini STT] Success: \"%s\"", result->text);
                ret = ESP_OK;
            } else {
                ESP_LOGE(TAG, "❌ [Gemini STT] No transcript string in response");
                ret = ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "❌ [Gemini STT] No alternatives in response");
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "❌ [Gemini STT] No results in response");
        ret = ESP_FAIL;
    }
    
    cJSON_Delete(json);
    
cleanup:
    if (response.data) free(response.data);
    if (payload) free(payload);
    if (client) esp_http_client_cleanup(client);
    return ret;
}

// Build Gemini function calling tools definition
static cJSON *build_gemini_tools(void)
{
    cJSON *tools = cJSON_CreateArray();
    cJSON *tool = cJSON_CreateObject();
    cJSON *function_declarations = cJSON_CreateArray();
    
    // get_device_state
    cJSON *func1 = cJSON_CreateObject();
    cJSON_AddStringToObject(func1, "name", "get_device_state");
    cJSON_AddStringToObject(func1, "description", "Get complete device state including WiFi, LEDs, audio, AWS, Spotify, sensors, and health");
    cJSON_AddItemToArray(function_declarations, func1);
    
    // get_health
    cJSON *func2 = cJSON_CreateObject();
    cJSON_AddStringToObject(func2, "name", "get_health");
    cJSON_AddStringToObject(func2, "description", "Get device health status including memory usage");
    cJSON_AddItemToArray(function_declarations, func2);
    
    // get_temperature
    cJSON *func3 = cJSON_CreateObject();
    cJSON_AddStringToObject(func3, "name", "get_temperature");
    cJSON_AddStringToObject(func3, "description", "Get current temperature reading from sensors");
    cJSON_AddItemToArray(function_declarations, func3);
    
    // get_sensors
    cJSON *func4 = cJSON_CreateObject();
    cJSON_AddStringToObject(func4, "name", "get_sensors");
    cJSON_AddStringToObject(func4, "description", "Get all sensor readings (temperature, humidity, VOC, CO2, ambient light, proximity)");
    cJSON_AddItemToArray(function_declarations, func4);
    
    // set_leds
    cJSON *func5 = cJSON_CreateObject();
    cJSON_AddStringToObject(func5, "name", "set_leds");
    cJSON_AddStringToObject(func5, "description", "Turn LEDs on or off. Arguments: {\"enabled\": true/false}");
    cJSON *params5 = cJSON_CreateObject();
    cJSON_AddStringToObject(params5, "type", "object");
    cJSON *props5 = cJSON_CreateObject();
    cJSON *enabled5 = cJSON_CreateObject();
    cJSON_AddStringToObject(enabled5, "type", "boolean");
    cJSON_AddStringToObject(enabled5, "description", "true to turn on, false to turn off");
    cJSON_AddItemToObject(props5, "enabled", enabled5);
    cJSON_AddItemToObject(params5, "properties", props5);
    cJSON_AddItemToObject(func5, "parameters", params5);
    cJSON_AddItemToArray(function_declarations, func5);
    
    // set_led_color
    cJSON *func6 = cJSON_CreateObject();
    cJSON_AddStringToObject(func6, "name", "set_led_color");
    cJSON_AddStringToObject(func6, "description", "Set LED color. Arguments: {\"red\": 0-255, \"green\": 0-255, \"blue\": 0-255}");
    cJSON *params6 = cJSON_CreateObject();
    cJSON_AddStringToObject(params6, "type", "object");
    cJSON *props6 = cJSON_CreateObject();
    cJSON *red6 = cJSON_CreateObject();
    cJSON_AddStringToObject(red6, "type", "number");
    cJSON_AddNumberToObject(red6, "minimum", 0);
    cJSON_AddNumberToObject(red6, "maximum", 255);
    cJSON_AddItemToObject(props6, "red", red6);
    cJSON *green6 = cJSON_CreateObject();
    cJSON_AddStringToObject(green6, "type", "number");
    cJSON_AddNumberToObject(green6, "minimum", 0);
    cJSON_AddNumberToObject(green6, "maximum", 255);
    cJSON_AddItemToObject(props6, "green", green6);
    cJSON *blue6 = cJSON_CreateObject();
    cJSON_AddStringToObject(blue6, "type", "number");
    cJSON_AddNumberToObject(blue6, "minimum", 0);
    cJSON_AddNumberToObject(blue6, "maximum", 255);
    cJSON_AddItemToObject(props6, "blue", blue6);
    cJSON_AddItemToObject(params6, "properties", props6);
    cJSON_AddItemToObject(func6, "parameters", params6);
    cJSON_AddItemToArray(function_declarations, func6);
    
    // set_audio_mute
    cJSON *func7 = cJSON_CreateObject();
    cJSON_AddStringToObject(func7, "name", "set_audio_mute");
    cJSON_AddStringToObject(func7, "description", "Mute or unmute audio. Arguments: {\"muted\": true/false}");
    cJSON *params7 = cJSON_CreateObject();
    cJSON_AddStringToObject(params7, "type", "object");
    cJSON *props7 = cJSON_CreateObject();
    cJSON *muted7 = cJSON_CreateObject();
    cJSON_AddStringToObject(muted7, "type", "boolean");
    cJSON_AddStringToObject(muted7, "description", "true to mute, false to unmute");
    cJSON_AddItemToObject(props7, "muted", muted7);
    cJSON_AddItemToObject(params7, "properties", props7);
    cJSON_AddItemToObject(func7, "parameters", params7);
    cJSON_AddItemToArray(function_declarations, func7);
    
    cJSON_AddItemToObject(tool, "functionDeclarations", function_declarations);
    cJSON_AddItemToArray(tools, tool);
    return tools;
}

// LLM: Chat completions using Gemini API
esp_err_t gemini_generate_text_response(const char *prompt, char *out_text, size_t out_len)
{
    return gemini_generate_text_response_with_tools(prompt, NULL, out_text, out_len);
}

// LLM: Chat completions with function calling support
esp_err_t gemini_generate_text_response_with_tools(const char *prompt, const char *device_state_json, char *out_text, size_t out_len)
{
    ESP_RETURN_ON_FALSE(prompt && out_text && out_len > 0, ESP_ERR_INVALID_ARG, TAG, "bad args");
    
    ESP_LOGI(TAG, "💬 [Gemini LLM] Generating response for: \"%.100s%s\"", prompt, strlen(prompt) > 100 ? "..." : "");
    
    // Build enhanced prompt with device state
    char enhanced_prompt[4096] = {0};
    if (device_state_json) {
        snprintf(enhanced_prompt, sizeof(enhanced_prompt),
                "You are Naphome, a voice assistant for a smart home device. Here is the current device state:\n\n%s\n\n"
                "User query: %s\n\n"
                "You have access to function calling tools to control the device and query its status:\n"
                "- get_device_state: Get complete device state (WiFi, LEDs, audio, AWS, Spotify, sensors, health)\n"
                "- get_health: Get device health status including memory usage and sensor counts\n"
                "- get_temperature: Get current temperature and humidity from sensors\n"
                "- get_sensors: Get all sensor readings (temperature, humidity, VOC, CO2, ambient light, proximity, PM2.5)\n"
                "- set_leds: Turn LEDs on or off (arguments: {\"enabled\": true/false})\n"
                "- set_led_color: Set LED color (arguments: {\"red\": 0-255, \"green\": 0-255, \"blue\": 0-255})\n"
                "- set_audio_mute: Mute or unmute audio (arguments: {\"muted\": true/false})\n\n"
                "When the user asks about device status, health, or sensors, use the appropriate function to get current data. "
                "When the user wants to control lights or audio, use the control functions. Always provide natural, conversational responses.",
                device_state_json, prompt);
    } else {
        snprintf(enhanced_prompt, sizeof(enhanced_prompt), "%s", prompt);
    }
    
    // Build JSON request for Gemini API
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_CreateArray();
    cJSON *content = cJSON_CreateObject();
    cJSON *parts = cJSON_CreateArray();
    cJSON *part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "text", enhanced_prompt);
    cJSON_AddItemToArray(parts, part);
    cJSON_AddItemToObject(content, "parts", parts);
    cJSON_AddItemToArray(contents, content);
    cJSON_AddItemToObject(root, "contents", contents);
    
    // Add function calling tools
    if (device_state_json) {
        cJSON *tools = build_gemini_tools();
        cJSON_AddItemToObject(root, "tools", tools);
        ESP_LOGI(TAG, "💬 [Gemini LLM] Function calling enabled with %d tools", cJSON_GetArraySize(cJSON_GetObjectItem(cJSON_GetArrayItem(tools, 0), "functionDeclarations")));
    }
    
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ESP_RETURN_ON_FALSE(payload, ESP_ERR_NO_MEM, TAG, "payload");
    
    // Build URL with API key
    char url[512];
    snprintf(url, sizeof(url), "%s?key=%s", GEMINI_API_URL, GEMINI_API_KEY_STRING);
    ESP_LOGD(TAG, "💬 [Gemini LLM] Request URL: %s", GEMINI_API_URL);
    ESP_LOGD(TAG, "💬 [Gemini LLM] Payload size: %zu bytes", strlen(payload));
    
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
    };
    
    http_buffer_t response = {0};
    cfg.user_data = &response;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(client, ESP_ERR_NO_MEM, TAG, "http client");
    
    ESP_LOGI(TAG, "💬 [Gemini LLM] Sending HTTP POST request...");
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(esp_http_client_set_method(client, HTTP_METHOD_POST), cleanup, TAG, "method");
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Content-Type", "application/json"), cleanup, TAG, "ct hdr");
    ESP_GOTO_ON_ERROR(esp_http_client_set_post_field(client, payload, strlen(payload)), cleanup, TAG, "set body");
    
    int64_t start_time = esp_timer_get_time();
    ESP_GOTO_ON_ERROR(esp_http_client_perform(client), cleanup, TAG, "perform");
    int64_t elapsed_us = esp_timer_get_time() - start_time;
    
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "💬 [Gemini LLM] HTTP response: %d (took %lld ms)", status, elapsed_us / 1000);
    if (status / 100 != 2) {
        ESP_LOGE(TAG, "❌ [Gemini LLM] HTTP error %d", status);
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    response.data = realloc(response.data, response.len + 1);
    if (!response.data) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    response.data[response.len] = '\0';
    
    // Parse response: {"candidates":[{"content":{"parts":[{"text":"response"}]}]}]}
    ESP_LOGD(TAG, "💬 [Gemini LLM] Response size: %zu bytes", response.len);
    cJSON *json = cJSON_Parse((const char *)response.data);
    if (!json) {
        ESP_LOGE(TAG, "❌ [Gemini LLM] Failed to parse JSON response");
        ESP_LOGE(TAG, "❌ [Gemini LLM] Response: %.*s", (int)response.len < 500 ? (int)response.len : 500, response.data);
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    cJSON *candidates = cJSON_GetObjectItem(json, "candidates");
    if (cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
        cJSON *first_candidate = cJSON_GetArrayItem(candidates, 0);
        cJSON *content = cJSON_GetObjectItem(first_candidate, "content");
        if (content) {
            cJSON *parts = cJSON_GetObjectItem(content, "parts");
            if (cJSON_IsArray(parts) && cJSON_GetArraySize(parts) > 0) {
                cJSON *first_part = cJSON_GetArrayItem(parts, 0);
                
                // Check for function call
                cJSON *function_call = cJSON_GetObjectItem(first_part, "functionCall");
                if (function_call) {
                    cJSON *name = cJSON_GetObjectItem(function_call, "name");
                    cJSON *args = cJSON_GetObjectItem(function_call, "args");
                    if (cJSON_IsString(name) && cJSON_IsObject(args)) {
                        const char *func_name = name->valuestring;
                        char *args_str = cJSON_PrintUnformatted(args);
                        ESP_LOGI(TAG, "🔧 [Gemini LLM] Function call detected: %s(%s)", func_name, args_str);
                        
                        // Execute function call
                        char func_response[512] = {0};
                        esp_err_t func_ret = gemini_execute_function_call(func_name, args_str, func_response, sizeof(func_response));
                        free(args_str);
                        
                        if (func_ret == ESP_OK) {
                            // Call Gemini again with function result
                            // Use original prompt (before device state enhancement) for user query context
                            char followup_prompt[1024];
                            snprintf(followup_prompt, sizeof(followup_prompt),
                                    "Function %s returned: %s\n\nProvide a natural language response to the user's original query: %s",
                                    func_name, func_response, prompt);
                            
                            // Recursive call (limit depth to prevent infinite loops)
                            // Note: Use NULL for device_state_json in followup to avoid redundant state
                            static int call_depth = 0;
                            if (call_depth < 2) {
                                call_depth++;
                                ret = gemini_generate_text_response_with_tools(followup_prompt, NULL, out_text, out_len);
                                call_depth--;
                            } else {
                                // Fallback: use function result as response to prevent infinite recursion
                                snprintf(out_text, out_len, "Function %s completed: %s", func_name, func_response);
                                ret = ESP_OK;
                            }
                        } else {
                            snprintf(out_text, out_len, "Error executing function %s", func_name);
                            ret = ESP_FAIL;
                        }
                    } else {
                        ESP_LOGE(TAG, "❌ [Gemini LLM] Invalid function call format");
                        ret = ESP_FAIL;
                    }
                } else {
                    // Regular text response
                    cJSON *text = cJSON_GetObjectItem(first_part, "text");
                    if (cJSON_IsString(text)) {
                        strncpy(out_text, text->valuestring, out_len - 1);
                        out_text[out_len - 1] = '\0';
                        ESP_LOGI(TAG, "✅ [Gemini LLM] Success: \"%.200s%s\"", out_text, strlen(out_text) > 200 ? "..." : "");
                        ret = ESP_OK;
                    } else {
                        ESP_LOGE(TAG, "❌ [Gemini LLM] No text string in response");
                        ret = ESP_FAIL;
                    }
                }
            } else {
                ESP_LOGE(TAG, "❌ [Gemini LLM] No parts in response");
                ret = ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "❌ [Gemini LLM] No content in response");
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "❌ [Gemini LLM] No candidates in response");
        ret = ESP_FAIL;
    }
    
    cJSON_Delete(json);
    
cleanup:
    if (response.data) free(response.data);
    if (payload) free(payload);
    if (client) esp_http_client_cleanup(client);
    return ret;
}

// TTS: Text-to-Speech using Google Text-to-Speech API
esp_err_t gemini_tts_generate(const char *text, const char *voice, uint8_t *out_wav, size_t max_out, size_t *bytes_written)
{
    ESP_RETURN_ON_FALSE(text && out_wav && max_out > 0, ESP_ERR_INVALID_ARG, TAG, "bad args");
    
    ESP_LOGI(TAG, "🔊 [Gemini TTS] Generating speech: \"%.100s%s\" (voice: %s)", 
             text, strlen(text) > 100 ? "..." : "", voice && voice[0] ? voice : "default");
    
    // Build JSON request for Google TTS API
    cJSON *root = cJSON_CreateObject();
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "text", text);
    cJSON_AddItemToObject(root, "input", input);
    
    cJSON *voice_config = cJSON_CreateObject();
    // Use provided voice or default to en-US-Standard-D
    cJSON_AddStringToObject(voice_config, "languageCode", "en-US");
    cJSON_AddStringToObject(voice_config, "name", voice && voice[0] ? voice : "en-US-Standard-D");
    cJSON_AddItemToObject(root, "voice", voice_config);
    
    cJSON *audio_config = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_config, "audioEncoding", "LINEAR16");
    cJSON_AddNumberToObject(audio_config, "sampleRateHertz", 24000); // Google TTS default
    cJSON_AddItemToObject(root, "audioConfig", audio_config);
    
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ESP_RETURN_ON_FALSE(payload, ESP_ERR_NO_MEM, TAG, "payload");
    
    // Build URL with API key
    char url[512];
    snprintf(url, sizeof(url), "%s?key=%s", TEXT_TO_SPEECH_URL, GEMINI_API_KEY_STRING);
    ESP_LOGD(TAG, "🔊 [Gemini TTS] Request URL: %s", TEXT_TO_SPEECH_URL);
    ESP_LOGD(TAG, "🔊 [Gemini TTS] Payload size: %zu bytes", strlen(payload));
    
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
    };
    
    http_buffer_t response = {0};
    cfg.user_data = &response;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(client, ESP_ERR_NO_MEM, TAG, "http client");
    
    ESP_LOGI(TAG, "🔊 [Gemini TTS] Sending HTTP POST request...");
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(esp_http_client_set_method(client, HTTP_METHOD_POST), cleanup, TAG, "method");
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Content-Type", "application/json"), cleanup, TAG, "ct hdr");
    ESP_GOTO_ON_ERROR(esp_http_client_set_post_field(client, payload, strlen(payload)), cleanup, TAG, "set body");
    
    int64_t start_time = esp_timer_get_time();
    ESP_GOTO_ON_ERROR(esp_http_client_perform(client), cleanup, TAG, "perform");
    int64_t elapsed_us = esp_timer_get_time() - start_time;
    
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "🔊 [Gemini TTS] HTTP response: %d (took %lld ms)", status, elapsed_us / 1000);
    if (status / 100 != 2) {
        ESP_LOGE(TAG, "❌ [Gemini TTS] HTTP error %d", status);
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    response.data = realloc(response.data, response.len + 1);
    if (!response.data) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    response.data[response.len] = '\0';
    
    // Parse response: {"audioContent": "base64_encoded_wav"}
    cJSON *json = cJSON_Parse((const char *)response.data);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse TTS response");
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    cJSON *audio_content = cJSON_GetObjectItem(json, "audioContent");
    if (cJSON_IsString(audio_content)) {
        // Decode base64 audio
        size_t b64_len = strlen(audio_content->valuestring);
        size_t decoded_len = (b64_len * 3) / 4;
        if (decoded_len > max_out) {
            ESP_LOGE(TAG, "TTS audio too large: %zu > %zu", decoded_len, max_out);
            cJSON_Delete(json);
            ret = ESP_ERR_NO_MEM;
            goto cleanup;
        }
        
        int decode_ret = mbedtls_base64_decode(out_wav, max_out, &decoded_len, 
                                                (const unsigned char *)audio_content->valuestring, b64_len);
        if (decode_ret != 0) {
            ESP_LOGE(TAG, "Base64 decode failed: %d", decode_ret);
            cJSON_Delete(json);
            ret = ESP_FAIL;
            goto cleanup;
        }
        
        if (bytes_written) {
            *bytes_written = decoded_len;
        }
        ESP_LOGI(TAG, "✅ [Gemini TTS] Success: %zu bytes audio generated", decoded_len);
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "❌ [Gemini TTS] No audioContent in response");
        ret = ESP_FAIL;
    }
    
    cJSON_Delete(json);
    
cleanup:
    if (response.data) free(response.data);
    if (payload) free(payload);
    if (client) esp_http_client_cleanup(client);
    return ret;
}

// Realtime API: Placeholder for future streaming support
// For now, we'll use batch STT
struct gemini_realtime_stream {
    int sample_rate_hz;
    gemini_realtime_transcript_cb_t transcript_cb;
    gemini_realtime_error_cb_t error_cb;
    void *cb_ctx;
    bool stop_requested;
};

gemini_realtime_handle_t gemini_realtime_start(int sample_rate_hz,
                                                gemini_realtime_transcript_cb_t transcript_cb,
                                                gemini_realtime_error_cb_t error_cb,
                                                void *cb_ctx)
{
    ESP_RETURN_ON_FALSE(sample_rate_hz > 0 && sample_rate_hz <= 48000 && transcript_cb, NULL, TAG, "bad args");
    ESP_LOGI(TAG, "🎙️ [Gemini Live] Starting realtime stream @ %d Hz", sample_rate_hz);
    ESP_LOGI(TAG, "🎙️ [Gemini Live] NOTE: Currently using batch STT mode (realtime API placeholder)");
    
    struct gemini_realtime_stream *stream = calloc(1, sizeof(struct gemini_realtime_stream));
    if (!stream) {
        ESP_LOGE(TAG, "❌ [Gemini Live] Failed to allocate stream");
        return NULL;
    }
    stream->sample_rate_hz = sample_rate_hz;
    stream->transcript_cb = transcript_cb;
    stream->error_cb = error_cb;
    stream->cb_ctx = cb_ctx;
    stream->stop_requested = false;
    ESP_LOGI(TAG, "✅ [Gemini Live] Stream initialized");
    return (gemini_realtime_handle_t)stream;
}

esp_err_t gemini_realtime_send_audio(gemini_realtime_handle_t handle, const int16_t *pcm_samples, size_t sample_count)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    struct gemini_realtime_stream *stream = (struct gemini_realtime_stream *)handle;
    
    // For now, batch process audio chunks
    // In the future, this could buffer and send to streaming API
    float duration_sec = stream->sample_rate_hz > 0 ? (float)sample_count / stream->sample_rate_hz : 0.0f;
    ESP_LOGD(TAG, "🎙️ [Gemini Live] Audio chunk: %zu samples @ %d Hz (%.2f sec, batch mode)", 
             sample_count, stream->sample_rate_hz, duration_sec);
    return ESP_OK;
}

esp_err_t gemini_realtime_stop(gemini_realtime_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "🎙️ [Gemini Live] Stopping realtime stream");
    struct gemini_realtime_stream *stream = (struct gemini_realtime_stream *)handle;
    stream->stop_requested = true;
    free(stream);
    ESP_LOGI(TAG, "✅ [Gemini Live] Stream stopped");
    return ESP_OK;
}
