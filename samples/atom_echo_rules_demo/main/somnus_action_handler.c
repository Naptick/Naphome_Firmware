#include "somnus_action_handler.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "cJSON.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "led_strip.h"
#include "face_led_simulator.h"

#define ACTION_TAG "somnus_action"

// LED pattern state
typedef enum {
    LED_PATTERN_NONE = 0,
    LED_PATTERN_BREATHING,
    LED_PATTERN_GRADIENT_RED_BLUE,
    LED_PATTERN_GRADIENT_RED_YELLOW,
    LED_PATTERN_GRADIENT_WHITE_BLUE,
    LED_PATTERN_GRADIENT_BLUE_GREEN,
    LED_PATTERN_GRADIENT_TEAL_ORANGE,
    LED_PATTERN_PULSE_LILAC,
    LED_PATTERN_PULSE_ORANGE,
} led_pattern_t;

static struct {
    scene_controller_t *led_ctrl;
    face_led_simulator_t *face_simulator;
    led_pattern_t current_pattern;
    uint8_t current_r, current_g, current_b;
    float current_intensity;
    float current_volume;
    bool paused;
    TaskHandle_t pattern_task;
    TimerHandle_t audio_timer;
    bool pattern_active;
} s_state = {0};

// Forward declarations
static esp_err_t handle_led_action(const cJSON *data);
static esp_err_t handle_song_change(const cJSON *data);
static esp_err_t handle_set_volume(const cJSON *data);
static esp_err_t handle_set_led_intensity(const cJSON *data);
static esp_err_t handle_pause(void);
static esp_err_t handle_play(void);
static esp_err_t handle_speech(const cJSON *data);

// LED pattern tasks
static void led_breathing_task(void *arg);
static void led_gradient_task(void *arg);
static void led_pulse_task(void *arg);

void somnus_action_handler_set_face_simulator(face_led_simulator_t *face_simulator)
{
    s_state.face_simulator = face_simulator;
}

esp_err_t somnus_action_handler_process(const char *payload, scene_controller_t *led_controller)
{
    if (!payload) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_state.led_ctrl = led_controller;
    
    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        ESP_LOGE(ACTION_TAG, "Failed to parse JSON payload");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t result = ESP_OK;
    
    // Handle array of actions
    if (cJSON_IsArray(root)) {
        int count = cJSON_GetArraySize(root);
        ESP_LOGI(ACTION_TAG, "Processing action list with %d actions", count);
        
        for (int i = 0; i < count; i++) {
            cJSON *action = cJSON_GetArrayItem(root, i);
            if (!cJSON_IsObject(action)) {
                continue;
            }
            
            cJSON *delay = cJSON_GetObjectItem(action, "Delay");
            if (cJSON_IsNumber(delay) && delay->valueint > 0) {
                vTaskDelay(pdMS_TO_TICKS(delay->valueint * 1000));
            }
            
            cJSON *action_type = cJSON_GetObjectItem(action, "Action");
            if (!cJSON_IsString(action_type)) {
                continue;
            }
            
            cJSON *data = cJSON_GetObjectItem(action, "Data");
            
            const char *action_str = action_type->valuestring;
            ESP_LOGI(ACTION_TAG, "Executing action: %s", action_str);
            
            if (strcmp(action_str, "LED") == 0) {
                handle_led_action(data);
            } else if (strcmp(action_str, "SongChange") == 0) {
                handle_song_change(data);
            } else if (strcmp(action_str, "SetVolume") == 0) {
                handle_set_volume(data);
            } else if (strcmp(action_str, "SetLEDIntensity") == 0) {
                handle_set_led_intensity(data);
            } else if (strcmp(action_str, "Pause") == 0) {
                handle_pause();
            } else if (strcmp(action_str, "Play") == 0) {
                handle_play();
            } else if (strcmp(action_str, "Speech") == 0) {
                handle_speech(data);
            } else {
                ESP_LOGW(ACTION_TAG, "Unknown action type: %s", action_str);
            }
        }
    }
    // Handle single action
    else if (cJSON_IsObject(root)) {
        cJSON *action_type = cJSON_GetObjectItem(root, "Action");
        if (cJSON_IsString(action_type)) {
            cJSON *data = cJSON_GetObjectItem(root, "Data");
            const char *action_str = action_type->valuestring;
            ESP_LOGI(ACTION_TAG, "Executing single action: %s", action_str);
            
            if (strcmp(action_str, "LED") == 0) {
                result = handle_led_action(data);
            } else if (strcmp(action_str, "SongChange") == 0) {
                result = handle_song_change(data);
            } else if (strcmp(action_str, "SetVolume") == 0) {
                result = handle_set_volume(data);
            } else if (strcmp(action_str, "SetLEDIntensity") == 0) {
                result = handle_set_led_intensity(data);
            } else if (strcmp(action_str, "Pause") == 0) {
                result = handle_pause();
            } else if (strcmp(action_str, "Play") == 0) {
                result = handle_play();
            } else if (strcmp(action_str, "Speech") == 0) {
                result = handle_speech(data);
            } else {
                ESP_LOGW(ACTION_TAG, "Unknown action type: %s", action_str);
                result = ESP_ERR_NOT_SUPPORTED;
            }
        }
    }
    
    cJSON_Delete(root);
    return result;
}

