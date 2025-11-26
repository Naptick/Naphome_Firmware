#pragma once

#include "esp_err.h"
#include "spotify_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize serial command parser.
 * Reads commands from UART and executes them.
 * 
 * @param spotify_client Spotify client instance for control commands
 * @return ESP_OK on success
 */
esp_err_t serial_command_parser_init(spotify_client_t *spotify_client);

#ifdef __cplusplus
}
#endif
