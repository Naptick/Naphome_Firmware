#include "korvo_audio.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "korvo_audio";

static korvo1_config_t default_korvo1_pins(int sample_rate_hz)
{
    korvo1_config_t cfg = {
        .port = I2S_NUM_0,
        .din_io_num = GPIO_NUM_19,
        .bclk_io_num = GPIO_NUM_18,
        .ws_io_num = GPIO_NUM_17,
        .mclk_io_num = GPIO_NUM_0,
        .sample_rate_hz = sample_rate_hz,
        .dma_buffer_count = 4,
        .dma_buffer_len = 256,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // Changed to STEREO to support multiple microphone channels
    };
    return cfg;
}

esp_err_t korvo_audio_init(korvo_audio_t *ctx, int sample_rate_hz)
{
    ESP_RETURN_ON_FALSE(ctx, ESP_ERR_INVALID_ARG, TAG, "ctx required");
    korvo1_config_t cfg = default_korvo1_pins(sample_rate_hz);
    ESP_LOGI(TAG, "Initializing Korvo-1 microphone:");
    ESP_LOGI(TAG, "  I2S Port: %d", cfg.port);
    ESP_LOGI(TAG, "  Sample Rate: %d Hz", cfg.sample_rate_hz);
    ESP_LOGI(TAG, "  Pins: DIN=GPIO%d, BCLK=GPIO%d, WS=GPIO%d, MCLK=GPIO%d",
             cfg.din_io_num, cfg.bclk_io_num, cfg.ws_io_num, cfg.mclk_io_num);
    const char* channel_fmt_str = (cfg.channel_format == I2S_CHANNEL_FMT_ONLY_LEFT) ? "ONLY_LEFT" : 
                                  (cfg.channel_format == I2S_CHANNEL_FMT_ONLY_RIGHT) ? "ONLY_RIGHT" : "STEREO";
    ESP_LOGI(TAG, "  Channel Format: %s", channel_fmt_str);
    ESP_LOGI(TAG, "  DMA: %d buffers x %d samples", cfg.dma_buffer_count, cfg.dma_buffer_len);
    ESP_RETURN_ON_ERROR(korvo1_init(&ctx->mic, &cfg), TAG, "init failed");
    esp_err_t start_err = korvo1_start(&ctx->mic);
    ESP_RETURN_ON_ERROR(start_err, TAG, "start failed");
    ctx->sample_rate_hz = sample_rate_hz;
    ctx->stream_mutex = xSemaphoreCreateMutex();
    if (!ctx->stream_mutex) {
        korvo1_stop(&ctx->mic);
        korvo1_deinit(&ctx->mic);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Korvo-1 microphone initialized and started successfully");
    return ESP_OK;
}

esp_err_t korvo_audio_acquire(korvo_audio_t *ctx, TickType_t timeout_ticks)
{
    ESP_RETURN_ON_FALSE(ctx && ctx->stream_mutex, ESP_ERR_INVALID_STATE, TAG, "bad ctx");
    if (xSemaphoreTake(ctx->stream_mutex, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void korvo_audio_release(korvo_audio_t *ctx)
{
    if (!ctx || !ctx->stream_mutex) {
        return;
    }
    xSemaphoreGive(ctx->stream_mutex);
}

esp_err_t korvo_audio_capture_locked(korvo_audio_t *ctx,
                                     int16_t *buffer,
                                     size_t samples_requested,
                                     size_t *samples_read,
                                     TickType_t timeout_ticks)
{
    ESP_RETURN_ON_FALSE(ctx && buffer, ESP_ERR_INVALID_ARG, TAG, "bad args");
    size_t bytes_read = 0;
    size_t bytes_to_read = samples_requested * sizeof(int16_t);
    esp_err_t err = korvo1_read(&ctx->mic, buffer, bytes_to_read, &bytes_read, timeout_ticks);
    if (samples_read) {
        size_t sample_size = sizeof(int16_t);
        *samples_read = sample_size > 0 ? bytes_read / sample_size : 0;
    }
    
    // Debug: Log first capture and periodic stats
    static bool first_capture = true;
    static int capture_count = 0;
    if (first_capture && err == ESP_OK && bytes_read > 0) {
        int16_t first_sample = buffer[0];
        int16_t max_sample = first_sample;
        int16_t min_sample = first_sample;
        int64_t sum = 0;
        size_t sample_size = sizeof(int16_t);
        size_t valid_samples = sample_size > 0 ? bytes_read / sample_size : 0;
        for (size_t i = 0; i < valid_samples && i < 100; i++) {
            if (buffer[i] > max_sample) max_sample = buffer[i];
            if (buffer[i] < min_sample) min_sample = buffer[i];
            sum += buffer[i] >= 0 ? buffer[i] : -buffer[i];
        }
        float avg_level = (float)sum / (float)valid_samples;
        ESP_LOGI(TAG, "First audio capture: %zu samples, first=%d, min=%d, max=%d, avg_level=%.0f",
                 valid_samples, first_sample, min_sample, max_sample, avg_level);
        first_capture = false;
    }
    
    capture_count++;
    if (err != ESP_OK && capture_count % 100 == 0) {
        ESP_LOGW(TAG, "Audio capture error: %s (err=%d), bytes_read=%zu", esp_err_to_name(err), err, bytes_read);
    }
    
    return err;
}

esp_err_t korvo_audio_capture(korvo_audio_t *ctx,
                              int16_t *buffer,
                              size_t samples_requested,
                              size_t *samples_read,
                              TickType_t timeout_ticks)
{
    ESP_RETURN_ON_ERROR(korvo_audio_acquire(ctx, timeout_ticks), TAG, "acquire");
    esp_err_t err = korvo_audio_capture_locked(ctx, buffer, samples_requested, samples_read, timeout_ticks);
    korvo_audio_release(ctx);
    return err;
}

void korvo_audio_shutdown(korvo_audio_t *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->stream_mutex) {
        vSemaphoreDelete(ctx->stream_mutex);
        ctx->stream_mutex = NULL;
    }
    korvo1_stop(&ctx->mic);
    korvo1_deinit(&ctx->mic);
}
