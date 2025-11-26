#include "spotify_client.h"
#include "kva_config_defaults.h"

#include <string.h>

#include "sdkconfig.h"

#include "esp_check.h"
#include "esp_log.h"
#include "spotify_player.h"

static const char *TAG = "spotify_client";

static bool spotify_client_use_local_player(void)
{
#if CONFIG_KVA_SPOTIFY_USE_CSPOT
    return spotify_player_is_ready();
#else
    return false;
#endif
}

esp_err_t spotify_client_init(spotify_client_t *client, const spotify_client_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(client && cfg, ESP_ERR_INVALID_ARG, TAG, "bad args");
    strlcpy(client->device_name, cfg->device_name ? cfg->device_name : "Korvo-1", sizeof(client->device_name));
    client->volume_step = cfg->volume_step > 0 ? cfg->volume_step : 10;
    ESP_LOGI(TAG, "Init for device \"%s\" (step=%d%%)", client->device_name, client->volume_step);
    return ESP_OK;
}

esp_err_t spotify_client_play(spotify_client_t *client, const char *query)
{
    ESP_RETURN_ON_FALSE(client, ESP_ERR_INVALID_ARG, TAG, "client required");
    if (spotify_client_use_local_player()) {
        ESP_LOGI(TAG, "Resuming cspot playback for device \"%s\"", client->device_name);
        esp_err_t err = spotify_player_resume();
        if (err == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "cspot resume failed (%s); falling back to stub", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Would send PLAY for \"%s\" on device \"%s\"", query && query[0] ? query : "(default)", client->device_name);
    return ESP_OK;
}

esp_err_t spotify_client_pause(spotify_client_t *client)
{
    ESP_RETURN_ON_FALSE(client, ESP_ERR_INVALID_ARG, TAG, "client required");
    if (spotify_client_use_local_player()) {
        esp_err_t err = spotify_player_pause();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Paused cspot playback");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "cspot pause failed (%s); falling back to stub", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Would send PAUSE on \"%s\"", client->device_name);
    return ESP_OK;
}

esp_err_t spotify_client_resume(spotify_client_t *client)
{
    ESP_RETURN_ON_FALSE(client, ESP_ERR_INVALID_ARG, TAG, "client required");
    if (spotify_client_use_local_player()) {
        esp_err_t err = spotify_player_resume();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Resume via cspot");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "cspot resume failed (%s); falling back to stub", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Would send RESUME on \"%s\"", client->device_name);
    return ESP_OK;
}

esp_err_t spotify_client_volume_delta(spotify_client_t *client, int delta_percent)
{
    ESP_RETURN_ON_FALSE(client, ESP_ERR_INVALID_ARG, TAG, "client required");
    if (delta_percent == 0) {
        delta_percent = client->volume_step;
    }
    if (spotify_client_use_local_player()) {
        esp_err_t err = spotify_player_volume_delta(delta_percent);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Adjusted cspot volume %+d%%", delta_percent);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "cspot volume adjust failed (%s); falling back to stub", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Would adjust volume %+d%% on \"%s\"", delta_percent, client->device_name);
    return ESP_OK;
}