static void stop_current_pattern(void)
{
    s_state.pattern_active = false;
    if (s_state.pattern_task) {
        vTaskDelete(s_state.pattern_task);
        s_state.pattern_task = NULL;
    }
}

static esp_err_t handle_led_action(const cJSON *data)
{
    if (!data || !s_state.led_ctrl) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Stop current pattern
    stop_current_pattern();
    
    // Get pattern
    cJSON *pattern_json = cJSON_GetObjectItem(data, "Pattern");
    const char *pattern_str = pattern_json && cJSON_IsString(pattern_json) 
        ? pattern_json->valuestring : "none";
    
    // Get color
    cJSON *color_array = cJSON_GetObjectItem(data, "Color");
    uint8_t r = 0, g = 0, b = 0;
    if (cJSON_IsArray(color_array) && cJSON_GetArraySize(color_array) >= 3) {
        cJSON *r_json = cJSON_GetArrayItem(color_array, 0);
        cJSON *g_json = cJSON_GetArrayItem(color_array, 1);
        cJSON *b_json = cJSON_GetArrayItem(color_array, 2);
        if (cJSON_IsNumber(r_json)) r = (uint8_t)cJSON_GetNumberValue(r_json);
        if (cJSON_IsNumber(g_json)) g = (uint8_t)cJSON_GetNumberValue(g_json);
        if (cJSON_IsNumber(b_json)) b = (uint8_t)cJSON_GetNumberValue(b_json);
    }
    
    // Get intensity
    cJSON *intensity_json = cJSON_GetObjectItem(data, "Intensity");
    if (cJSON_IsNumber(intensity_json)) {
        s_state.current_intensity = (float)cJSON_GetNumberValue(intensity_json);
    }
    
    // Get duration
    cJSON *duration_json = cJSON_GetObjectItem(data, "TotalDuration");
    int duration = duration_json && cJSON_IsNumber(duration_json) 
        ? (int)cJSON_GetNumberValue(duration_json) : 900;
    
    s_state.current_r = r;
    s_state.current_g = g;
    s_state.current_b = b;
    
    ESP_LOGI(ACTION_TAG, "LED: pattern=%s color=[%u,%u,%u] intensity=%.2f duration=%d",
             pattern_str, r, g, b, s_state.current_intensity, duration);
    
    // Update face LED simulator if available
    face_led_pattern_t face_pattern = FACE_LED_PATTERN_NONE;
    
    // Check for special color triggers (from LED.py)
    if (r == 255 && g == 150 && b == 150) {
        // Red-Blue gradient (normal speed)
        s_state.current_pattern = LED_PATTERN_GRADIENT_RED_BLUE;
        face_pattern = FACE_LED_PATTERN_GRADIENT_RED_BLUE;
        s_state.pattern_active = true;
        xTaskCreate(led_gradient_task, "led_grad_rb", 4096, NULL, 5, &s_state.pattern_task);
        if (s_state.face_simulator) {
            face_led_simulator_set_pattern(s_state.face_simulator, face_pattern, r, g, b, s_state.current_intensity);
        }
        return ESP_OK;
    } else if (r == 220 && g == 38 && b == 38) {
        // Red-Yellow gradient (slow)
        s_state.current_pattern = LED_PATTERN_GRADIENT_RED_YELLOW;
        face_pattern = FACE_LED_PATTERN_GRADIENT_RED_YELLOW;
        s_state.pattern_active = true;
        xTaskCreate(led_gradient_task, "led_grad_ry", 4096, NULL, 5, &s_state.pattern_task);
        if (s_state.face_simulator) {
            face_led_simulator_set_pattern(s_state.face_simulator, face_pattern, r, g, b, s_state.current_intensity);
        }
        return ESP_OK;
    } else if (r == 14 && g == 165 && b == 233) {
        // White-Blue gradient (slow)
        s_state.current_pattern = LED_PATTERN_GRADIENT_WHITE_BLUE;
        face_pattern = FACE_LED_PATTERN_GRADIENT_WHITE_BLUE;
        s_state.pattern_active = true;
        xTaskCreate(led_gradient_task, "led_grad_wb", 4096, NULL, 5, &s_state.pattern_task);
        if (s_state.face_simulator) {
            face_led_simulator_set_pattern(s_state.face_simulator, face_pattern, r, g, b, s_state.current_intensity);
        }
        return ESP_OK;
    } else if (r == 6 && g == 182 && b == 212) {
        // Blue-Green gradient (slow)
        s_state.current_pattern = LED_PATTERN_GRADIENT_BLUE_GREEN;
        face_pattern = FACE_LED_PATTERN_GRADIENT_BLUE_GREEN;
        s_state.pattern_active = true;
        xTaskCreate(led_gradient_task, "led_grad_bg", 4096, NULL, 5, &s_state.pattern_task);
        if (s_state.face_simulator) {
            face_led_simulator_set_pattern(s_state.face_simulator, face_pattern, r, g, b, s_state.current_intensity);
        }
        return ESP_OK;
    } else if (r == 244 && g == 114 && b == 182) {
        // Dark Teal-Orange gradient (slow)
        s_state.current_pattern = LED_PATTERN_GRADIENT_TEAL_ORANGE;
        face_pattern = FACE_LED_PATTERN_GRADIENT_TEAL_ORANGE;
        s_state.pattern_active = true;
        xTaskCreate(led_gradient_task, "led_grad_to", 4096, NULL, 5, &s_state.pattern_task);
        if (s_state.face_simulator) {
            face_led_simulator_set_pattern(s_state.face_simulator, face_pattern, r, g, b, s_state.current_intensity);
        }
        return ESP_OK;
    } else if (r == 255 && g == 135 && b == 0) {
        // Orange pulse
        s_state.current_pattern = LED_PATTERN_PULSE_ORANGE;
        face_pattern = FACE_LED_PATTERN_PULSE_ORANGE;
        s_state.pattern_active = true;
        xTaskCreate(led_pulse_task, "led_pulse_orange", 4096, NULL, 5, &s_state.pattern_task);
        if (s_state.face_simulator) {
            face_led_simulator_set_pattern(s_state.face_simulator, face_pattern, r, g, b, s_state.current_intensity);
        }
        return ESP_OK;
    } else if (r == 255 && g == 100 && b == 255) {
        // Lilac pulse
        s_state.current_pattern = LED_PATTERN_PULSE_LILAC;
        face_pattern = FACE_LED_PATTERN_PULSE_LILAC;
        s_state.pattern_active = true;
        xTaskCreate(led_pulse_task, "led_pulse_lilac", 4096, NULL, 5, &s_state.pattern_task);
        if (s_state.face_simulator) {
            face_led_simulator_set_pattern(s_state.face_simulator, face_pattern, r, g, b, s_state.current_intensity);
        }
        return ESP_OK;
    }
    
    // Handle standard patterns
    if (strcasecmp(pattern_str, "breathing") == 0) {
        s_state.current_pattern = LED_PATTERN_BREATHING;
        face_pattern = FACE_LED_PATTERN_BREATHING;
        s_state.pattern_active = true;
        xTaskCreate(led_breathing_task, "led_breathing", 4096, NULL, 5, &s_state.pattern_task);
        if (s_state.face_simulator) {
            face_led_simulator_set_pattern(s_state.face_simulator, face_pattern, r, g, b, s_state.current_intensity);
        }
    } else if (strcasecmp(pattern_str, "none") == 0) {
        // Solid color
        s_state.current_pattern = LED_PATTERN_NONE;
        face_pattern = FACE_LED_PATTERN_NONE;
        esp_err_t err = scene_controller_set_light_color(s_state.led_ctrl, r, g, b, 
                                                s_state.current_intensity, 2000);
        if (s_state.face_simulator) {
            face_led_simulator_set_pattern(s_state.face_simulator, face_pattern, r, g, b, s_state.current_intensity);
        }
        return err;
    } else {
        // Default to solid color
        s_state.current_pattern = LED_PATTERN_NONE;
        face_pattern = FACE_LED_PATTERN_NONE;
        esp_err_t err = scene_controller_set_light_color(s_state.led_ctrl, r, g, b, 
                                                s_state.current_intensity, 2000);
        if (s_state.face_simulator) {
            face_led_simulator_set_pattern(s_state.face_simulator, face_pattern, r, g, b, s_state.current_intensity);
        }
        return err;
    }
    
    return ESP_OK;
}

