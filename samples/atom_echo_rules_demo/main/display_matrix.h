#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct display_matrix display_matrix_t;

typedef struct {
    int spi_host;            // e.g. SPI2_HOST
    int sclk_gpio;
    int mosi_gpio;
    int cs_gpio;
    int dc_gpio;
    int reset_gpio;
    int backlight_gpio;
    uint32_t max_transfer_bytes;
    int tile_rows;
    int tile_cols;
    int panel_width;
    int panel_height;
} display_matrix_config_t;

esp_err_t display_matrix_init(display_matrix_t **out_display,
                              const display_matrix_config_t *cfg);
void display_matrix_deinit(display_matrix_t *display);

esp_err_t display_matrix_fill(display_matrix_t *display, uint16_t rgb565);
esp_err_t display_matrix_draw_pixel(display_matrix_t *display,
                                    int tile_x,
                                    int tile_y,
                                    uint16_t rgb565);
esp_err_t display_matrix_flush(display_matrix_t *display);
esp_err_t display_matrix_draw_tile(display_matrix_t *display,
                                   int tile_x,
                                   int tile_y,
                                   uint16_t color);

esp_err_t display_matrix_draw_bitmap(display_matrix_t *display,
                                     int x,
                                     int y,
                                     int width,
                                     int height,
                                     const uint16_t *bitmap);

#ifdef __cplusplus
}
#endif
