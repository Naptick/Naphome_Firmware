#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hal/gpio_types.h"
#include "hal/rmt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_MODEL_WS2812 = 0,
} led_strip_model_t;

typedef enum {
    LED_STRIP_COLOR_COMPONENT_FMT_RGB = 0,
} led_strip_color_component_format_t;

typedef struct {
    int strip_gpio_num;
    uint32_t max_leds;
    led_strip_model_t led_model;
    led_strip_color_component_format_t color_component_format;
    struct {
        bool invert_out;
    } flags;
} led_strip_config_t;

typedef struct {
    uint32_t resolution_hz;
    bool with_dma;
} led_strip_rmt_config_t;

typedef struct led_strip_impl *led_strip_handle_t;

esp_err_t led_strip_new_rmt_device(const led_strip_config_t *config,
                                   const led_strip_rmt_config_t *rmt_config,
                                   led_strip_handle_t *ret_handle);
esp_err_t led_strip_set_pixel(led_strip_handle_t handle, uint32_t index, uint32_t red, uint32_t green, uint32_t blue);
esp_err_t led_strip_refresh(led_strip_handle_t handle);
esp_err_t led_strip_clear(led_strip_handle_t handle);
esp_err_t led_strip_del(led_strip_handle_t handle);

#ifdef __cplusplus
}
#endif
