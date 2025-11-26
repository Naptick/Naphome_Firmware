#include "wake_word_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

// Forward declaration for LED update callback
extern void update_mic_leds(float mic1_level, float mic2_level, float mic3_level);

struct wake_word_service {
    wake_word_service_config_t cfg;
    wake_word_callback_t callback;
    void *callback_ctx;
    TimerHandle_t simulated_timer;
    TaskHandle_t task;
    int16_t *frame_buffer;
    size_t frame_samples;
    int activation_frames;
    int frames_over_threshold;
    float noise_floor;
    float energy_offset;
    TickType_t cooldown_ticks;
    TickType_t resume_from_tick;
    bool stop_requested;
    bool calibrating;
    int calibration_samples;
    float calibration_sum;
    int calibration_count;
};

static const char *TAG = "wake_word";
static const size_t WAKE_WORD_TASK_STACK = 4096;
static const int WAKE_WORD_TASK_PRIORITY = 4;
static const BaseType_t WAKE_WORD_TASK_CORE = 1;
static const size_t WAKE_WORD_DEFAULT_FRAME_SAMPLES = 512;
static const int WAKE_WORD_DEFAULT_FRAMES = 4;
static const int WAKE_WORD_DEFAULT_COOLDOWN_MS = 2500;
static const int WAKE_WORD_CALIBRATION_SAMPLES = 200;  // ~1 second at 5ms per frame
static const float WAKE_WORD_MIN_NOISE_FLOOR = 100.0f;  // Minimum noise floor to prevent false positives

