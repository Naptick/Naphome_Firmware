#include "status_display.h"

#include <string.h>
#include "esp_log.h"

#define STATUS_TAG "status_display"

// Color definitions (RGB565)
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_ORANGE    0xFC00
#define COLOR_GREY      0x4208
#define COLOR_DARKGREY  0x2104

// Icon patterns - reserved for future pixel-level icon drawing

// Simple icon patterns - using tile-based approach
// We'll draw simple geometric shapes for each status

esp_err_t status_display_init(status_display_t *status, display_matrix_t *display)
{
    if (!status || !display) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(status, 0, sizeof(status_display_t));
    status->display = display;
    
    // Clear display with dark background
    esp_err_t err = display_matrix_fill(display, COLOR_BLACK);
    if (err != ESP_OK) {
        ESP_LOGE(STATUS_TAG, "Failed to clear display");
        return err;
    }
    
    ESP_LOGI(STATUS_TAG, "Status display initialized");
    return ESP_OK;
}

// Draw a simple filled circle-like icon (using tile as base)
static esp_err_t draw_status_icon(display_matrix_t *display,
                                  int tile_x, int tile_y,
                                  uint16_t color,
                                  bool filled)
{
    if (!display) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // For simplicity, we'll use the tile drawing function
    // In a more advanced version, we could draw pixel-level icons
    if (filled) {
        return display_matrix_draw_tile(display, tile_x, tile_y, color);
    } else {
        // Draw border only - fill with black, then draw border pixels
        esp_err_t err = display_matrix_draw_tile(display, tile_x, tile_y, COLOR_BLACK);
        if (err != ESP_OK) return err;
        
        // For now, just draw a smaller filled area to simulate a border
        // This is a simplified version - full implementation would use pixel-level drawing
        return ESP_OK;
    }
}

esp_err_t status_display_update(status_display_t *status,
                                bool wifi_connected,
                                bool aws_connected,
                                bool ble_advertising,
                                bool spotify_ready,
                                bool gemini_ready,
                                bool system_ok)
{
    if (!status || !status->display) {
        return ESP_ERR_INVALID_ARG;
    }
    
    status->wifi_connected = wifi_connected;
    status->aws_connected = aws_connected;
    status->ble_advertising = ble_advertising;
    status->spotify_ready = spotify_ready;
    status->gemini_ready = gemini_ready;
    status->system_ok = system_ok;
    
    // Simplified layout: 3x2 grid of status icons with minimal nested loops
    // Row 0: Wi-Fi (1,1), AWS (4,1), BLE (7,1)
    // Row 1: Spotify (1,4), Gemini (4,4), System (7,4)
    
    // Clear background first
    display_matrix_fill(status->display, COLOR_BLACK);
    
    // Draw status icons directly (simplified to reduce stack usage)
    display_matrix_draw_tile(status->display, 1, 1, wifi_connected ? COLOR_GREEN : COLOR_RED);
    display_matrix_draw_tile(status->display, 4, 1, aws_connected ? COLOR_BLUE : COLOR_RED);
    display_matrix_draw_tile(status->display, 7, 1, ble_advertising ? COLOR_CYAN : COLOR_RED);
    display_matrix_draw_tile(status->display, 1, 4, spotify_ready ? COLOR_GREEN : COLOR_RED);
    display_matrix_draw_tile(status->display, 4, 4, gemini_ready ? COLOR_YELLOW : COLOR_RED);
    display_matrix_draw_tile(status->display, 7, 4, system_ok ? COLOR_GREEN : COLOR_RED);
    
    // Draw simple borders around icons (reduced nesting)
    int icon_positions[6][2] = {{1,1}, {4,1}, {7,1}, {1,4}, {4,4}, {7,4}};
    uint16_t icon_colors[6] = {
        wifi_connected ? COLOR_GREEN : COLOR_DARKGREY,
        aws_connected ? COLOR_BLUE : COLOR_DARKGREY,
        ble_advertising ? COLOR_CYAN : COLOR_DARKGREY,
        spotify_ready ? COLOR_GREEN : COLOR_DARKGREY,
        gemini_ready ? COLOR_YELLOW : COLOR_DARKGREY,
        system_ok ? COLOR_GREEN : COLOR_DARKGREY
    };
    
    // Draw borders around each icon (simplified - just adjacent tiles)
    for (int i = 0; i < 6; i++) {
        int icon_x = icon_positions[i][0];
        int icon_y = icon_positions[i][1];
        uint16_t border_color = icon_colors[i];
        
        // Draw border tiles (top, bottom, left, right)
        if (icon_y > 0) display_matrix_draw_tile(status->display, icon_x, icon_y - 1, border_color);
        if (icon_y < 9) display_matrix_draw_tile(status->display, icon_x, icon_y + 1, border_color);
        if (icon_x > 0) display_matrix_draw_tile(status->display, icon_x - 1, icon_y, border_color);
        if (icon_x < 9) display_matrix_draw_tile(status->display, icon_x + 1, icon_y, border_color);
    }
    
    return ESP_OK;
}

esp_err_t status_display_draw_icon(display_matrix_t *display,
                                   int tile_x, int tile_y,
                                   const uint16_t *icon_pattern,
                                   int icon_width, int icon_height,
                                   uint16_t color_on, uint16_t color_off)
{
    // This is a placeholder for future pixel-level icon drawing
    // For now, we use tile-based drawing
    (void)icon_pattern;
    (void)icon_width;
    (void)icon_height;
    (void)color_off;
    
    return display_matrix_draw_tile(display, tile_x, tile_y, color_on);
}
