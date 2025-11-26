#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OpenWakeWord performance statistics
 */
typedef struct {
    uint32_t total_frames_processed;
    uint32_t detections_count;
    uint32_t inference_errors;
    uint32_t preprocessing_errors;
    
    // Timing (in milliseconds, approximate)
    uint32_t avg_preprocessing_time_ms;
    uint32_t avg_inference_time_ms;
    uint32_t max_preprocessing_time_ms;
    uint32_t max_inference_time_ms;
    
    // Memory usage
    size_t model_size_bytes;
    size_t tensor_arena_size_bytes;
    size_t audio_buffer_size_bytes;
    
    // Model state
    bool model_loaded;
    bool tflite_initialized;
    bool test_mode_enabled;
} openwakeword_stats_t;

/**
 * @brief Get current statistics
 * 
 * @param handle OpenWakeWord handle
 * @param stats_out Output statistics
 * @return ESP_OK on success
 */
int openwakeword_get_stats(void *handle, openwakeword_stats_t *stats_out);

/**
 * @brief Reset statistics
 * 
 * @param handle OpenWakeWord handle
 * @return ESP_OK on success
 */
int openwakeword_reset_stats(void *handle);

#ifdef __cplusplus
}
#endif