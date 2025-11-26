#include "display_matrix.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DISPLAY_TAG "display_matrix"

struct display_matrix {
    display_matrix_config_t cfg;
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    bool bus_initialized;
    int tile_width;
    int tile_height;
    uint16_t *tile_buffer;
    size_t tile_buffer_pixels;
};

// LP5562 I2C address (typical)
#define LP5562_I2C_ADDR 0x30

// LP5562 registers (per M5Stack official implementation)
#define LP5562_REG_ENABLE 0x00
#define LP5562_REG_OP_MODE 0x01
#define LP5562_REG_B_PWM 0x07  // Blue channel PWM
#define LP5562_REG_W_PWM 0x0E  // White channel PWM (backlight on Atom S3R) - M5Stack uses 0x0E, not 0x0D
#define LP5562_REG_CONFIG 0x08  // Configuration register (M5Stack sets this to 0x01)
#define LP5562_REG_LED_MAP 0x70

static esp_err_t configure_backlight_lp5562(bool on)
{
    // LP5562 is on I2C bus: SDA=GPIO45, SCL=GPIO0 (per schematic)
    // Note: GPIO0 is a strapping pin, but after boot it can be used for I2C
    ESP_LOGI(DISPLAY_TAG, "Configuring LP5562 backlight via I2C (SDA=GPIO45, SCL=GPIO0, addr=0x%02X)", LP5562_I2C_ADDR);
    
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 45,
        .scl_io_num = 0,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,  // Start with 100kHz for reliability
    };
    
    esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(DISPLAY_TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }
    
    err = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(DISPLAY_TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(DISPLAY_TAG, "I2C driver already installed, reusing");
    } else {
        ESP_LOGI(DISPLAY_TAG, "I2C driver installed successfully");
    }
    
    // Enable LP5562 chip - following M5Stack official implementation
    // Register 0x00: Enable chip (bit 6 = chip enable)
    uint8_t enable_val = on ? 0x40 : 0x00;  // 0b01000000 = chip enable
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LP5562_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, LP5562_REG_ENABLE, true);
    i2c_master_write_byte(cmd, enable_val, true);
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    if (err != ESP_OK) {
        ESP_LOGW(DISPLAY_TAG, "LP5562 enable write failed: %s (chip may not be present)", esp_err_to_name(err));
        i2c_driver_delete(I2C_NUM_0);
        return err;
    }
    ESP_LOGI(DISPLAY_TAG, "LP5562 enable register written: 0x%02X", enable_val);
    
    if (on) {
        vTaskDelay(pdMS_TO_TICKS(1));  // Delay after enable (M5Stack uses 1ms)
        
        // Register 0x08: Configuration register (M5Stack sets to 0x01)
        cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (LP5562_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, LP5562_REG_CONFIG, true);
        i2c_master_write_byte(cmd, 0x01, true);  // Configuration value from M5Stack
        i2c_master_stop(cmd);
        err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        if (err != ESP_OK) {
            ESP_LOGW(DISPLAY_TAG, "LP5562 CONFIG write failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(DISPLAY_TAG, "LP5562 CONFIG register set to 0x01");
        }
        
        // Register 0x70: LED_MAP (M5Stack sets to 0x00)
        cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (LP5562_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, LP5562_REG_LED_MAP, true);
        i2c_master_write_byte(cmd, 0x00, true);  // Direct mode mapping
        i2c_master_stop(cmd);
        err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        if (err != ESP_OK) {
            ESP_LOGW(DISPLAY_TAG, "LP5562 LED_MAP write failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(DISPLAY_TAG, "LP5562 LED_MAP configured: 0x00");
        }
        
        // Register 0x0E: White channel PWM (brightness) - M5Stack uses 0x0E, not 0x0D
        uint8_t pwm_val = 0xFF;  // Full brightness
        cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (LP5562_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, LP5562_REG_W_PWM, true);
        i2c_master_write_byte(cmd, pwm_val, true);  // Full brightness
        i2c_master_stop(cmd);
        err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        
        if (err != ESP_OK) {
            ESP_LOGW(DISPLAY_TAG, "LP5562 W_PWM write failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(DISPLAY_TAG, "LP5562 W_PWM (0x0E) set to 0x%02X (full brightness)", pwm_val);
        }
    } else {
        // Turn off PWM
        cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (LP5562_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, LP5562_REG_W_PWM, true);
        i2c_master_write_byte(cmd, 0x00, true);  // Off
        i2c_master_stop(cmd);
        i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        ESP_LOGI(DISPLAY_TAG, "LP5562 backlight disabled via I2C");
    }
    
    // Don't delete I2C driver as it might be used by other components
    return ESP_OK;
}

static esp_err_t configure_backlight(const display_matrix_config_t *cfg, bool on)
{
    // According to schematic, backlight is controlled by LP5562 via I2C, not direct GPIO
    // Try I2C control first, fall back to GPIO if I2C fails
    esp_err_t err = configure_backlight_lp5562(on);
    if (err == ESP_OK) {
        return ESP_OK;
    }
    
    // Fallback to direct GPIO control (for compatibility)
    if (cfg->backlight_gpio < 0) {
        ESP_LOGI(DISPLAY_TAG, "Backlight GPIO not configured (< 0), skipping");
        return ESP_OK;
    }
    ESP_LOGI(DISPLAY_TAG, "Falling back to GPIO%d for backlight control", cfg->backlight_gpio);
    gpio_config_t io_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << cfg->backlight_gpio,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&io_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(DISPLAY_TAG, "gpio_config(backlight) failed: %s", esp_err_to_name(err));
        return err;
    }
    err = gpio_set_level(cfg->backlight_gpio, on ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(DISPLAY_TAG, "gpio_set_level(backlight) failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(DISPLAY_TAG, "Backlight configured via GPIO");
    return ESP_OK;
}

esp_err_t display_matrix_init(display_matrix_t **out_display,
                              const display_matrix_config_t *cfg)
{
    if (!out_display || !cfg || cfg->tile_rows <= 0 || cfg->tile_cols <= 0 ||
        cfg->panel_width <= 0 || cfg->panel_height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_display = NULL;

    display_matrix_t *display = calloc(1, sizeof(display_matrix_t));
    if (!display) {
        return ESP_ERR_NO_MEM;
    }
    display->cfg = *cfg;

    spi_bus_config_t buscfg = {
        .sclk_io_num = cfg->sclk_gpio,
        .mosi_io_num = cfg->mosi_gpio,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = cfg->max_transfer_bytes > 0 ? cfg->max_transfer_bytes : cfg->panel_width * cfg->panel_height * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(cfg->spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(DISPLAY_TAG, "spi_bus_initialize failed (%s)", esp_err_to_name(err));
        goto fail;
    }
    display->bus_initialized = true;

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = cfg->dc_gpio,
        .cs_gpio_num = cfg->cs_gpio,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_LOGI(DISPLAY_TAG, "SPI IO config: DC=%d, CS=%d, freq=%d MHz, mode=%d", 
             cfg->dc_gpio, cfg->cs_gpio, io_config.pclk_hz / 1000000, io_config.spi_mode);
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)cfg->spi_host,
                                   &io_config,
                                   &display->io);
    if (err != ESP_OK) {
        ESP_LOGE(DISPLAY_TAG, "esp_lcd_new_panel_io_spi failed (%s)", esp_err_to_name(err));
        goto fail;
    }

    // GC9107 panel initialization (custom driver since ESP-IDF doesn't have it)
    ESP_LOGI(DISPLAY_TAG, "Initializing GC9107 panel (custom driver)...");
    
    // Configure reset GPIO
    if (cfg->reset_gpio >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << cfg->reset_gpio,
        };
        err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(DISPLAY_TAG, "GPIO config for RST failed: %s", esp_err_to_name(err));
            goto fail;
        }
        gpio_set_level(cfg->reset_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(cfg->reset_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // GC9107 initialization sequence (based on M5GFX Panel_GC9107)
    ESP_LOGI(DISPLAY_TAG, "Sending GC9107 init commands...");
    
    // Soft reset sequence
    esp_lcd_panel_io_tx_param(display->io, 0xFE, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    esp_lcd_panel_io_tx_param(display->io, 0xEF, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    
    // Configuration commands
    uint8_t b0_val = 0xC0;
    uint8_t b2_val = 0x2F;
    uint8_t b3_val = 0x03;
    uint8_t b6_val = 0x19;
    uint8_t b7_val = 0x01;
    uint8_t ac_val = 0xCB;
    uint8_t ab_val = 0x0E;
    uint8_t b4_val = 0x04;
    uint8_t a8_val = 0x19;
    uint8_t b8_val = 0x08;
    uint8_t e8_val = 0x24;
    uint8_t e9_val = 0x48;
    uint8_t ea_val = 0x22;
    uint8_t c6_val = 0x30;
    uint8_t c7_val = 0x18;
    
    esp_lcd_panel_io_tx_param(display->io, 0xB0, &b0_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xB2, &b2_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xB3, &b3_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xB6, &b6_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xB7, &b7_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xAC, &ac_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xAB, &ab_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xB4, &b4_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xA8, &a8_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xB8, &b8_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xE8, &e8_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xE9, &e9_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xEA, &ea_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xC6, &c6_val, 1);
    esp_lcd_panel_io_tx_param(display->io, 0xC7, &c7_val, 1);
    
    // Gamma settings
    uint8_t f0_params[] = {0x01,0x2b,0x23,0x3c,0xb7,0x12,0x17,0x60,0x00,0x06,0x0c,0x17,0x12,0x1f};
    uint8_t f1_params[] = {0x01,0x2b,0x23,0x3c,0xb7,0x12,0x17,0x60,0x00,0x06,0x0c,0x17,0x12,0x1f};
    esp_lcd_panel_io_tx_param(display->io, 0xF0, f0_params, sizeof(f0_params));
    esp_lcd_panel_io_tx_param(display->io, 0xF1, f1_params, sizeof(f1_params));
    
    // Set color mode (RGB565 = 0x55)
    uint8_t colmod = 0x55;
    esp_lcd_panel_io_tx_param(display->io, 0x3A, &colmod, 1);
    
    // Set MADCTL (Memory Access Control) - RGB mode, normal orientation
    // For GC9107: 0x00 = normal, 0x20 = mirror Y, 0x40 = mirror X, 0x80 = rotate 180
    uint8_t madctl = 0x00; // Normal orientation
    esp_lcd_panel_io_tx_param(display->io, 0x36, &madctl, 1);
    
    // Set display window - GC9107 on AtomS3R has offset_y=32 (per M5GFX)
    // Set column address (CASET) - 0x2A
    uint8_t caset[] = {0x00, 0x00, 0x00, 0x7F}; // 0 to 127 (128 pixels)
    esp_lcd_panel_io_tx_param(display->io, 0x2A, caset, 4);
    // Set row address (RASET) - 0x2B with offset
    uint8_t raset[] = {0x00, 0x20, 0x00, 0x9F}; // 32 to 159 (128 pixels with 32 offset)
    esp_lcd_panel_io_tx_param(display->io, 0x2B, raset, 4);
    
    // Exit sleep mode
    esp_lcd_panel_io_tx_param(display->io, 0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120)); // Wait for display to wake up
    
    // Display ON
    esp_lcd_panel_io_tx_param(display->io, 0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    
    ESP_LOGI(DISPLAY_TAG, "GC9107 init commands sent, display should be on");
    
    // Use ST7789 driver as base (GC9107 is command-compatible for drawing operations)
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1, // Already handled manually
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
    };
    
    // Use ST7789 driver - GC9107 is command-compatible for drawing operations
    err = esp_lcd_new_panel_st7789(display->io, &panel_config, &display->panel);
    if (err != ESP_OK) {
        ESP_LOGE(DISPLAY_TAG, "esp_lcd_new_panel_st7789 failed (%s)", esp_err_to_name(err));
        goto fail;
    }
    ESP_LOGI(DISPLAY_TAG, "GC9107 panel driver initialized (using ST7789 interface)");

    // Skip panel reset and init - already done manually for GC9107
    ESP_LOGI(DISPLAY_TAG, "GC9107 initialization complete, skipping ST7789 reset/init");
    
    // Don't call ST7789's init() as it will send conflicting commands
    // The panel is already initialized with GC9107-specific commands above

    ESP_LOGI(DISPLAY_TAG, "Configuring panel mirror...");
    err = esp_lcd_panel_mirror(display->panel, false, true);
    if (err != ESP_OK) {
        ESP_LOGW(DISPLAY_TAG, "panel mirror failed (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(DISPLAY_TAG, "Panel mirror configured (mirror_x=false, mirror_y=true)");
    }
    
    // Try swapping mirror settings - Atom S3R might need different orientation
    // Try without mirror first, then with both mirrors if needed
    ESP_LOGI(DISPLAY_TAG, "Trying panel mirror (mirror_x=false, mirror_y=false)...");
    err = esp_lcd_panel_mirror(display->panel, false, false);
    if (err == ESP_OK) {
        ESP_LOGI(DISPLAY_TAG, "Panel mirror set to (false, false)");
    }

    // Try inverting colors - ST7789 sometimes needs this
    ESP_LOGI(DISPLAY_TAG, "Trying color inversion...");
    err = esp_lcd_panel_invert_color(display->panel, true);
    if (err != ESP_OK) {
        ESP_LOGW(DISPLAY_TAG, "color invert failed (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(DISPLAY_TAG, "Color inversion enabled");
    }
    
    // Set display window to full screen
    ESP_LOGI(DISPLAY_TAG, "Setting display window to (0,0) to (%d,%d)...", cfg->panel_width, cfg->panel_height);
    err = esp_lcd_panel_set_gap(display->panel, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGW(DISPLAY_TAG, "set_gap failed (%s)", esp_err_to_name(err));
    }
    
    // Display is already turned on in GC9107 init sequence above
    // Don't call ST7789's disp_on_off as it may send wrong commands
    ESP_LOGI(DISPLAY_TAG, "GC9107 display already on from init sequence");
    
    // Add a small delay after panel init before enabling backlight
    vTaskDelay(pdMS_TO_TICKS(50));
    
    ESP_LOGI(DISPLAY_TAG, "Enabling backlight...");
    err = configure_backlight(cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(DISPLAY_TAG, "Backlight configuration failed (%s), continuing anyway", esp_err_to_name(err));
    }
    
    // Note: GPIO0 on ESP32-S3 is a strapping pin. If backlight doesn't work,
    // the LP5562 might need I2C initialization first (I2C address typically 0x30 on SDA=GPIO38, SCL=GPIO39)
    // For now, we try direct GPIO control as documented in the README.

    display->tile_width = cfg->panel_width / cfg->tile_cols;
    display->tile_height = cfg->panel_height / cfg->tile_rows;
    if (display->tile_width <= 0 || display->tile_height <= 0) {
        ESP_LOGE(DISPLAY_TAG, "Invalid tile size computed (%d x %d)", display->tile_width, display->tile_height);
        err = ESP_ERR_INVALID_ARG;
        goto fail;
    }

    display->tile_buffer_pixels = display->tile_width * display->tile_height;
    display->tile_buffer = heap_caps_malloc(display->tile_buffer_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!display->tile_buffer) {
        err = ESP_ERR_NO_MEM;
        ESP_LOGE(DISPLAY_TAG, "Failed to allocate tile buffer");
        goto fail;
    }

    *out_display = display;
    return ESP_OK;

fail:
    display_matrix_deinit(display);
    return err ? err : ESP_FAIL;
}

void display_matrix_deinit(display_matrix_t *display)
{
    if (!display) {
        return;
    }
    configure_backlight(&display->cfg, false);
    if (display->panel) {
        esp_lcd_panel_disp_on_off(display->panel, false);
        esp_lcd_panel_del(display->panel);
    }
    if (display->io) {
        esp_lcd_panel_io_del(display->io);
    }
    if (display->bus_initialized) {
        spi_bus_free(display->cfg.spi_host);
    }
    free(display->tile_buffer);
    free(display);
}

static esp_err_t draw_filled_rect(display_matrix_t *display,
                                  int x0,
                                  int y0,
                                  int x1,
                                  int y1,
                                  uint16_t color)
{
    int width = x1 - x0;
    int height = y1 - y0;
    size_t pixels = (size_t)width * height;
    if (pixels > display->tile_buffer_pixels) {
        uint16_t *scratch = heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
        if (!scratch) {
            return ESP_ERR_NO_MEM;
        }
        for (size_t i = 0; i < pixels; ++i) {
            scratch[i] = color;
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(display->panel, x0, y0, x1, y1, scratch);
        free(scratch);
        return err;
    }
    for (size_t i = 0; i < pixels; ++i) {
        display->tile_buffer[i] = color;
    }
    ESP_LOGI(DISPLAY_TAG, "Drawing filled rect: (%d,%d) to (%d,%d), color=0x%04X, pixels=%zu", 
             x0, y0, x1, y1, color, pixels);
    esp_err_t err = esp_lcd_panel_draw_bitmap(display->panel, x0, y0, x1, y1, display->tile_buffer);
    if (err != ESP_OK) {
        ESP_LOGE(DISPLAY_TAG, "esp_lcd_panel_draw_bitmap (fill) failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t display_matrix_fill(display_matrix_t *display, uint16_t rgb565)
{
    if (!display) {
        return ESP_ERR_INVALID_ARG;
    }
    return draw_filled_rect(display,
                            0,
                            0,
                            display->cfg.panel_width,
                            display->cfg.panel_height,
                            rgb565);
}

esp_err_t display_matrix_draw_pixel(display_matrix_t *display,
                                    int tile_x,
                                    int tile_y,
                                    uint16_t rgb565)
{
    if (!display) {
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
    return draw_filled_rect(display, x0, y0, x1, y1, rgb565);
}

esp_err_t display_matrix_draw_tile(display_matrix_t *display,
                                   int tile_x,
                                   int tile_y,
                                   uint16_t color)
{
    return display_matrix_draw_pixel(display, tile_x, tile_y, color);
}

esp_err_t display_matrix_flush(display_matrix_t *display)
{
    (void)display;
    return ESP_OK;
}

esp_err_t display_matrix_draw_bitmap(display_matrix_t *display,
                                     int x,
                                     int y,
                                     int width,
                                     int height,
                                     const uint16_t *bitmap)
{
    if (!display || !bitmap || width <= 0 || height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (x < 0 || y < 0 || 
        x + width > display->cfg.panel_width || 
        y + height > display->cfg.panel_height) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t pixels = (size_t)width * height;
    
    // Allocate DMA-capable buffer for the bitmap
    uint16_t *dma_buffer = heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!dma_buffer) {
        ESP_LOGE(DISPLAY_TAG, "Failed to allocate DMA buffer for bitmap");
        return ESP_ERR_NO_MEM;
    }

    // Copy bitmap data to DMA buffer
    memcpy(dma_buffer, bitmap, pixels * sizeof(uint16_t));
    
    ESP_LOGI(DISPLAY_TAG, "Drawing bitmap: x=%d, y=%d, w=%d, h=%d, pixels=%zu", 
             x, y, width, height, pixels);
    ESP_LOGI(DISPLAY_TAG, "First few pixels: 0x%04X, 0x%04X, 0x%04X, 0x%04X", 
             bitmap[0], bitmap[1], bitmap[2], bitmap[3]);

    // Draw the bitmap
    esp_err_t err = esp_lcd_panel_draw_bitmap(display->panel, x, y, x + width, y + height, dma_buffer);
    
    if (err != ESP_OK) {
        ESP_LOGE(DISPLAY_TAG, "esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(DISPLAY_TAG, "Bitmap draw command sent successfully");
    }
    
    free(dma_buffer);
    return err;
}
