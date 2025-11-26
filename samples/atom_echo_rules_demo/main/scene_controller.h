#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "led_strip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct scene_controller scene_controller_t;

typedef struct {
    int led_gpio;
    int led_pixel_count;
    int led_pixel_order; // 0=GRB, 1=RGB (reserved for future)
    float master_brightness;
} scene_controller_config_t;

esp_err_t scene_controller_init(scene_controller_t **out_ctrl, const scene_controller_config_t *cfg);
void scene_controller_deinit(scene_controller_t *ctrl);

esp_err_t scene_controller_apply_light_scene(scene_controller_t *ctrl,
                                             const char *scene_id,
                                             uint32_t transition_ms);

esp_err_t scene_controller_set_light_color(scene_controller_t *ctrl,
                                           uint8_t r,
                                           uint8_t g,
                                           uint8_t b,
                                           float brightness,
                                           uint32_t transition_ms);

esp_err_t scene_controller_play_sound_scene(scene_controller_t *ctrl,
                                            const char *scene_id,
                                            float volume_0_1);

esp_err_t scene_controller_play_sound_playlist(scene_controller_t *ctrl,
                                               const char *playlist_id,
                                               float volume_0_1);

// Helper functions for advanced LED control
led_strip_handle_t scene_controller_get_strip(scene_controller_t *ctrl);
int scene_controller_get_pixel_count(scene_controller_t *ctrl);
float scene_controller_get_master_brightness(scene_controller_t *ctrl);

#ifdef __cplusplus
}
#endif