static inline int clamp_int(int value, int min, int max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static float energy_offset_for_sensitivity(int sensitivity)
{
    int clamped = clamp_int(sensitivity, 0, 100);
    const float min_offset = 600.0f;
    const float max_offset = 12000.0f;
    float scaled = (float)clamped / 100.0f;
    return min_offset + scaled * (max_offset - min_offset);
}

static float compute_frame_level(const int16_t *samples, size_t sample_count)
{
    if (!samples || sample_count == 0) {
        return 0.0f;
    }
    uint64_t sum = 0;
    for (size_t i = 0; i < sample_count; ++i) {
        int32_t val = samples[i];
        sum += val >= 0 ? val : -val;
    }
    // Prevent divide-by-zero
    if (sample_count == 0) {
        return 0.0f;
    }
    return (float)sum / (float)sample_count;
}

// Compute energy levels for each microphone channel in STEREO audio
// STEREO format: samples are interleaved [L, R, L, R, ...]
// Korvo-1 has 3 microphones: we extract left, right, and combined for far-field processing
static void compute_mic_levels(const int16_t *samples, size_t sample_count, 
                                float *mic1_level, float *mic2_level, float *mic3_level)
{
    if (!samples || sample_count == 0) {
        *mic1_level = 0.0f;
        *mic2_level = 0.0f;
        *mic3_level = 0.0f;
        return;
    }
    
    uint64_t sum1 = 0, sum2 = 0, sum3 = 0;
    size_t count1 = 0, count2 = 0, count3 = 0;
    
    // For STEREO: samples are interleaved [L, R, L, R, ...]
    // MIC1 = Left channel (first microphone)
    // MIC2 = Right channel (second microphone)  
    // MIC3 = Combined/averaged signal for far-field beamforming effect
    for (size_t i = 0; i < sample_count; i += 2) {
        if (i < sample_count) {
            int32_t val1 = samples[i]; // Left channel (MIC1)
            int32_t abs_val1 = val1 >= 0 ? val1 : -val1;
            sum1 += abs_val1;
            count1++;
            sum3 += abs_val1; // Include in combined
            count3++;
        }
        if (i + 1 < sample_count) {
            int32_t val2 = samples[i + 1]; // Right channel (MIC2)
            int32_t abs_val2 = val2 >= 0 ? val2 : -val2;
            sum2 += abs_val2;
            count2++;
            sum3 += abs_val2; // Include in combined
            count3++;
        }
    }
    
    *mic1_level = count1 > 0 ? (float)sum1 / (float)count1 : 0.0f;
    *mic2_level = count2 > 0 ? (float)sum2 / (float)count2 : 0.0f;
    *mic3_level = count3 > 0 ? (float)sum3 / (float)count3 : 0.0f; // Combined average for far-field
}

static void simulated_timer_cb(TimerHandle_t timer)
{
    wake_word_service_t *service = (wake_word_service_t *)pvTimerGetTimerID(timer);
    if (!service || !service->callback) {
        return;
    }
    ESP_LOGI(TAG, "Simulated wake word detected");
    if (service->cfg.aws_bridge) {
        aws_iot_bridge_record_wake(service->cfg.aws_bridge, true);
    }
    service->callback(service->callback_ctx);
}

static void wake_word_task(void *arg)
{
    wake_word_service_t *service = (wake_word_service_t *)arg;
    const TickType_t frame_delay = pdMS_TO_TICKS(5);
    while (!service->stop_requested) {
        if (!service->cfg.audio) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        if (service->resume_from_tick != 0 && now < service->resume_from_tick) {
            vTaskDelay(frame_delay);
            continue;
        } else if (service->resume_from_tick != 0 && now >= service->resume_from_tick) {
            service->resume_from_tick = 0;
        }

        esp_err_t lock_err = korvo_audio_acquire(service->cfg.audio, pdMS_TO_TICKS(20));
        if (lock_err != ESP_OK) {
            vTaskDelay(frame_delay);
            continue;
        }

        size_t read = 0;
        esp_err_t read_err = korvo_audio_capture_locked(service->cfg.audio,
                                                        service->frame_buffer,
                                                        service->frame_samples,
                                                        &read,
                                                        pdMS_TO_TICKS(50));
        korvo_audio_release(service->cfg.audio);
        if (read_err != ESP_OK || read == 0) {
            vTaskDelay(frame_delay);
            continue;
        }

        float level = compute_frame_level(service->frame_buffer, read);
        
        // Compute individual microphone channel levels for LED visualization
        float mic1_level = 0.0f, mic2_level = 0.0f, mic3_level = 0.0f;
        compute_mic_levels(service->frame_buffer, read, &mic1_level, &mic2_level, &mic3_level);
        
        // Update microphone LEDs (callback to main)
        update_mic_leds(mic1_level, mic2_level, mic3_level);
        
        // Calibration period: collect samples to establish baseline noise floor
        if (service->calibrating) {
            service->calibration_sum += level;
            service->calibration_count++;
            
            // Log calibration progress every 50 samples
            if (service->calibration_count > 0 && service->calibration_count % 50 == 0) {
                float current_avg = service->calibration_count > 0 ? service->calibration_sum / (float)service->calibration_count : 0.0f;
                ESP_LOGI(TAG, "Wake word: Calibrating... (%d/%d samples, current avg=%.0f, current level=%.0f)",
                         service->calibration_count, service->calibration_samples, current_avg, level);
            }
            
            if (service->calibration_count >= service->calibration_samples && service->calibration_count > 0) {
                // Calculate average noise floor from calibration samples
                float raw_avg = service->calibration_count > 0 ? service->calibration_sum / (float)service->calibration_count : 0.0f;
                service->noise_floor = raw_avg;
                
                // Ensure minimum noise floor to prevent false positives
                if (service->noise_floor < WAKE_WORD_MIN_NOISE_FLOOR) {
                    ESP_LOGW(TAG, "Wake word: Noise floor too low (%.0f), using minimum %.0f",
                             raw_avg, WAKE_WORD_MIN_NOISE_FLOOR);
                    service->noise_floor = WAKE_WORD_MIN_NOISE_FLOOR;
                }
                
                service->calibrating = false;
                float final_threshold = service->noise_floor + service->energy_offset;
                ESP_LOGI(TAG, "Wake word: *** Calibration complete ***");
                ESP_LOGI(TAG, "Wake word:   Noise floor: %.0f (from %d samples, raw avg=%.0f)",
                         service->noise_floor, service->calibration_count, raw_avg);
                ESP_LOGI(TAG, "Wake word:   Energy offset: %.0f (sensitivity=%d)",
                         service->energy_offset, service->cfg.sensitivity);
                ESP_LOGI(TAG, "Wake word:   Detection threshold: %.0f (noise + offset)",
                         final_threshold);
                ESP_LOGI(TAG, "Wake word:   Will trigger when level > %.0f for %d consecutive frames",
                         final_threshold, service->activation_frames);
            } else {
                // Still calibrating, skip detection
                vTaskDelay(frame_delay);
                continue;
            }
        }
        
        // Fallback: if noise floor not set (shouldn't happen after calibration)
        if (service->noise_floor <= 0.0f) {
            service->noise_floor = level > WAKE_WORD_MIN_NOISE_FLOOR ? level : WAKE_WORD_MIN_NOISE_FLOOR;
            ESP_LOGW(TAG, "Wake word: WARNING - Noise floor not calibrated, using fallback value %.0f (current level=%.0f)",
                     service->noise_floor, level);
        }

        float threshold = service->noise_floor + service->energy_offset;
        
        // Debug logging every 50 frames (~0.25 seconds at 5ms per frame) for more frequent updates
        static int debug_counter = 0;
        debug_counter++;
        if (debug_counter % 50 == 0) {
            ESP_LOGI(TAG, "Wake word: level=%.0f, noise=%.0f, threshold=%.0f, offset=%.0f, frames_over=%d/%d, diff=%.0f",
                     level, service->noise_floor, threshold, service->energy_offset,
                     service->frames_over_threshold, service->activation_frames,
                     level - threshold);
        }
        
        if (level <= threshold) {
            float old_noise = service->noise_floor;
            service->noise_floor = service->noise_floor * 0.98f + level * 0.02f;
            if (service->frames_over_threshold > 0) {
                ESP_LOGI(TAG, "Wake word: Level dropped below threshold (%.0f <= %.0f), resetting counter. Noise floor: %.0f -> %.0f",
                         level, threshold, old_noise, service->noise_floor);
            }
            service->frames_over_threshold = 0;
        } else {
            float diff = level - threshold;
            service->frames_over_threshold++;
            if (service->frames_over_threshold == 1) {
                ESP_LOGI(TAG, "Wake word: *** Level exceeded threshold! *** (%.0f > %.0f, diff=+%.0f, frames_over=%d/%d)",
                         level, threshold, diff, service->frames_over_threshold, service->activation_frames);
            } else if (service->frames_over_threshold == 2) {
                ESP_LOGI(TAG, "Wake word: Still over threshold (frames_over=%d/%d, level=%.0f, diff=+%.0f)",
                         service->frames_over_threshold, service->activation_frames, level, diff);
            } else if (service->frames_over_threshold == service->activation_frames - 1) {
                ESP_LOGI(TAG, "Wake word: Almost there! (frames_over=%d/%d, level=%.0f, diff=+%.0f)",
                         service->frames_over_threshold, service->activation_frames, level, diff);
            } else if (service->frames_over_threshold % 2 == 0 && service->frames_over_threshold > 2) {
                ESP_LOGI(TAG, "Wake word: Continuing over threshold (frames_over=%d/%d, level=%.0f, diff=+%.0f)",
                         service->frames_over_threshold, service->activation_frames, level, diff);
            }
        }

        // Only trigger wake word if not calibrating
        if (!service->calibrating && service->frames_over_threshold >= service->activation_frames) {
            service->frames_over_threshold = 0;
            service->resume_from_tick = xTaskGetTickCount() + service->cooldown_ticks;
            if (service->cfg.aws_bridge) {
                aws_iot_bridge_record_wake(service->cfg.aws_bridge, false);
            }
            ESP_LOGI(TAG,
                     "*** WAKE WORD DETECTED *** energy=%.0f, noise=%.0f, threshold=%.0f, offset=%.0f",
                     level,
                     service->noise_floor,
                     threshold,
                     service->energy_offset);
            if (service->callback) {
                service->callback(service->callback_ctx);
            }
        }

        vTaskDelay(frame_delay);
    }

    service->task = NULL;
    vTaskDelete(NULL);
}

wake_word_service_t *wake_word_service_start(const wake_word_service_config_t *cfg,
                                             wake_word_callback_t cb,
                                             void *cb_ctx)
{
    ESP_RETURN_ON_FALSE(cfg && cb, NULL, TAG, "invalid args");
    wake_word_service_t *service = calloc(1, sizeof(wake_word_service_t));
    if (!service) {
        return NULL;
    }
    service->cfg = *cfg;
    service->callback = cb;
    service->callback_ctx = cb_ctx;

    service->frame_samples = cfg->frame_samples > 0 ? cfg->frame_samples : WAKE_WORD_DEFAULT_FRAME_SAMPLES;
    service->activation_frames = cfg->activation_frames > 0 ? cfg->activation_frames : WAKE_WORD_DEFAULT_FRAMES;
    int cooldown_ms = cfg->cooldown_ms > 0 ? cfg->cooldown_ms : WAKE_WORD_DEFAULT_COOLDOWN_MS;
    service->cooldown_ticks = pdMS_TO_TICKS(cooldown_ms);
    service->energy_offset = energy_offset_for_sensitivity(cfg->sensitivity);
    
    // Initialize calibration
    service->calibrating = true;
    service->calibration_samples = WAKE_WORD_CALIBRATION_SAMPLES;
    service->calibration_sum = 0.0f;
    service->calibration_count = 0;
    service->noise_floor = 0.0f;  // Will be set after calibration

    if (cfg->simulated_interval_ms > 0) {
        service->simulated_timer = xTimerCreate("wake_sim",
                                                pdMS_TO_TICKS(cfg->simulated_interval_ms),
                                                pdTRUE,
                                                service,
                                                simulated_timer_cb);
        if (service->simulated_timer) {
            xTimerStart(service->simulated_timer, 0);
            ESP_LOGI(TAG, "Simulated wake timer every %d ms", cfg->simulated_interval_ms);
        }
    }

    if (cfg->audio) {
        service->frame_buffer = malloc(service->frame_samples * sizeof(int16_t));
        if (!service->frame_buffer) {
            ESP_LOGE(TAG, "Failed to allocate wake-word frame buffer");
            wake_word_service_stop(service);
            return NULL;
        }
        BaseType_t rc = xTaskCreatePinnedToCore(wake_word_task,
                                                "wake_word",
                                                WAKE_WORD_TASK_STACK,
                                                service,
                                                WAKE_WORD_TASK_PRIORITY,
                                                &service->task,
                                                WAKE_WORD_TASK_CORE);
        if (rc != pdPASS) {
            ESP_LOGE(TAG, "Wake-word task creation failed");
            wake_word_service_stop(service);
            return NULL;
        }
        ESP_LOGI(TAG,
                 "Wake-word listener ready (frame=%d, frames=%d, cooldown=%d ms, offset=%.0f, sensitivity=%d)",
                 (int)service->frame_samples,
                 service->activation_frames,
                 cooldown_ms,
                 service->energy_offset,
                 cfg->sensitivity);
        ESP_LOGI(TAG, "Wake word: Calibrating noise floor for %d samples (~%.1f seconds)...",
                 service->calibration_samples, (service->calibration_samples * 5.0f) / 1000.0f);
        ESP_LOGI(TAG, "Wake word: Will trigger when audio energy exceeds noise floor + %.0f for %d consecutive frames",
                 service->energy_offset, service->activation_frames);
    } else {
        ESP_LOGW(TAG, "Wake-word audio path disabled; only simulated triggers are active");
    }

    return service;
}

void wake_word_service_stop(wake_word_service_t *service)
{
    if (!service) {
        return;
    }
    if (service->simulated_timer) {
        xTimerStop(service->simulated_timer, 0);
        xTimerDelete(service->simulated_timer, 0);
    }
    if (service->task) {
        service->stop_requested = true;
        while (service->task) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    free(service->frame_buffer);
    free(service);
}
