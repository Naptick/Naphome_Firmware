#pragma once

#include <stdbool.h>
#include "display_matrix.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    display_matrix_t *display;
    bool wifi_connected;
    bool aws_connected;
    bool ble_advertising;
    bool spotify_ready;
    bool gemini_ready;
    bool system_ok;
} status_display_t;

/**
 * Initialize status display
 */
esp_err_t status_display_init(status_display_t *status, display_matrix_t *display);

/**
 * Update status display with current state
 */
esp_err_t status_display_update(status_display_t *status,
                                bool wifi_connected,
                                bool aws_connected,
                                bool ble_advertising,
                                bool spotify_ready,
                                bool gemini_ready,
                                bool system_ok);

/**
 * Draw a simple icon pattern at tile position
 */
esp_err_t status_display_draw_icon(display_matrix_t *display,
                                   int tile_x, int tile_y,
                                   const uint16_t *icon_pattern,
                                   int icon_width, int icon_height,
                                   uint16_t color_on, uint16_t color_off);

#ifdef __cplusplus
}
#endif
