#include "scene_controller.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "led_strip.h"

struct scene_controller {
    scene_controller_config_t cfg;
    led_strip_handle_t strip;
};

static const char *TAG = "scene_controller";

static esp_err_t set_all(scene_controller_t *ctrl, uint8_t r, uint8_t g, uint8_t b, float brightness)
{
    if (!ctrl || !ctrl->strip) {
        return ESP_ERR_INVALID_STATE;
    }
    float master = ctrl->cfg.master_brightness > 0.0f ? ctrl->cfg.master_brightness : 1.0f;
    float scale = brightness > 0.0f ? brightness : 1.0f;
    float factor = master * scale;
    if (factor > 1.0f) {
        factor = 1.0f;
    } else if (factor < 0.01f) {
        factor = 0.01f;
    }
    uint32_t rr = (uint32_t)(r * factor);
    uint32_t gg = (uint32_t)(g * factor);
    uint32_t bb = (uint32_t)(b * factor);
    for (int i = 0; i < ctrl->cfg.led_pixel_count; ++i) {
        ESP_RETURN_ON_ERROR(led_strip_set_pixel(ctrl->strip, i, rr, gg, bb), TAG, "set pixel");
    }
    return led_strip_refresh(ctrl->strip);
}

static esp_err_t apply_scene(scene_controller_t *ctrl, const char *scene_id, uint32_t transition_ms)
{
    if (!scene_id || scene_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(scene_id, "warm_dim") == 0) {
        ESP_LOGI(TAG, "Light scene warm_dim (transition %ums)", (unsigned)transition_ms);
        return set_all(ctrl, 255, 147, 41, 0.15f);
    }
    if (strcmp(scene_id, "gentle_rain") == 0) {
        ESP_LOGI(TAG, "Light scene gentle_rain (transition %ums)", (unsigned)transition_ms);
        return set_all(ctrl, 80, 120, 255, 0.10f);
    }
    if (strcmp(scene_id, "cool_mist") == 0) {
        ESP_LOGI(TAG, "Light scene cool_mist (transition %ums)", (unsigned)transition_ms);
        return set_all(ctrl, 160, 200, 255, 0.20f);
    }
    ESP_LOGW(TAG, "Unknown scene id '%s', clearing", scene_id);
    ESP_RETURN_ON_ERROR(led_strip_clear(ctrl->strip), TAG, "clear");
    return led_strip_refresh(ctrl->strip);
}

esp_err_t scene_controller_init(scene_controller_t **out_ctrl, const scene_controller_config_t *cfg)
{
    if (!out_ctrl || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_ctrl = NULL;
    scene_controller_t *ctrl = calloc(1, sizeof(scene_controller_t));
    if (!ctrl) {
        return ESP_ERR_NO_MEM;
    }
    ctrl->cfg = *cfg;
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = cfg->led_gpio,
        .max_leds = cfg->led_pixel_count,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
        .with_dma = false,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &ctrl->strip);
    if (err != ESP_OK) {
        free(ctrl);
        return err;
    }
    ESP_LOGI(TAG, "Scene controller initialised (GPIO%d, %d pixels)", cfg->led_gpio, cfg->led_pixel_count);
    *out_ctrl = ctrl;
    return ESP_OK;
}

void scene_controller_deinit(scene_controller_t *ctrl)
{
    if (!ctrl) {
        return;
    }
    if (ctrl->strip) {
        led_strip_clear(ctrl->strip);
        led_strip_refresh(ctrl->strip);
        led_strip_del(ctrl->strip);
    }
    free(ctrl);
}

esp_err_t scene_controller_apply_light_scene(scene_controller_t *ctrl,
                                             const char *scene_id,
                                             uint32_t transition_ms)
{
    if (!ctrl || !scene_id) {
        return ESP_ERR_INVALID_ARG;
    }
    return apply_scene(ctrl, scene_id, transition_ms);
}

esp_err_t scene_controller_set_light_color(scene_controller_t *ctrl,
                                           uint8_t r,
                                           uint8_t g,
                                           uint8_t b,
                                           float brightness,
                                           uint32_t transition_ms)
{
    (void)transition_ms;
    if (!ctrl) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Light color rgb(%u,%u,%u) brightness=%.2f transition=%ums",
             r, g, b, brightness, (unsigned)transition_ms);
    return set_all(ctrl, r, g, b, brightness);
}

esp_err_t scene_controller_play_sound_scene(scene_controller_t *ctrl,
                                            const char *scene_id,
                                            float volume_0_1)
{
    (void)ctrl;
    ESP_LOGI(TAG, "Sound scene '%s' volume=%.2f", scene_id ? scene_id : "<none>", volume_0_1);
    return ESP_OK;
}

esp_err_t scene_controller_play_sound_playlist(scene_controller_t *ctrl,
                                               const char *playlist_id,
                                               float volume_0_1)
{
    (void)ctrl;
    ESP_LOGI(TAG, "Sound playlist '%s' volume=%.2f", playlist_id ? playlist_id : "<none>", volume_0_1);
    return ESP_OK;
}

led_strip_handle_t scene_controller_get_strip(scene_controller_t *ctrl)
{
    if (!ctrl) {
        return NULL;
    }
    return ctrl->strip;
}

int scene_controller_get_pixel_count(scene_controller_t *ctrl)
{
    if (!ctrl) {
        return 0;
    }
    return ctrl->cfg.led_pixel_count;
}

float scene_controller_get_master_brightness(scene_controller_t *ctrl)
{
    if (!ctrl) {
        return 1.0f;
    }
    return ctrl->cfg.master_brightness > 0.0f ? ctrl->cfg.master_brightness : 1.0f;
}
