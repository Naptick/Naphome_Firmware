// M5GFX wrapper for display_matrix interface
#include "display_matrix.h"
#include <M5GFX.h>
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#define DISPLAY_TAG "display_matrix"

struct display_matrix {
    display_matrix_config_t cfg;
    m5gfx::M5GFX *gfx;
    int tile_width;
    int tile_height;
    uint16_t *tile_buffer;
    size_t tile_buffer_pixels;
};

extern "C" {

esp_err_t display_matrix_init(display_matrix_t **out_display,
                              const display_matrix_config_t *cfg)
{
    if (!out_display || !cfg || cfg->tile_rows <= 0 || cfg->tile_cols <= 0 ||
        cfg->panel_width <= 0 || cfg->panel_height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_display = NULL;

    display_matrix_t *display = (display_matrix_t*)calloc(1, sizeof(display_matrix_t));
    if (!display) {
        return ESP_ERR_NO_MEM;
    }
    display->cfg = *cfg;

    ESP_LOGI(DISPLAY_TAG, "Initializing M5GFX for AtomS3R (GC9107 128x128)...");
    
    // Create M5GFX instance
    display->gfx = new m5gfx::M5GFX();
    if (!display->gfx) {
        ESP_LOGE(DISPLAY_TAG, "Failed to create M5GFX instance");
        free(display);
        return ESP_ERR_NO_MEM;
    }

    // Initialize - M5GFX auto-detects AtomS3R board
    if (!display->gfx->init()) {
        ESP_LOGE(DISPLAY_TAG, "M5GFX init failed");
        delete display->gfx;
        free(display);
        return ESP_FAIL;
    }
    
    // Set display parameters
    display->gfx->setRotation(0);
    display->gfx->setBrightness(255);
    
    ESP_LOGI(DISPLAY_TAG, "M5GFX initialized successfully");
    ESP_LOGI(DISPLAY_TAG, "Display size: %dx%d", display->gfx->width(), display->gfx->height());

    display->tile_width = cfg->panel_width / cfg->tile_cols;
    display->tile_height = cfg->panel_height / cfg->tile_rows;
    if (display->tile_width <= 0 || display->tile_height <= 0) {
        ESP_LOGE(DISPLAY_TAG, "Invalid tile size computed (%d x %d)", display->tile_width, display->tile_height);
        delete display->gfx;
        free(display);
        return ESP_ERR_INVALID_ARG;
    }

    display->tile_buffer_pixels = display->tile_width * display->tile_height;
    display->tile_buffer = (uint16_t*)heap_caps_malloc(display->tile_buffer_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!display->tile_buffer) {
        ESP_LOGE(DISPLAY_TAG, "Failed to allocate tile buffer");
        delete display->gfx;
        free(display);
        return ESP_ERR_NO_MEM;
    }

    *out_display = display;
    return ESP_OK;
}

void display_matrix_deinit(display_matrix_t *display)
{
    if (!display) {
        return;
    }
    if (display->gfx) {
        display->gfx->setBrightness(0);
        delete display->gfx;
        display->gfx = nullptr;
    }
    free(display->tile_buffer);
    free(display);
}

esp_err_t display_matrix_fill(display_matrix_t *display, uint16_t rgb565)
{
    if (!display || !display->gfx) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // M5GFX/LGFX uses color565 directly
    display->gfx->fillScreen(rgb565);
    return ESP_OK;
}

esp_err_t display_matrix_draw_bitmap(display_matrix_t *display,
                                     int x,
                                     int y,
                                     int width,
                                     int height,
                                     const uint16_t *bitmap)
{
    if (!display || !display->gfx || !bitmap || width <= 0 || height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(DISPLAY_TAG, "Drawing bitmap via M5GFX: x=%d, y=%d, w=%d, h=%d", x, y, width, height);
    
    // M5GFX pushImage for RGB565 data (bitmap is already RGB565)
    display->gfx->pushImage(x, y, width, height, bitmap);
    
    ESP_LOGI(DISPLAY_TAG, "Bitmap drawn successfully");
    return ESP_OK;
}

esp_err_t display_matrix_draw_tile(display_matrix_t *display,
                                   int tile_x,
                                   int tile_y,
                                   uint16_t color)
{
    if (!display || !display->gfx) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tile_x < 0 || tile_x >= display->cfg.tile_cols ||
        tile_y < 0 || tile_y >= display->cfg.tile_rows) {
        return ESP_ERR_INVALID_ARG;
    }

    int x0 = tile_x * display->tile_width;
    int y0 = tile_y * display->tile_height;
    int x1 = x0 + display->tile_width;
    int y1 = y0 + display->tile_height;

    // M5GFX uses color565 directly
    display->gfx->fillRect(x0, y0, x1 - x0, y1 - y0, color);
    return ESP_OK;
}

esp_err_t display_matrix_draw_pixel(display_matrix_t *display,
                                    int tile_x,
                                    int tile_y,
                                    uint16_t rgb565)
{
    return display_matrix_draw_tile(display, tile_x, tile_y, rgb565);
}

esp_err_t display_matrix_flush(display_matrix_t *display)
{
    if (!display || !display->gfx) {
        return ESP_ERR_INVALID_ARG;
    }
    // M5GFX handles flushing automatically
    // No explicit flush needed - drawing operations are immediate
    return ESP_OK;
}

} // extern "C"
