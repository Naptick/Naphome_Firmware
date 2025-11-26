/**
 * @file spotify_service.h
 * @brief Lightweight Spotify demo hooks for the Atom Echo sample.
 *
 * The full Naphome firmware integrates the cspot SDK to talk to Spotify.
 * For this sample we only log intent so developers can wire real playback
 * later without needing additional credentials.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *device_name;
    int volume_step_percent;
} spotify_service_config_t;

esp_err_t spotify_service_init(const spotify_service_config_t *cfg);
esp_err_t spotify_service_play_demo(const char *query);
esp_err_t spotify_service_pause(void);
esp_err_t spotify_service_resume(void);
esp_err_t spotify_service_volume_delta(int delta_percent);
bool spotify_service_is_ready(void);

#ifdef __cplusplus
}
#endif
