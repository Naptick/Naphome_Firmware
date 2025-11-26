#include "korvo_audio.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

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
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    };
    return cfg;
}

esp_err_t korvo_audio_init(korvo_audio_t *ctx, int sample_rate_hz)
{
    ESP_RETURN_ON_FALSE(ctx, ESP_ERR_INVALID_ARG, TAG, "ctx required");
    korvo1_config_t cfg = default_korvo1_pins(sample_rate_hz);
    ESP_RETURN_ON_ERROR(korvo1_init(&ctx->mic, &cfg), TAG, "init failed");
    ESP_RETURN_ON_ERROR(korvo1_start(&ctx->mic), TAG, "start failed");
    ctx->sample_rate_hz = sample_rate_hz;
    return ESP_OK;
}

esp_err_t korvo_audio_capture(korvo_audio_t *ctx, int16_t *buffer, size_t samples_requested, size_t *samples_read, TickType_t timeout_ticks)
{
    ESP_RETURN_ON_FALSE(ctx && buffer, ESP_ERR_INVALID_ARG, TAG, "bad args");
    size_t bytes_read = 0;
    size_t bytes_to_read = samples_requested * sizeof(int16_t);
    esp_err_t err = korvo1_read(&ctx->mic, buffer, bytes_to_read, &bytes_read, timeout_ticks);
    if (samples_read) {
        *samples_read = bytes_read / sizeof(int16_t);
    }
    return err;
}

void korvo_audio_shutdown(korvo_audio_t *ctx)
{
    if (!ctx) {
        return;
    }
    korvo1_stop(&ctx->mic);
    korvo1_deinit(&ctx->mic);
}
