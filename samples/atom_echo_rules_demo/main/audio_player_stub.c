#include "audio_player.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "audio_player_stub";

esp_err_t audio_player_init(const audio_player_config_t *cfg)
{
    (void)cfg;
    ESP_LOGW(TAG, "Audio player stub active – no audio hardware is configured on this sample.");
    return ESP_OK;
}

esp_err_t audio_player_play_wav(const uint8_t *wav_data, size_t wav_len)
{
    (void)wav_data;
    (void)wav_len;
    ESP_LOGW(TAG, "audio_player_play_wav stub called – ignoring request");
    return ESP_OK;
}

esp_err_t audio_player_submit_pcm(const int16_t *samples,
                                  size_t sample_count,
                                  int sample_rate_hz,
                                  int num_channels)
{
    (void)samples;
    (void)sample_count;
    (void)sample_rate_hz;
    (void)num_channels;
    // Intentionally silent – cspot will still believe playback succeeded.
    return ESP_OK;
}

void audio_player_shutdown(void)
{
    ESP_LOGW(TAG, "audio_player_shutdown stub called – nothing to tear down");
}
