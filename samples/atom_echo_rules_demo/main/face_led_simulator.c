/**
 * Face LED Simulator Implementation
 * 
 * Renders LED patterns on the NaphomeFace.png image by overlaying
 * LED colors at mapped positions in the oval region.
 */

#include "face_led_simulator.h"
#include "face_led_positions.h"
#include "naphome_face_image.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAG "face_led_sim"

// Helper: Convert RGB888 to RGB565
static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Helper: Blend two RGB565 colors
static uint16_t blend_rgb565(uint16_t base, uint16_t overlay, float alpha) {
    if (alpha <= 0.0f) return base;
    if (alpha >= 1.0f) return overlay;
    
    uint8_t base_r = (base >> 11) & 0x1F;
    uint8_t base_g = (base >> 5) & 0x3F;
    uint8_t base_b = base & 0x1F;
    
    uint8_t over_r = (overlay >> 11) & 0x1F;
    uint8_t over_g = (overlay >> 5) & 0x3F;
    uint8_t over_b = overlay & 0x1F;
    
    uint8_t r = (uint8_t)(base_r + (over_r - base_r) * alpha);
    uint8_t g = (uint8_t)(base_g + (over_g - base_g) * alpha);
    uint8_t b = (uint8_t)(base_b + (over_b - base_b) * alpha);
    
    return rgb888_to_rgb565(r << 3, g << 2, b << 3);
}

// Render LED at position with color and intensity
static void render_led(uint16_t *buffer, int x, int y, uint8_t r, uint8_t g, uint8_t b, float intensity) {
    if (x < 0 || x >= 128 || y < 0 || y >= 128) return;
    
    int idx = y * 128 + x;
    uint16_t led_color = rgb888_to_rgb565(r, g, b);
    
    // Apply intensity and blend with base image
    float alpha = intensity * 0.7f; // 70% opacity for LED glow
    buffer[idx] = blend_rgb565(buffer[idx], led_color, alpha);
    
    // Add glow effect (small radius around LED)
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            if (nx >= 0 && nx < 128 && ny >= 0 && ny < 128) {
                int nidx = ny * 128 + nx;
                float glow_alpha = alpha * 0.3f; // Dimmer glow
                buffer[nidx] = blend_rgb565(buffer[nidx], led_color, glow_alpha);
            }
        }
    }
}

// Render frame with current LED colors
static void render_frame(face_led_simulator_t *sim, uint8_t *led_r, uint8_t *led_g, uint8_t *led_b, float *led_intensity) {
    // Copy base face image
    memcpy(sim->render_buffer, sim->face_image, 128 * 128 * sizeof(uint16_t));
    
    // Render each LED
    for (int i = 0; i < sim->num_leds; i++) {
        if (led_intensity && led_intensity[i] > 0.0f) {
            render_led(sim->render_buffer,
                      sim->led_positions[i].x,
                      sim->led_positions[i].y,
                      led_r[i], led_g[i], led_b[i],
                      led_intensity[i]);
        }
    }
    
    // Draw to display
    display_matrix_draw_bitmap(sim->display, 0, 0, 128, 128, sim->render_buffer);
}

// Pattern task functions
static void pattern_breathing_task(void *arg);
static void pattern_gradient_task(void *arg);
static void pattern_pulse_task(void *arg);

