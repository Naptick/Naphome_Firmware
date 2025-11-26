#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "korvo_audio.h"
#include "nvs_flash.h"
#include "openai_client.h"

static const char *TAG = "openai_demo";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retrying Wi-Fi connection...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_connect(void)
{
    ESP_RETURN_ON_FALSE(strlen(CONFIG_KORVO_OPENAI_WIFI_SSID) > 0, ESP_ERR_INVALID_STATE, TAG, "Set Wi-Fi SSID via menuconfig");
    s_wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_event_group, ESP_ERR_NO_MEM, TAG, "event group");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_KORVO_OPENAI_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_KORVO_OPENAI_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID=%s", CONFIG_KORVO_OPENAI_WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(20000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi connected");
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Wi-Fi connect failed");
    return ESP_FAIL;
}

static esp_err_t capture_audio_block(korvo_audio_t *audio, int16_t *buffer, size_t total_samples)
{
    size_t captured = 0;
    while (captured < total_samples) {
        size_t remaining = total_samples - captured;
        size_t chunk = remaining;
        if (chunk > 512) {
            chunk = 512;
        }
        size_t read = 0;
        ESP_RETURN_ON_ERROR(korvo_audio_capture(audio, buffer + captured, chunk, &read, pdMS_TO_TICKS(500)), TAG, "mic read");
        captured += read;
    }
    ESP_LOGI(TAG, "Captured %zu samples", captured);
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(wifi_connect());

    korvo_audio_t audio = {0};
    const int sample_rate = CONFIG_KORVO_OPENAI_SAMPLE_RATE;
    ESP_ERROR_CHECK(korvo_audio_init(&audio, sample_rate));

    const size_t total_samples = (sample_rate * CONFIG_KORVO_OPENAI_CAPTURE_MS) / 1000;
    int16_t *capture = malloc(total_samples * sizeof(int16_t));
    ESP_ERROR_CHECK_WITHOUT_ABORT(capture ? ESP_OK : ESP_ERR_NO_MEM);
    if (!capture) {
        korvo_audio_shutdown(&audio);
        return;
    }

    esp_err_t err = capture_audio_block(&audio, capture, total_samples);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Capture failed");
        korvo_audio_shutdown(&audio);
        free(capture);
        return;
    }

    openai_transcription_t transcript = {0};
    err = openai_transcribe_wav(capture, total_samples, sample_rate, &transcript);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Transcript: \"%s\"", transcript.text);
    } else {
        ESP_LOGE(TAG, "Transcription failed (%s)", esp_err_to_name(err));
    }

    const size_t tts_buffer_bytes = 96 * 1024;
    uint8_t *tts_buffer = malloc(tts_buffer_bytes);
    if (tts_buffer && err == ESP_OK && strlen(transcript.text) > 0) {
        size_t wav_bytes = 0;
        esp_err_t tts_err = openai_tts_generate(transcript.text,
                                                CONFIG_KORVO_OPENAI_TTS_VOICE,
                                                tts_buffer,
                                                tts_buffer_bytes,
                                                &wav_bytes);
        if (tts_err == ESP_OK) {
            ESP_LOGI(TAG, "Generated %d bytes of WAV audio from OpenAI", (int)wav_bytes);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, tts_buffer, wav_bytes > 64 ? 64 : wav_bytes, ESP_LOG_INFO);
        } else {
            ESP_LOGE(TAG, "TTS request failed (%s)", esp_err_to_name(tts_err));
        }
    } else if (!tts_buffer) {
        ESP_LOGE(TAG, "Unable to allocate TTS buffer");
    }

    free(tts_buffer);
    free(capture);
    korvo_audio_shutdown(&audio);
    ESP_LOGI(TAG, "Demo complete. Reset to run again.");
    vTaskDelay(portMAX_DELAY);
}
