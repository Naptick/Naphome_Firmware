#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spotify_client {
    char device_name[64];
    int volume_step;
} spotify_client_t;

typedef struct {
    const char *device_name;
    int volume_step;
} spotify_client_config_t;

esp_err_t spotify_client_init(spotify_client_t *client, const spotify_client_config_t *cfg);
esp_err_t spotify_client_play(spotify_client_t *client, const char *query);
esp_err_t spotify_client_pause(spotify_client_t *client);
esp_err_t spotify_client_resume(spotify_client_t *client);
esp_err_t spotify_client_volume_delta(spotify_client_t *client, int delta_percent);

#ifdef __cplusplus
}
#endif
