#pragma once

#include "esp_err.h"
#include "scene_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration (must match face_led_simulator.h)
struct face_led_simulator;
typedef struct face_led_simulator face_led_simulator_t;

/**
 * @brief Handle SomnusDevice-style action commands
 * 
 * Supports actions:
 * - LED: Control LED patterns and colors
 * - SongChange: Play audio files
 * - SetVolume: Adjust audio volume
 * - SetLEDIntensity: Adjust LED brightness
 * - Pause/Play: Pause/resume audio and LEDs
 * - Speech: Text-to-speech (placeholder)
 * 
 * @param payload JSON payload string (can be single action or action list)
 * @param led_controller Scene controller handle for LED control
 * @return ESP_OK on success, error code on failure
 */
esp_err_t somnus_action_handler_process(const char *payload, scene_controller_t *led_controller);

/**
 * @brief Set face LED simulator for synchronized display
 * 
 * @param face_simulator Face LED simulator handle (can be NULL to disable)
 */
void somnus_action_handler_set_face_simulator(face_led_simulator_t *face_simulator);

#ifdef __cplusplus
}
#endif