esp_err_t face_led_simulator_init(face_led_simulator_t *simulator,
                                  display_matrix_t *display,
                                  const uint16_t *face_image) {
    if (!simulator || !display || !face_image) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(simulator, 0, sizeof(face_led_simulator_t));
    simulator->display = display;
    simulator->face_image = face_image;
    simulator->num_leds = FACE_LED_COUNT;
    
    // Copy LED positions
    for (int i = 0; i < FACE_LED_COUNT; i++) {
        simulator->led_positions[i].x = face_led_positions[i].x;
        simulator->led_positions[i].y = face_led_positions[i].y;
        simulator->led_positions[i].angle = face_led_positions[i].angle;
    }
    
    // Allocate render buffer
    simulator->render_buffer = (uint16_t*)heap_caps_malloc(128 * 128 * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!simulator->render_buffer) {
        ESP_LOGE(TAG, "Failed to allocate render buffer");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Face LED simulator initialized with %d LEDs", simulator->num_leds);
    return ESP_OK;
}

void face_led_simulator_deinit(face_led_simulator_t *simulator) {
    if (!simulator) return;
    
    face_led_simulator_stop_pattern(simulator);
    
    if (simulator->render_buffer) {
        free(simulator->render_buffer);
        simulator->render_buffer = NULL;
    }
}

esp_err_t face_led_simulator_set_pattern(face_led_simulator_t *simulator,
                                         face_led_pattern_t pattern,
                                         uint8_t r, uint8_t g, uint8_t b,
                                         float intensity) {
    if (!simulator) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Stop current pattern
    face_led_simulator_stop_pattern(simulator);
    
    simulator->current_pattern = pattern;
    simulator->current_r = r;
    simulator->current_g = g;
    simulator->current_b = b;
    simulator->current_intensity = intensity;
    simulator->pattern_active = true;
    
    // Start pattern task based on type
    switch (pattern) {
        case FACE_LED_PATTERN_BREATHING:
            xTaskCreate(pattern_breathing_task, "face_breathing", 4096, simulator, 5, &simulator->pattern_task);
            break;
        case FACE_LED_PATTERN_GRADIENT_RED_BLUE:
        case FACE_LED_PATTERN_GRADIENT_RED_YELLOW:
        case FACE_LED_PATTERN_GRADIENT_WHITE_BLUE:
        case FACE_LED_PATTERN_GRADIENT_BLUE_GREEN:
        case FACE_LED_PATTERN_GRADIENT_TEAL_ORANGE:
            xTaskCreate(pattern_gradient_task, "face_gradient", 4096, simulator, 5, &simulator->pattern_task);
            break;
        case FACE_LED_PATTERN_PULSE_ORANGE:
        case FACE_LED_PATTERN_PULSE_LILAC:
            xTaskCreate(pattern_pulse_task, "face_pulse", 4096, simulator, 5, &simulator->pattern_task);
            break;
        case FACE_LED_PATTERN_NONE:
        default:
            // Solid color - render once
            {
                uint8_t led_r[FACE_LED_COUNT];
                uint8_t led_g[FACE_LED_COUNT];
                uint8_t led_b[FACE_LED_COUNT];
                float led_intensity[FACE_LED_COUNT];
                
                for (int i = 0; i < FACE_LED_COUNT; i++) {
                    led_r[i] = r;
                    led_g[i] = g;
                    led_b[i] = b;
                    led_intensity[i] = intensity;
                }
                
                render_frame(simulator, led_r, led_g, led_b, led_intensity);
            }
            break;
    }
    
    return ESP_OK;
}

void face_led_simulator_stop_pattern(face_led_simulator_t *simulator) {
    if (!simulator) return;
    
    simulator->pattern_active = false;
    
    if (simulator->pattern_task) {
        // Wait for task to finish (with timeout)
        vTaskDelay(pdMS_TO_TICKS(100));
        if (simulator->pattern_task) {
            vTaskDelete(simulator->pattern_task);
            simulator->pattern_task = NULL;
        }
    }
}

// Breathing pattern task
static void pattern_breathing_task(void *arg) {
    face_led_simulator_t *sim = (face_led_simulator_t *)arg;
    if (!sim) vTaskDelete(NULL);
    
    uint8_t r = sim->current_r;
    uint8_t g = sim->current_g;
    uint8_t b = sim->current_b;
    float base_intensity = sim->current_intensity;
    
    float phase = 0.0f;
    const float phase_increment = 0.02f;
    
    uint8_t led_r[FACE_LED_COUNT];
    uint8_t led_g[FACE_LED_COUNT];
    uint8_t led_b[FACE_LED_COUNT];
    float led_intensity[FACE_LED_COUNT];
    
    // Initialize all LEDs to same color
    for (int i = 0; i < FACE_LED_COUNT; i++) {
        led_r[i] = r;
        led_g[i] = g;
        led_b[i] = b;
    }
    
    while (sim->pattern_active) {
        float breath = 0.5f * (1.0f - cosf(phase));
        breath = fmaxf(0.05f, breath);
        float intensity = breath * base_intensity;
        
        // Apply intensity to all LEDs
        for (int i = 0; i < FACE_LED_COUNT; i++) {
            led_intensity[i] = intensity;
        }
        
        render_frame(sim, led_r, led_g, led_b, led_intensity);
        
        phase += phase_increment;
        if (phase >= 2.0f * M_PI) {
            phase = 0.0f;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // ~20 FPS
    }
    
    vTaskDelete(NULL);
}

// Gradient pattern task
static void pattern_gradient_task(void *arg) {
    face_led_simulator_t *sim = (face_led_simulator_t *)arg;
    if (!sim) vTaskDelete(NULL);
    
    uint8_t start_r = 0, start_g = 0, start_b = 0;
    uint8_t end_r = 0, end_g = 0, end_b = 0;
    bool slow_mode = false;
    
    // Set gradient colors based on pattern
    switch (sim->current_pattern) {
        case FACE_LED_PATTERN_GRADIENT_RED_BLUE:
            start_r = 255; start_g = 0; start_b = 0;
            end_r = 0; end_g = 0; end_b = 255;
            slow_mode = false;
            break;
        case FACE_LED_PATTERN_GRADIENT_RED_YELLOW:
            start_r = 255; start_g = 0; start_b = 0;
            end_r = 255; end_g = 255; end_b = 0;
            slow_mode = true;
            break;
        case FACE_LED_PATTERN_GRADIENT_WHITE_BLUE:
            start_r = 255; start_g = 255; start_b = 255;
            end_r = 0; end_g = 0; end_b = 255;
            slow_mode = true;
            break;
        case FACE_LED_PATTERN_GRADIENT_BLUE_GREEN:
            start_r = 0; start_g = 0; start_b = 255;
            end_r = 0; end_g = 255; end_b = 0;
            slow_mode = true;
            break;
        case FACE_LED_PATTERN_GRADIENT_TEAL_ORANGE:
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
    
    uint8_t led_r[FACE_LED_COUNT];
    uint8_t led_g[FACE_LED_COUNT];
    uint8_t led_b[FACE_LED_COUNT];
    float led_intensity[FACE_LED_COUNT];
    
    while (sim->pattern_active) {
        // Calculate color for each LED based on angle and phase
        for (int i = 0; i < FACE_LED_COUNT; i++) {
            float angle = sim->led_positions[i].angle + phase * 2.0f * M_PI;
            float blend = 0.5f * (1.0f + cosf(angle));
            
            led_r[i] = (uint8_t)(start_r + (end_r - start_r) * blend);
            led_g[i] = (uint8_t)(start_g + (end_g - start_g) * blend);
            led_b[i] = (uint8_t)(start_b + (end_b - start_b) * blend);
            led_intensity[i] = sim->current_intensity;
        }
        
        render_frame(sim, led_r, led_g, led_b, led_intensity);
        
        phase += phase_inc;
        if (phase >= 1.0f) {
            phase = 0.0f;
        }
        
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    vTaskDelete(NULL);
}

// Pulse pattern task (4-4-4 breathing)
static void pattern_pulse_task(void *arg) {
    face_led_simulator_t *sim = (face_led_simulator_t *)arg;
    if (!sim) vTaskDelete(NULL);
    
    uint8_t r, g, b;
    if (sim->current_pattern == FACE_LED_PATTERN_PULSE_ORANGE) {
        r = 255; g = 135; b = 0;
    } else { // LILAC
        r = 255; g = 100; b = 255;
    }
    
    const float inhale_duration = 4.0f;
    const float hold_duration = 4.0f;
    const float exhale_duration = 4.0f;
    const float total_cycle = 12.0f;
    
    TickType_t cycle_start = xTaskGetTickCount();
    
    uint8_t led_r[FACE_LED_COUNT];
    uint8_t led_g[FACE_LED_COUNT];
    uint8_t led_b[FACE_LED_COUNT];
    float led_intensity[FACE_LED_COUNT];
    
    // Initialize all LEDs to same color
    for (int i = 0; i < FACE_LED_COUNT; i++) {
        led_r[i] = r;
        led_g[i] = g;
        led_b[i] = b;
    }
    
    while (sim->pattern_active) {
        TickType_t now = xTaskGetTickCount();
        float elapsed = (float)(now - cycle_start) / (float)configTICK_RATE_HZ;
        float cycle_pos = fmodf(elapsed, total_cycle);
        
        float breath_intensity;
        if (cycle_pos <= inhale_duration) {
            float progress = cycle_pos / inhale_duration;
            breath_intensity = 0.5f * (1.0f - cosf(progress * M_PI));
        } else if (cycle_pos <= inhale_duration + hold_duration) {
            breath_intensity = 1.0f;
        } else {
            float progress = (cycle_pos - inhale_duration - hold_duration) / exhale_duration;
            breath_intensity = 0.5f * (1.0f + cosf(progress * M_PI));
        }
        
        breath_intensity = fmaxf(0.05f, breath_intensity);
        float intensity = breath_intensity * sim->current_intensity;
        
        // Apply intensity to all LEDs
        for (int i = 0; i < FACE_LED_COUNT; i++) {
            led_intensity[i] = intensity;
        }
        
        render_frame(sim, led_r, led_g, led_b, led_intensity);
        
        vTaskDelay(pdMS_TO_TICKS(16)); // ~60 FPS
    }
    
    vTaskDelete(NULL);
}
