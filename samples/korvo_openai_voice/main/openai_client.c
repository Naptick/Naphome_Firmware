#include "openai_client.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_crypto_base64.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "openai_secrets.h"

static const char *TAG = "openai_client";
static const char *RESPONSES_URL = "https://api.openai.com/v1/responses";
static const char *TTS_URL = "https://api.openai.com/v1/audio/speech";

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

    esp_err_t err = ESP_OK;
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
        err = ESP_FAIL;
    }

cleanup:
    free(auth_value);
    esp_http_client_cleanup(client);
    return err;
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
    esp_err_t err = esp_crypto_base64_encode((unsigned char *)encoded, target_len, &written, input, input_len);
    if (err != ESP_OK) {
        free(encoded);
        return err;
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

static esp_err_t parse_transcription_text(const char *json, char *out_text, size_t out_len)
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
    err = parse_transcription_text((const char *)response.data, result->text, sizeof(result->text));
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
