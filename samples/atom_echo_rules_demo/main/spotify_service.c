#include "spotify_service.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "spotify_service";

static bool s_ready;
static spotify_service_config_t s_cfg;

esp_err_t spotify_service_init(const spotify_service_config_t *cfg)
{
    if (!cfg || !cfg->device_name || cfg->device_name[0] == '\0') {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    s_cfg = *cfg;
    if (s_cfg.volume_step_percent <= 0) {
        s_cfg.volume_step_percent = 10;
    }

    ESP_LOGI(TAG,
             "Spotify demo initialised for device \"%s\" (volume step %d%%). "
             "Integrate cspot or Spotify Web API here for real playback.",
             s_cfg.device_name,
             s_cfg.volume_step_percent);

    s_ready = true;
    return ESP_OK;
}

bool spotify_service_is_ready(void)
{
    return s_ready;
}

esp_err_t spotify_service_play_demo(const char *query)
{
    if (!s_ready) {
        ESP_LOGW(TAG, "Spotify service not initialised");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG,
             "🎵 [Spotify] Simulating play request for \"%s\" on device \"%s\"",
             (query && query[0]) ? query : "(default playlist)",
             s_cfg.device_name);
    ESP_LOGI(TAG,
             "     -> Add real playback integration via cspot or Spotify Web API.");
    return ESP_OK;
}

esp_err_t spotify_service_pause(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "🎵 [Spotify] Simulating pause on \"%s\"", s_cfg.device_name);
    return ESP_OK;
}

esp_err_t spotify_service_resume(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "🎵 [Spotify] Simulating resume on \"%s\"", s_cfg.device_name);
    return ESP_OK;
}

esp_err_t spotify_service_volume_delta(int delta_percent)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (delta_percent == 0) {
        delta_percent = s_cfg.volume_step_percent;
    }
    ESP_LOGI(TAG,
             "🎵 [Spotify] Simulating volume delta %+d%% on \"%s\"",
             delta_percent,
             s_cfg.device_name);
    return ESP_OK;
}