static void led_breathing_task(void *arg)
{
    (void)arg;
    scene_controller_t *ctrl = s_state.led_ctrl;
    led_strip_handle_t strip = scene_controller_get_strip(ctrl);
    if (!ctrl || !strip) {
        vTaskDelete(NULL);
        return;
    }
    
    uint8_t r = s_state.current_r;
    uint8_t g = s_state.current_g;
    uint8_t b = s_state.current_b;
    float intensity = s_state.current_intensity;
    
    float phase = 0.0f;
    const float phase_increment = 0.02f; // Breathing speed
    
    while (s_state.pattern_active) {
        // Cosine-based breathing: 0.5 * (1 - cos(phase)) gives 0.0 to 1.0
        float breath = 0.5f * (1.0f - cosf(phase));
        breath = fmaxf(0.05f, breath); // Minimum brightness
        
        float brightness = breath * intensity;
        
        scene_controller_set_light_color(ctrl, r, g, b, brightness, 0);
        
        phase += phase_increment;
        if (phase >= 2.0f * M_PI) {
            phase = 0.0f;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // ~20 FPS
    }
    
    vTaskDelete(NULL);
}

static void led_gradient_task(void *arg)
{
    (void)arg;
    scene_controller_t *ctrl = s_state.led_ctrl;
    led_strip_handle_t strip = scene_controller_get_strip(ctrl);
    if (!ctrl || !strip) {
        vTaskDelete(NULL);
        return;
    }
    
    uint8_t start_r = 0, start_g = 0, start_b = 0;
    uint8_t end_r = 0, end_g = 0, end_b = 0;
    bool slow_mode = false;
    
    // Set gradient colors based on pattern
    switch (s_state.current_pattern) {
        case LED_PATTERN_GRADIENT_RED_BLUE:
            start_r = 255; start_g = 0; start_b = 0;
            end_r = 0; end_g = 0; end_b = 255;
            slow_mode = false;
            break;
        case LED_PATTERN_GRADIENT_RED_YELLOW:
            start_r = 255; start_g = 0; start_b = 0;
            end_r = 255; end_g = 255; end_b = 0;
            slow_mode = true;
            break;
        case LED_PATTERN_GRADIENT_WHITE_BLUE:
            start_r = 255; start_g = 255; start_b = 255;
            end_r = 0; end_g = 0; end_b = 255;
            slow_mode = true;
            break;
        case LED_PATTERN_GRADIENT_BLUE_GREEN:
            start_r = 0; start_g = 0; start_b = 255;
            end_r = 0; end_g = 255; end_b = 0;
            slow_mode = true;
            break;
        case LED_PATTERN_GRADIENT_TEAL_ORANGE:
            start_r = 0; start_g = 100; start_b = 100;
            end_r = 255; end_g = 140; end_b = 0;
            slow_mode = true;
            break;
        default:
            vTaskDelete(NULL);
            return;
    }
    
    float phase = 0.0f;
    float phase_inc = slow_mode ? 0.005f : 0.01f;
    int delay_ms = slow_mode ? 30 : 20;
    
    int num_leds = scene_controller_get_pixel_count(ctrl);
    
    while (s_state.pattern_active) {
        for (int i = 0; i < num_leds; i++) {
            // Calculate angle for each LED (circular pattern)
            float angle = 2.0f * M_PI * ((float)i / (float)num_leds) + phase;
            float blend = 0.5f * (1.0f + cosf(angle));
            
            uint8_t r = (uint8_t)(start_r + (end_r - start_r) * blend);
            uint8_t g = (uint8_t)(start_g + (end_g - start_g) * blend);
            uint8_t b = (uint8_t)(start_b + (end_b - start_b) * blend);
            
            float brightness = s_state.current_intensity;
            float master = scene_controller_get_master_brightness(ctrl);
            float factor = brightness * master;
            if (factor > 1.0f) factor = 1.0f;
            if (factor < 0.01f) factor = 0.01f;
            
            uint32_t rr = (uint32_t)(r * factor);
            uint32_t gg = (uint32_t)(g * factor);
            uint32_t bb = (uint32_t)(b * factor);
            led_strip_set_pixel(strip, i, rr, gg, bb);
        }
        led_strip_refresh(strip);
        
        phase += phase_inc;
        if (phase >= 1.0f) {
            phase = 0.0f;
        }
        
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    vTaskDelete(NULL);
}

static void led_pulse_task(void *arg)
{
    (void)arg;
    scene_controller_t *ctrl = s_state.led_ctrl;
    led_strip_handle_t strip = scene_controller_get_strip(ctrl);
    if (!ctrl || !strip) {
        vTaskDelete(NULL);
        return;
    }
    
    uint8_t r, g, b;
    if (s_state.current_pattern == LED_PATTERN_PULSE_ORANGE) {
        r = 255; g = 135; b = 0;
    } else { // LILAC
        r = 255; g = 100; b = 255;
    }
    
    // 4-4-4 breathing pattern: 4s inhale, 4s hold, 4s exhale = 12s cycle
    const float inhale_duration = 4.0f;
    const float hold_duration = 4.0f;
    const float exhale_duration = 4.0f;
    const float total_cycle = 12.0f;
    
    TickType_t cycle_start = xTaskGetTickCount();
    
    while (s_state.pattern_active) {
        TickType_t now = xTaskGetTickCount();
        float elapsed = (float)(now - cycle_start) / (float)configTICK_RATE_HZ;
        float cycle_pos = fmodf(elapsed, total_cycle);
        
        float breath_intensity;
        if (cycle_pos <= inhale_duration) {
            // Inhale
            float progress = cycle_pos / inhale_duration;
            breath_intensity = 0.5f * (1.0f - cosf(progress * M_PI));
        } else if (cycle_pos <= inhale_duration + hold_duration) {
            // Hold
            breath_intensity = 1.0f;
        } else {
            // Exhale
            float progress = (cycle_pos - inhale_duration - hold_duration) / exhale_duration;
            breath_intensity = 0.5f * (1.0f + cosf(progress * M_PI));
        }
        
        breath_intensity = fmaxf(0.05f, breath_intensity);
        float brightness = breath_intensity * s_state.current_intensity;
        
        scene_controller_set_light_color(ctrl, r, g, b, brightness, 0);
        
        vTaskDelay(pdMS_TO_TICKS(16)); // ~60 FPS
    }
    
    vTaskDelete(NULL);
}

static esp_err_t handle_song_change(const cJSON *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *song_name_json = cJSON_GetObjectItem(data, "SongName");
    if (!cJSON_IsString(song_name_json)) {
        ESP_LOGE(ACTION_TAG, "SongChange missing SongName");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *song_name = song_name_json->valuestring;
    cJSON *volume_json = cJSON_GetObjectItem(data, "Volume");
    float volume = volume_json && cJSON_IsNumber(volume_json) 
        ? (float)cJSON_GetNumberValue(volume_json) : 0.6f;
    
    cJSON *duration_json = cJSON_GetObjectItem(data, "Duration");
    int duration = duration_json && cJSON_IsNumber(duration_json)
        ? (int)cJSON_GetNumberValue(duration_json) : 0;
    
    s_state.current_volume = volume;
    
    ESP_LOGI(ACTION_TAG, "SongChange: '%s' volume=%.2f duration=%d", song_name, volume, duration);
    
    // TODO: Implement audio playback
    // For now, just log the request
    ESP_LOGW(ACTION_TAG, "Audio playback not yet implemented - requested: %s", song_name);
    
    return ESP_OK;
}

static esp_err_t handle_set_volume(const cJSON *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *volume_json = cJSON_GetObjectItem(data, "Volume");
    if (cJSON_IsNumber(volume_json)) {
        s_state.current_volume = (float)cJSON_GetNumberValue(volume_json);
        s_state.current_volume = fmaxf(0.0f, fminf(1.0f, s_state.current_volume));
        ESP_LOGI(ACTION_TAG, "SetVolume: %.2f", s_state.current_volume);
        // TODO: Apply to audio player
        return ESP_OK;
    }
    
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t handle_set_led_intensity(const cJSON *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *intensity_json = cJSON_GetObjectItem(data, "Intensity");
    if (cJSON_IsNumber(intensity_json)) {
        s_state.current_intensity = (float)cJSON_GetNumberValue(intensity_json);
        s_state.current_intensity = fmaxf(0.0f, fminf(1.0f, s_state.current_intensity));
        ESP_LOGI(ACTION_TAG, "SetLEDIntensity: %.2f", s_state.current_intensity);
        
        // Update current LED color with new intensity
        if (s_state.led_ctrl && s_state.current_pattern == LED_PATTERN_NONE) {
            return scene_controller_set_light_color(s_state.led_ctrl, 
                                                     s_state.current_r, 
                                                     s_state.current_g, 
                                                     s_state.current_b,
                                                     s_state.current_intensity, 500);
        }
        return ESP_OK;
    }
    
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t handle_pause(void)
{
    ESP_LOGI(ACTION_TAG, "Pause command received");
    s_state.paused = true;
    stop_current_pattern();
    
    // Clear LEDs
    led_strip_handle_t strip = scene_controller_get_strip(s_state.led_ctrl);
    if (strip) {
        led_strip_clear(strip);
        led_strip_refresh(strip);
    }
    
    // TODO: Pause audio playback
    ESP_LOGW(ACTION_TAG, "Audio pause not yet implemented");
    
    return ESP_OK;
}

static esp_err_t handle_play(void)
{
    ESP_LOGI(ACTION_TAG, "Play command received");
    s_state.paused = false;
    
    // TODO: Resume audio playback
    ESP_LOGW(ACTION_TAG, "Audio resume not yet implemented");
    
    // Restore LED state if we have one
    if (s_state.current_pattern != LED_PATTERN_NONE) {
        // Re-trigger the pattern
        cJSON *dummy_data = cJSON_CreateObject();
        cJSON *color = cJSON_CreateArray();
        cJSON_AddItemToArray(color, cJSON_CreateNumber(s_state.current_r));
        cJSON_AddItemToArray(color, cJSON_CreateNumber(s_state.current_g));
        cJSON_AddItemToArray(color, cJSON_CreateNumber(s_state.current_b));
        cJSON_AddItemToObject(dummy_data, "Color", color);
        cJSON_AddItemToObject(dummy_data, "Intensity", cJSON_CreateNumber(s_state.current_intensity));
        handle_led_action(dummy_data);
        cJSON_Delete(dummy_data);
    }
    
    return ESP_OK;
}

static esp_err_t handle_speech(const cJSON *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *text_json = cJSON_GetObjectItem(data, "Text");
    if (!cJSON_IsString(text_json)) {
        ESP_LOGE(ACTION_TAG, "Speech missing Text");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *text = text_json->valuestring;
    ESP_LOGI(ACTION_TAG, "Speech: '%s'", text);
    
    // TODO: Implement TTS
    ESP_LOGW(ACTION_TAG, "TTS not yet implemented - requested: %s", text);
    
    return ESP_OK;
}
