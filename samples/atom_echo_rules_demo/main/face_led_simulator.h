/**
 * Face LED Simulator
 * 
 * Simulates LED patterns on the NaphomeFace.png image by rendering
 * LED effects on the oval region of the face.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "display_matrix.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration for FreeRTOS types
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// LED pattern types (matching somnus_action_handler)
typedef enum {
    FACE_LED_PATTERN_NONE = 0,
    FACE_LED_PATTERN_BREATHING,
    FACE_LED_PATTERN_GRADIENT_RED_BLUE,
    FACE_LED_PATTERN_GRADIENT_RED_YELLOW,
    FACE_LED_PATTERN_GRADIENT_WHITE_BLUE,
    FACE_LED_PATTERN_GRADIENT_BLUE_GREEN,
    FACE_LED_PATTERN_GRADIENT_TEAL_ORANGE,
    FACE_LED_PATTERN_PULSE_LILAC,
    FACE_LED_PATTERN_PULSE_ORANGE,
} face_led_pattern_t;

typedef struct face_led_simulator {
    display_matrix_t *display;
    face_led_pattern_t current_pattern;
    uint8_t current_r, current_g, current_b;
    float current_intensity;
    bool pattern_active;
    TaskHandle_t pattern_task;
    
    // Face image data (128x128 RGB565)
    const uint16_t *face_image;
    uint16_t *render_buffer;  // Working buffer for rendering
    
    // LED positions (50 LEDs: 16 inner + 24 outer)
    struct {
        int x, y;
        float angle;
    } led_positions[50];
    int num_leds;
} face_led_simulator_t;

/**
 * Initialize the face LED simulator
 * @param simulator Pointer to simulator structure
 * @param display Display matrix handle
 * @param face_image RGB565 image data (128x128)
 * @return ESP_OK on success
 */
esp_err_t face_led_simulator_init(face_led_simulator_t *simulator,
                                  display_matrix_t *display,
                                  const uint16_t *face_image);

/**
 * Deinitialize the face LED simulator
 */
void face_led_simulator_deinit(face_led_simulator_t *simulator);

/**
 * Set LED pattern
 * @param simulator Simulator handle
 * @param pattern Pattern type
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @param intensity Intensity multiplier (0.0-1.0)
 * @return ESP_OK on success
 */
esp_err_t face_led_simulator_set_pattern(face_led_simulator_t *simulator,
                                         face_led_pattern_t pattern,
                                         uint8_t r, uint8_t g, uint8_t b,
                                         float intensity);

/**
 * Stop current pattern
 */
void face_led_simulator_stop_pattern(face_led_simulator_t *simulator);

#ifdef __cplusplus
}
#endif
