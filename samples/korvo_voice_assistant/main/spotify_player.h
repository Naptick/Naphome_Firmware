#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *device_name;
    const char *credentials_path;
    uint16_t zeroconf_port;
} spotify_player_config_t;

esp_err_t spotify_player_start(const spotify_player_config_t *config);
bool spotify_player_is_ready(void);
esp_err_t spotify_player_pause(void);
esp_err_t spotify_player_resume(void);
esp_err_t spotify_player_set_volume_percent(int percent);
esp_err_t spotify_player_volume_delta(int delta_percent);

#ifdef __cplusplus
}
#endif
