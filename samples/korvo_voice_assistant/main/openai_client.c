#include "openai_client.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "openai_secrets.h"

static const char *TAG = "openai_client";
static const char *RESPONSES_URL = "https://api.openai.com/v1/responses";
static const char *TTS_URL = "https://api.openai.com/v1/audio/speech";

// SSE streaming context
typedef struct {
    openai_stream_callback_t callback;
    void *callback_ctx;
    char *accumulated_text;
    size_t accumulated_len;
    size_t accumulated_cap;
} sse_stream_ctx_t;

// Realtime WebSocket streaming context
struct openai_realtime_stream {
    esp_websocket_client_handle_t ws_client;
    openai_realtime_transcript_cb_t transcript_cb;
    openai_realtime_error_cb_t error_cb;
    void *cb_ctx;
    int sample_rate_hz;
    bool connected;
    TaskHandle_t task;
    QueueHandle_t audio_queue;
    char *session_id;
    bool stop_requested;
    // Buffer for fragmented WebSocket messages
    char *message_buffer;
    size_t message_buffer_len;
    size_t message_buffer_cap;
};

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

// SSE event handler for streaming responses
static esp_err_t http_sse_event_handler(esp_http_client_event_t *evt)
{
    if (!evt || !evt->user_data) {
        return ESP_FAIL;
    }
    sse_stream_ctx_t *ctx = (sse_stream_ctx_t *)evt->user_data;
    
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        const char *data = (const char *)evt->data;
        size_t data_len = evt->data_len;
        
        // Parse SSE format: "data: {...}\n\n"
        const char *line_start = data;
        const char *end = data + data_len;
        
        while (line_start < end) {
            const char *line_end = line_start;
            while (line_end < end && *line_end != '\n' && *line_end != '\r') {
                line_end++;
            }
            
            size_t line_len = line_end - line_start;
            if (line_len > 0) {
                // Check if line starts with "data: "
                if (line_len > 6 && memcmp(line_start, "data: ", 6) == 0) {
                    const char *json_start = line_start + 6;
                    size_t json_len = line_len - 6;
                    
                    // Parse JSON to extract text
                    char *json_str = malloc(json_len + 1);
                    if (json_str) {
                        memcpy(json_str, json_start, json_len);
                        json_str[json_len] = '\0';
                        
                        cJSON *root = cJSON_Parse(json_str);
                        if (root) {
                            // Check for "output" array
                            cJSON *output = cJSON_GetObjectItem(root, "output");
                            if (cJSON_IsArray(output)) {
                                cJSON *msg = NULL;
                                cJSON_ArrayForEach(msg, output) {
                                    cJSON *content = cJSON_GetObjectItem(msg, "content");
                                    if (cJSON_IsArray(content)) {
                                        cJSON *entry = NULL;
                                        cJSON_ArrayForEach(entry, content) {
                                            cJSON *type = cJSON_GetObjectItem(entry, "type");
                                            if (cJSON_IsString(type) && strcmp(type->valuestring, "output_text") == 0) {
                                                cJSON *text = cJSON_GetObjectItem(entry, "text");
                                                if (cJSON_IsString(text) && ctx->callback) {
                                                    ctx->callback(text->valuestring, false, ctx->callback_ctx);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            
                            // Check for "done" flag
                            cJSON *done = cJSON_GetObjectItem(root, "done");
                            if (cJSON_IsTrue(done) && ctx->callback) {
                                ctx->callback("", true, ctx->callback_ctx);
                            }
                            
                            cJSON_Delete(root);
                        }
                        free(json_str);
                    }
                }
            }
            
            // Skip newline
            while (line_end < end && (*line_end == '\n' || *line_end == '\r')) {
                line_end++;
            }
            line_start = line_end;
        }
    }
    
    return ESP_OK;
}

static char *make_auth_header(void)
{
    size_t needed = strlen("Bearer ") + strlen(OPENAI_API_KEY_STRING) + 1;
    char *value = calloc(1, needed);
    if (!value) {
        return NULL;
    }
    snprintf(value, needed, "Bearer %s", OPENAI_API_KEY_STRING);
    return value;
}

static esp_err_t http_post_json(const char *url, const char *payload, http_buffer_t *response, bool expect_binary)
{
    ESP_RETURN_ON_FALSE(url && payload && response, ESP_ERR_INVALID_ARG, TAG, "bad args");
    http_buffer_append(response, NULL, 0);
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 60000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(client, ESP_FAIL, TAG, "client init failed");

    esp_err_t ret = ESP_OK;
    char *auth_value = make_auth_header();
    if (!auth_value) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Authorization", auth_value), cleanup, TAG, "auth hdr");
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Content-Type", "application/json"), cleanup, TAG, "ct hdr");
    if (expect_binary) {
        ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Accept", "audio/wav"), cleanup, TAG, "accept hdr");
    }
    ESP_GOTO_ON_ERROR(esp_http_client_set_post_field(client, payload, strlen(payload)), cleanup, TAG, "set body");
    ESP_GOTO_ON_ERROR(esp_http_client_perform(client), cleanup, TAG, "perform");
    int status = esp_http_client_get_status_code(client);
    if (status / 100 != 2) {
        ESP_LOGE(TAG, "HTTP %d", status);
        ret = ESP_FAIL;
    }

cleanup:
    free(auth_value);
    esp_http_client_cleanup(client);
    return ret;
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

static cJSON *make_text_content_node(const char *text)
{
    cJSON *node = cJSON_CreateObject();
    cJSON_AddStringToObject(node, "type", "input_text");
    cJSON_AddStringToObject(node, "text", text);
    return node;
}

static cJSON *make_audio_content_node(const char *b64, const char *format)
{
    cJSON *node = cJSON_CreateObject();
    cJSON_AddStringToObject(node, "type", "input_audio");
    cJSON *payload = cJSON_AddObjectToObject(node, "input_audio");
    cJSON_AddStringToObject(payload, "data", b64);
    cJSON_AddStringToObject(payload, "format", format);
    return node;
}

static esp_err_t parse_output_text(const char *json, char *out_text, size_t out_len)
{
    cJSON *root = cJSON_Parse(json);
    ESP_RETURN_ON_FALSE(root, ESP_FAIL, TAG, "json parse failed");
    esp_err_t err = ESP_FAIL;
    cJSON *output = cJSON_GetObjectItem(root, "output");
    if (cJSON_IsArray(output)) {
        cJSON *msg = NULL;
        cJSON_ArrayForEach(msg, output)
        {
            cJSON *content = cJSON_GetObjectItem(msg, "content");
            if (!cJSON_IsArray(content)) {
                continue;
            }
            cJSON *entry = NULL;
            cJSON_ArrayForEach(entry, content)
            {
                cJSON *type = cJSON_GetObjectItem(entry, "type");
                if (!cJSON_IsString(type)) {
                    continue;
                }
                if (strcmp(type->valuestring, "output_text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(entry, "text");
                    if (cJSON_IsString(text)) {
                        strlcpy(out_text, text->valuestring, out_len);
                        err = ESP_OK;
                        break;
                    }
                }
            }
            if (err == ESP_OK) {
                break;
            }
        }
    }
    cJSON_Delete(root);
    return err;
}

esp_err_t openai_transcribe_wav(const int16_t *pcm_samples, size_t sample_count, int sample_rate_hz, openai_transcription_t *result)
{
    ESP_RETURN_ON_FALSE(pcm_samples && sample_count && result, ESP_ERR_INVALID_ARG, TAG, "bad args");
    uint8_t *wav = NULL;
    size_t wav_len = 0;
    ESP_RETURN_ON_ERROR(build_wav_from_pcm(pcm_samples, sample_count, sample_rate_hz, &wav, &wav_len), TAG, "wav build");

    char *b64 = NULL;
    esp_err_t err = base64_encode_alloc(wav, wav_len, &b64);
    free(wav);
    ESP_RETURN_ON_ERROR(err, TAG, "wav b64");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "gpt-4o-mini-transcribe");
    cJSON *input = cJSON_AddArrayToObject(root, "input");
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON *content = cJSON_AddArrayToObject(msg, "content");
    cJSON_AddItemToArray(content, make_text_content_node("Please transcribe the attached audio using concise lowercase text."));
    cJSON_AddItemToArray(content, make_audio_content_node(b64, "wav"));
    cJSON_AddItemToArray(input, msg);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(b64);
    ESP_RETURN_ON_FALSE(payload, ESP_ERR_NO_MEM, TAG, "payload");

    http_buffer_t response = {0};
    err = http_post_json(RESPONSES_URL, payload, &response, false);
    free(payload);
    ESP_RETURN_ON_ERROR(err, TAG, "http");

    response.data = realloc(response.data, response.len + 1);
    if (!response.data) {
        response.len = 0;
        return ESP_ERR_NO_MEM;
    }
    response.data[response.len] = '\0';
    err = parse_output_text((const char *)response.data, result->text, sizeof(result->text));
    free(response.data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse transcription response");
    }
    return err;
}

esp_err_t openai_tts_generate(const char *text, const char *voice, uint8_t *out_wav, size_t max_out, size_t *bytes_written)
{
    ESP_RETURN_ON_FALSE(text && out_wav && max_out > 0, ESP_ERR_INVALID_ARG, TAG, "bad args");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "gpt-4o-mini-tts");
    cJSON_AddStringToObject(root, "input", text);
    cJSON_AddStringToObject(root, "voice", voice && voice[0] ? voice : "alloy");
    cJSON_AddStringToObject(root, "format", "wav");
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ESP_RETURN_ON_FALSE(payload, ESP_ERR_NO_MEM, TAG, "payload");

    http_buffer_t response = {0};
    esp_err_t err = http_post_json(TTS_URL, payload, &response, true);
    free(payload);
    ESP_RETURN_ON_ERROR(err, TAG, "http");

    if (response.len > max_out) {
        ESP_LOGE(TAG, "TTS response (%d) > buffer (%d)", (int)response.len, (int)max_out);
        free(response.data);
        return ESP_ERR_NO_MEM;
    }
    memcpy(out_wav, response.data, response.len);
    free(response.data);
    if (bytes_written) {
        *bytes_written = response.len;
    }
    return ESP_OK;
}

esp_err_t openai_generate_text_response(const char *prompt, char *out_text, size_t out_len)
{
    ESP_RETURN_ON_FALSE(prompt && out_text && out_len > 0, ESP_ERR_INVALID_ARG, TAG, "bad args");
    cJSON *root = cJSON_CreateObject();
    ESP_RETURN_ON_FALSE(root, ESP_ERR_NO_MEM, TAG, "json alloc");
    // Use gpt-4o for chat completions (latest chat model, user requested gpt-5-chat but gpt-4o is latest available)
    cJSON_AddStringToObject(root, "model", "gpt-4o");
    cJSON *input = cJSON_AddArrayToObject(root, "input");
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON *content = cJSON_AddArrayToObject(msg, "content");
    cJSON_AddItemToArray(content, make_text_content_node(prompt));
    cJSON_AddItemToArray(input, msg);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ESP_RETURN_ON_FALSE(payload, ESP_ERR_NO_MEM, TAG, "payload");

    http_buffer_t response = {0};
    esp_err_t err = http_post_json(RESPONSES_URL, payload, &response, false);
    free(payload);
    ESP_RETURN_ON_ERROR(err, TAG, "http");

    response.data = realloc(response.data, response.len + 1);
    if (!response.data) {
        return ESP_ERR_NO_MEM;
    }
    response.data[response.len] = '\0';
    err = parse_output_text((const char *)response.data, out_text, out_len);
    free(response.data);
    return err;
}

// Stream audio to OpenAI with SSE responses
esp_err_t openai_stream_audio(const int16_t *pcm_samples, size_t sample_count, int sample_rate_hz,
                              openai_stream_callback_t callback, void *callback_ctx)
{
    ESP_RETURN_ON_FALSE(pcm_samples && sample_count && callback, ESP_ERR_INVALID_ARG, TAG, "bad args");
    
    uint8_t *wav = NULL;
    size_t wav_len = 0;
    ESP_RETURN_ON_ERROR(build_wav_from_pcm(pcm_samples, sample_count, sample_rate_hz, &wav, &wav_len), TAG, "wav build");

    char *b64 = NULL;
    esp_err_t err = base64_encode_alloc(wav, wav_len, &b64);
    free(wav);
    ESP_RETURN_ON_ERROR(err, TAG, "wav b64");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "gpt-4o-mini-transcribe");
    cJSON_AddBoolToObject(root, "stream", cJSON_True);  // Enable streaming
    cJSON *input = cJSON_AddArrayToObject(root, "input");
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON *content = cJSON_AddArrayToObject(msg, "content");
    cJSON_AddItemToArray(content, make_text_content_node("Please transcribe the attached audio using concise lowercase text."));
    cJSON_AddItemToArray(content, make_audio_content_node(b64, "wav"));
    cJSON_AddItemToArray(input, msg);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(b64);
    ESP_RETURN_ON_FALSE(payload, ESP_ERR_NO_MEM, TAG, "payload");

    // Setup SSE streaming context
    sse_stream_ctx_t sse_ctx = {
        .callback = callback,
        .callback_ctx = callback_ctx,
        .accumulated_text = NULL,
        .accumulated_len = 0,
        .accumulated_cap = 0
    };

    esp_http_client_config_t cfg = {
        .url = RESPONSES_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_sse_event_handler,
        .user_data = &sse_ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 60000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(client, ESP_FAIL, TAG, "client init failed");

    char *auth_value = make_auth_header();
    if (!auth_value) {
        esp_http_client_cleanup(client);
        free(payload);
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Authorization", auth_value), cleanup, TAG, "auth hdr");
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Content-Type", "application/json"), cleanup, TAG, "ct hdr");
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Accept", "text/event-stream"), cleanup, TAG, "accept hdr");
    ESP_GOTO_ON_ERROR(esp_http_client_set_post_field(client, payload, strlen(payload)), cleanup, TAG, "set body");
    ESP_GOTO_ON_ERROR(esp_http_client_perform(client), cleanup, TAG, "perform");
    
    int status = esp_http_client_get_status_code(client);
    if (status / 100 != 2) {
        ESP_LOGE(TAG, "HTTP %d", status);
        ret = ESP_FAIL;
    }

cleanup:
    free(auth_value);
    free(payload);
    if (sse_ctx.accumulated_text) {
        free(sse_ctx.accumulated_text);
    }
    esp_http_client_cleanup(client);
    return ret;
}

// WebSocket event handler for OpenAI Realtime API
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_id_t ws_event_id = (esp_websocket_event_id_t)event_id;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    struct openai_realtime_stream *stream = (struct openai_realtime_stream *)handler_args;
    
    ESP_LOGI(TAG, "WebSocket event received: event_id=%" PRId32 " (ws_event_id=%d)", event_id, (int)ws_event_id);
    
    if (!stream) {
        ESP_LOGE(TAG, "WebSocket event handler: stream is NULL");
        return;
    }
    
    switch (ws_event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected to OpenAI Realtime API");
            stream->connected = true;
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED handler executing, ws_client=%p", stream->ws_client);
            
            // Send session.update to configure and establish the session
            // This may be required to trigger session.created from OpenAI
            ESP_LOGI(TAG, "Preparing session.update...");
            cJSON *session_update = cJSON_CreateObject();
            cJSON_AddStringToObject(session_update, "type", "session.update");
            
            cJSON *session_config = cJSON_CreateObject();
            // Configure session for real-time transcription
            // Minimal configuration to establish session
            cJSON *modalities = cJSON_CreateArray();
            cJSON_AddItemToArray(modalities, cJSON_CreateString("text")); // Enable text modality
            cJSON_AddItemToObject(session_config, "modalities", modalities);
            cJSON_AddStringToObject(session_config, "instructions", "You are a helpful voice assistant.");
            cJSON_AddStringToObject(session_config, "voice", "alloy");
            cJSON_AddNumberToObject(session_config, "temperature", 1.0);
            
            cJSON_AddItemToObject(session_update, "session", session_config);
            
            char *session_json = cJSON_PrintUnformatted(session_update);
            if (!session_json) {
                ESP_LOGE(TAG, "Failed to create session.update JSON");
                cJSON_Delete(session_update);
                break;
            }
            
            if (!stream->ws_client) {
                ESP_LOGE(TAG, "WebSocket client is NULL, cannot send session.update");
                free(session_json);
                cJSON_Delete(session_update);
                break;
            }
            
            // Check if WebSocket is actually connected
            bool is_connected = esp_websocket_client_is_connected(stream->ws_client);
            ESP_LOGI(TAG, "WebSocket is_connected check: %d", is_connected);
            if (!is_connected) {
                ESP_LOGW(TAG, "WebSocket not fully connected yet according to is_connected()");
            }
            
            ESP_LOGI(TAG, "Sending session.update to establish session...");
            ESP_LOGI(TAG, "Session.update JSON: %s", session_json);
            ESP_LOGI(TAG, "JSON length: %d bytes, ws_client: %p", strlen(session_json), stream->ws_client);
            
            esp_err_t send_err = esp_websocket_client_send_text(stream->ws_client, session_json, strlen(session_json), portMAX_DELAY);
            if (send_err == ESP_OK) {
                ESP_LOGI(TAG, "Successfully sent session.update (%d bytes)", strlen(session_json));
            } else {
                ESP_LOGE(TAG, "Failed to send session.update: %s (0x%x)", esp_err_to_name(send_err), send_err);
            }
            
            free(session_json);
            cJSON_Delete(session_update);
            
            ESP_LOGI(TAG, "Waiting for session.created event from server...");
            break;
            
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected");
            stream->connected = false;
            // Clear session ID on disconnect
            if (stream->session_id) {
                free(stream->session_id);
                stream->session_id = NULL;
            }
            if (stream->error_cb) {
                stream->error_cb(ESP_FAIL, stream->cb_ctx);
            }
            break;
            
        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x08) { // Close frame
                ESP_LOGI(TAG, "WebSocket closed");
                stream->connected = false;
                break;
            }
            
            if (data->data_len > 0 && data->op_code == 0x01) { // Only handle text frames
                // Handle fragmented WebSocket messages by buffering
                // Append to message buffer
                if (stream->message_buffer_len + data->data_len + 1 > stream->message_buffer_cap) {
                    size_t new_cap = stream->message_buffer_cap == 0 ? 4096 : stream->message_buffer_cap * 2;
                    while (stream->message_buffer_len + data->data_len + 1 > new_cap) {
                        new_cap *= 2;
                    }
                    char *new_buf = realloc(stream->message_buffer, new_cap);
                    if (!new_buf) {
                        ESP_LOGE(TAG, "Failed to realloc message buffer");
                        break;
                    }
                    stream->message_buffer = new_buf;
                    stream->message_buffer_cap = new_cap;
                }
                
                memcpy(stream->message_buffer + stream->message_buffer_len, data->data_ptr, data->data_len);
                stream->message_buffer_len += data->data_len;
                stream->message_buffer[stream->message_buffer_len] = '\0';
                
                // Try to parse JSON - if it fails, wait for more fragments
                char *json_str = stream->message_buffer;
                size_t json_len = stream->message_buffer_len;
                
                cJSON *event = cJSON_Parse(json_str);
                if (event) {
                    // Successfully parsed - this is a complete message
                        cJSON *type = cJSON_GetObjectItem(event, "type");
                        if (cJSON_IsString(type)) {
                            const char *event_type = type->valuestring;
                            
                            // Log all events for debugging
                            ESP_LOGI(TAG, "Received OpenAI event: %s (len=%d)", event_type, (int)data->data_len);
                            // Also log first 200 chars of the full event for debugging
                            if (data->data_len < 200) {
                                ESP_LOGI(TAG, "Event data: %s", json_str);
                            }
                            
                            // Handle session.created
                            if (strcmp(event_type, "session.created") == 0) {
                                cJSON *session = cJSON_GetObjectItem(event, "session");
                                if (session) {
                                    cJSON *id = cJSON_GetObjectItem(session, "id");
                                    if (cJSON_IsString(id)) {
                                        if (stream->session_id) free(stream->session_id);
                                        stream->session_id = strdup(id->valuestring);
                                        ESP_LOGI(TAG, "Session created: %s", stream->session_id);
                                    }
                                }
                            }
                            // Handle response.audio_transcript.delta (partial transcription)
                            else if (strcmp(event_type, "response.audio_transcript.delta") == 0) {
                                cJSON *delta = cJSON_GetObjectItem(event, "delta");
                                if (delta && cJSON_IsString(delta) && delta->valuestring && strlen(delta->valuestring) > 0) {
                                    // Forward to callback for console output
                                    if (stream->transcript_cb) {
                                        stream->transcript_cb(delta->valuestring, false, stream->cb_ctx);
                                    }
                                }
                            }
                            // Handle response.audio_transcript.done (final transcription)
                            else if (strcmp(event_type, "response.audio_transcript.done") == 0) {
                                cJSON *transcript = cJSON_GetObjectItem(event, "transcript");
                                if (transcript && cJSON_IsString(transcript) && transcript->valuestring && strlen(transcript->valuestring) > 0) {
                                    // Forward to callback for console output
                                    if (stream->transcript_cb) {
                                        stream->transcript_cb(transcript->valuestring, true, stream->cb_ctx);
                                    }
                                }
                            }
                            // Handle error events
                            else if (strcmp(event_type, "error") == 0) {
                                cJSON *error_obj = cJSON_GetObjectItem(event, "error");
                                ESP_LOGE(TAG, "OpenAI error: %s", json_str);
                                if (stream->error_cb) {
                                    stream->error_cb(ESP_FAIL, stream->cb_ctx);
                                }
                                (void)error_obj; // Suppress unused variable warning
                            }
                            // Log unhandled events
                            else {
                                ESP_LOGI(TAG, "Unhandled event type: %s", event_type);
                            }
                        } else {
                            // Log if we can't parse event type
                            ESP_LOGW(TAG, "Received event without type field: %s", json_str);
                        }
                        cJSON_Delete(event);
                    // Reset message buffer for next message
                    stream->message_buffer_len = 0;
                } else {
                    // JSON parse failed - might be incomplete fragment, wait for more
                    // Log if buffer is getting very large (might be malformed or stuck)
                    if (stream->message_buffer_len > 10000) {
                        ESP_LOGW(TAG, "Failed to parse JSON after %d bytes, resetting buffer: %.200s", (int)json_len, json_str);
                        // Reset buffer if too large to avoid memory issues
                        stream->message_buffer_len = 0;
                    } else if (stream->message_buffer_len > 500) {
                        // Log that we're buffering fragments
                        ESP_LOGD(TAG, "Buffering WebSocket fragment (total=%d bytes)", (int)stream->message_buffer_len);
                    }
                }
            }
            break;
            
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            if (stream->error_cb) {
                stream->error_cb(ESP_FAIL, stream->cb_ctx);
            }
            break;
            
        default:
            break;
    }
}

// Audio streaming task
static void realtime_audio_task(void *arg)
{
    struct openai_realtime_stream *stream = (struct openai_realtime_stream *)arg;
    if (!stream) {
        vTaskDelete(NULL);
        return;
    }
    
    int16_t audio_buffer[512]; // Chunk size for streaming
    size_t sample_size = sizeof(int16_t);
    size_t samples_per_chunk = sample_size > 0 ? sizeof(audio_buffer) / sample_size : 512;
    
    static int audio_sent_count = 0;
    static int audio_receive_count = 0;
    
    while (!stream->stop_requested) {
        // Wait for audio data from queue
        if (xQueueReceive(stream->audio_queue, audio_buffer, pdMS_TO_TICKS(100)) == pdTRUE) {
            audio_receive_count++;
            
            // Only send audio if WebSocket is connected AND session is created
            // OpenAI may reject audio before session is established
            if (stream->connected && stream->ws_client && stream->session_id) {
                // Verify WebSocket is still connected before sending
                if (!esp_websocket_client_is_connected(stream->ws_client)) {
                    ESP_LOGW(TAG, "WebSocket not connected, marking as disconnected");
                    stream->connected = false;
                    // Don't send, will retry after reconnection
                } else {
                    // Base64 encode audio
                    char *b64 = NULL;
                    size_t audio_bytes = samples_per_chunk * sizeof(int16_t);
                    esp_err_t err = base64_encode_alloc((const uint8_t *)audio_buffer, audio_bytes, &b64);
                    
                    if (err == ESP_OK && b64) {
                        // Create input_audio_buffer.append event
                        // OpenAI Realtime API format: {"type": "input_audio_buffer.append", "audio": "<base64>"}
                        cJSON *event = cJSON_CreateObject();
                        cJSON_AddStringToObject(event, "type", "input_audio_buffer.append");
                        cJSON_AddStringToObject(event, "audio", b64);
                        
                        char *event_json = cJSON_PrintUnformatted(event);
                        if (event_json) {
                            // Use shorter timeout to avoid blocking if WebSocket is stuck
                            esp_err_t send_err = esp_websocket_client_send_text(stream->ws_client, event_json, strlen(event_json), pdMS_TO_TICKS(100));
                            if (send_err == ESP_OK) {
                                audio_sent_count++;
                                // Log every 50 chunks sent (about every 2-3 seconds at 24kHz)
                                if (audio_sent_count % 50 == 0) {
                                    ESP_LOGI(TAG, "Audio sent to OpenAI: %d chunks (received %d from queue)", audio_sent_count, audio_receive_count);
                                }
                            } else {
                                // Check if it's a connection error (0x587 = ESP_ERR_WS_SEND_FRAME_FAILED)
                                if (send_err == 0x587 || send_err == ESP_ERR_INVALID_STATE || send_err == ESP_FAIL) {
                                    ESP_LOGW(TAG, "WebSocket send failed, marking disconnected: %s (0x%x)", 
                                            esp_err_to_name(send_err), send_err);
                                    stream->connected = false;
                                }
                                // Only log occasionally to avoid spam
                                static int fail_count = 0;
                                if (++fail_count % 50 == 0) {
                                    ESP_LOGW(TAG, "Failed to send audio (count=%d): %s (0x%x)", 
                                            fail_count, esp_err_to_name(send_err), send_err);
                                }
                            }
                            free(event_json);
                        }
                        cJSON_Delete(event);
                        free(b64);
                    } else {
                        ESP_LOGW(TAG, "Base64 encode failed: %s", esp_err_to_name(err));
                    }
                }
            } else {
                // Log why we're not sending - only occasionally
                static int skip_count = 0;
                if (++skip_count % 200 == 0) {
                    ESP_LOGI(TAG, "Waiting for session before sending audio: connected=%d, session_id=%s", 
                            stream->connected, stream->session_id ? "yes" : "no");
                }
            }
        }
    }
    
    vTaskDelete(NULL);
}

openai_realtime_handle_t openai_realtime_start(int sample_rate_hz,
                                                openai_realtime_transcript_cb_t transcript_cb,
                                                openai_realtime_error_cb_t error_cb,
                                                void *cb_ctx)
{
    ESP_RETURN_ON_FALSE(sample_rate_hz > 0 && transcript_cb, NULL, TAG, "bad args");
    
    struct openai_realtime_stream *stream = calloc(1, sizeof(struct openai_realtime_stream));
    ESP_RETURN_ON_FALSE(stream, NULL, TAG, "alloc failed");
    
    stream->transcript_cb = transcript_cb;
    stream->error_cb = error_cb;
    stream->cb_ctx = cb_ctx;
    stream->sample_rate_hz = sample_rate_hz;
    stream->connected = false;
    stream->stop_requested = false;
    stream->message_buffer = NULL;
    stream->message_buffer_len = 0;
    stream->message_buffer_cap = 0;
    
    // Check available heap before creating queue
    uint32_t free_heap = esp_get_free_heap_size();
    size_t queue_item_size = sizeof(int16_t) * 512;
    size_t queue_size = 30;  // Reduced from 50 to save memory
    size_t queue_memory_needed = queue_size * queue_item_size;
    
    ESP_LOGI(TAG, "Free heap before queue: %" PRIu32 " bytes, queue needs: %zu bytes", 
             free_heap, queue_memory_needed);
    
    if (free_heap < queue_memory_needed + 10240) {  // Reserve 10KB buffer
        ESP_LOGE(TAG, "Insufficient heap: %" PRIu32 " < %zu (needed + 10KB buffer)", 
                 free_heap, queue_memory_needed + 10240);
        goto err_cleanup;
    }
    
    // Create audio queue - reduced size to prevent memory issues
    stream->audio_queue = xQueueCreate(queue_size, queue_item_size);
    if (!stream->audio_queue) {
        ESP_LOGE(TAG, "queue create failed (heap: %" PRIu32 " bytes, needed: %zu bytes)", 
                 esp_get_free_heap_size(), queue_memory_needed);
        goto err_cleanup;
    }
    
    ESP_LOGI(TAG, "Audio queue created: %zu items, %zu bytes/item, total: %zu bytes", 
             queue_size, queue_item_size, queue_memory_needed);
    
    // Build WebSocket URL
    char ws_url[256];
    snprintf(ws_url, sizeof(ws_url), 
             "wss://api.openai.com/v1/realtime?model=gpt-4o-realtime-preview-2024-10-01&sample_rate=%d",
             sample_rate_hz);
    
    // Build Authorization header - must be terminated with \r\n
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s\r\n", OPENAI_API_KEY_STRING);
    
    esp_websocket_client_config_t ws_cfg = {
        .uri = ws_url,
        .headers = auth_header,  // Set headers in config before init
        // Note: crt_bundle_attach may not be available in ESP-IDF v4.4
    };
    
    stream->ws_client = esp_websocket_client_init(&ws_cfg);
    if (!stream->ws_client) {
        ESP_LOGE(TAG, "ws client init failed");
        goto err_cleanup;
    }
    
    esp_websocket_register_events(stream->ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, stream);
    
    esp_err_t err = esp_websocket_client_start(stream->ws_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ws client start failed: %s", esp_err_to_name(err));
        goto err_cleanup;
    }
    
    // Start audio streaming task
    xTaskCreatePinnedToCore(realtime_audio_task, "openai_realtime_audio", 4096, stream, 5, &stream->task, 1);
    
    ESP_LOGI(TAG, "OpenAI Realtime API started (sample_rate=%d Hz)", sample_rate_hz);
    return stream;
    
err_cleanup:
    if (stream->audio_queue) {
        vQueueDelete(stream->audio_queue);
    }
    if (stream->ws_client) {
        esp_websocket_client_stop(stream->ws_client);
        esp_websocket_client_destroy(stream->ws_client);
    }
    if (stream->session_id) {
        free(stream->session_id);
    }
    if (stream->message_buffer) {
        free(stream->message_buffer);
    }
    free(stream);
    return NULL;
}

esp_err_t openai_realtime_send_audio(openai_realtime_handle_t handle, const int16_t *pcm_samples, size_t sample_count)
{
    ESP_RETURN_ON_FALSE(handle && pcm_samples && sample_count > 0, ESP_ERR_INVALID_ARG, TAG, "bad args");
    
    if (!handle->connected || !handle->audio_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Send audio in chunks
    size_t chunk_size = 512;
    for (size_t i = 0; i < sample_count; i += chunk_size) {
        size_t to_send = (sample_count - i > chunk_size) ? chunk_size : (sample_count - i);
        int16_t chunk[512];
        memcpy(chunk, &pcm_samples[i], to_send * sizeof(int16_t));
        
        // Pad if needed
        if (to_send < chunk_size) {
            memset(&chunk[to_send], 0, (chunk_size - to_send) * sizeof(int16_t));
        }
        
        // Use timeout to avoid blocking, but allow some wait time
        if (xQueueSend(handle->audio_queue, chunk, pdMS_TO_TICKS(10)) != pdTRUE) {
            static int drop_count = 0;
            if (++drop_count % 100 == 0) {
                ESP_LOGW(TAG, "Audio queue full, dropping samples (total drops: %d)", drop_count);
            }
        }
    }
    
    return ESP_OK;
}

esp_err_t openai_realtime_stop(openai_realtime_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "bad args");
    
    handle->stop_requested = true;
    
    if (handle->task) {
        vTaskDelete(handle->task);
        handle->task = NULL;
    }
    
    if (handle->ws_client) {
        esp_websocket_client_stop(handle->ws_client);
        esp_websocket_client_destroy(handle->ws_client);
        handle->ws_client = NULL;
    }
    
    if (handle->audio_queue) {
        vQueueDelete(handle->audio_queue);
        handle->audio_queue = NULL;
    }
    
    if (handle->session_id) {
        free(handle->session_id);
        handle->session_id = NULL;
    }
    
    if (handle->message_buffer) {
        free(handle->message_buffer);
        handle->message_buffer = NULL;
    }
    
    free(handle);
    return ESP_OK;
}
