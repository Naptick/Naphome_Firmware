#pragma once

#include "esp_err.h"
#include "hal/gpio_types.h"
#include "led_strip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_CONTROLLER_STATE_IDLE = 0,
    LED_CONTROLLER_STATE_LISTENING,
    LED_CONTROLLER_STATE_THINKING,
    LED_CONTROLLER_STATE_SPEAKING,
    LED_CONTROLLER_STATE_ERROR,
} led_controller_state_t;

typedef struct {
    gpio_num_t data_gpio;
    uint8_t led_count;
    uint8_t brightness;      // 0-255
    uint8_t reserved_pixels; // number of leading LEDs reserved for status indicators
} led_controller_config_t;

typedef struct {
    led_strip_handle_t strip;
    uint8_t led_count;
    uint8_t brightness;
    uint8_t reserved_pixels;
    led_controller_state_t state;
    // Trippy fade animation state
    bool trippy_active;
    float trippy_time;
    float trippy_speed;
} led_controller_t;

esp_err_t led_controller_init(led_controller_t *controller, const led_controller_config_t *config);
void led_controller_set_state(led_controller_t *controller, led_controller_state_t state);
void led_controller_set_pixel_color(led_controller_t *controller, uint8_t pixel_index, uint8_t red, uint8_t green, uint8_t blue);
void led_controller_shutdown(led_controller_t *controller);

// Trippy fade animation functions
void led_controller_start_trippy_fade(led_controller_t *controller);
void led_controller_stop_trippy_fade(led_controller_t *controller);
void led_controller_update_trippy_fade(led_controller_t *controller);

#ifdef __cplusplus
}
#endif
