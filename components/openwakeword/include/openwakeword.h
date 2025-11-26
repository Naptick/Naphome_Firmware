/**
 * @file openwakeword.h
 * @brief OpenWakeWord wake word detection for ESP32-S3
 * 
 * Port of OpenWakeWord (https://github.com/dscripka/openWakeWord) to ESP32
 * for self-training custom wake words like "Hey, Naptick"
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback function type for wake word detection
 * 
 * @param wake_word_name Name of the detected wake word
 * @param confidence Confidence score (0.0 to 1.0)
 * @param user_data User-provided context
 */
typedef void (*openwakeword_callback_t)(const char *wake_word_name, float confidence, void *user_data);

/**
 * @brief Configuration for OpenWakeWord
 */
typedef struct {
    const char *model_path;           ///< Path to TFLite model in partition/SPIFFS
    float threshold;                   ///< Detection threshold (0.0 to 1.0, default 0.5)
    uint32_t sample_rate;              ///< Audio sample rate (default 16000)
    uint32_t frame_size_ms;           ///< Audio frame size in milliseconds (default 80)
    uint32_t cooldown_ms;             ///< Cooldown period after detection in ms (default 2000)
    bool enable_vad;                  ///< Enable voice activity detection
    float vad_threshold;              ///< VAD threshold if enabled
} openwakeword_config_t;

/**
 * @brief OpenWakeWord handle
 */
typedef struct openwakeword_handle* openwakeword_handle_t;

/**
 * @brief Initialize OpenWakeWord
 * 
 * @param config Configuration parameters
 * @param callback Callback function for wake word detection
 * @param user_data User context passed to callback
 * @param handle_out Output handle
 * @return 
 *     - ESP_OK on success
 *     - ESP_ERR_NO_MEM if memory allocation fails
 *     - ESP_ERR_NOT_FOUND if model file not found
 *     - ESP_ERR_INVALID_ARG if invalid configuration
 */
esp_err_t openwakeword_init(const openwakeword_config_t *config,
                            openwakeword_callback_t callback,
                            void *user_data,
                            openwakeword_handle_t *handle_out);

/**
 * @brief Process audio frame
 * 
 * @param handle OpenWakeWord handle
 * @param audio_samples 16-bit PCM audio samples
 * @param sample_count Number of samples (must match frame_size_ms)
 * @return 
 *     - ESP_OK on success
 *     - ESP_ERR_INVALID_ARG if invalid parameters
 */
esp_err_t openwakeword_process_audio(openwakeword_handle_t handle,
                                     const int16_t *audio_samples,
                                     size_t sample_count);

/**
 * @brief Deinitialize OpenWakeWord and free resources
 * 
 * @param handle OpenWakeWord handle
 * @return ESP_OK on success
 */
esp_err_t openwakeword_deinit(openwakeword_handle_t handle);

/**
 * @brief Get model input shape requirements
 * 
 * @param handle OpenWakeWord handle
 * @param samples_out Required number of audio samples per frame
 * @return ESP_OK on success
 */
esp_err_t openwakeword_get_input_requirements(openwakeword_handle_t handle,
                                               size_t *samples_out);

/**
 * @brief Get performance statistics
 * 
 * @param handle OpenWakeWord handle
 * @param total_frames_out Total frames processed
 * @param detections_out Total detections
 * @param model_loaded_out Whether model is loaded
 * @return ESP_OK on success
 */
esp_err_t openwakeword_get_statistics(openwakeword_handle_t handle,
                                       uint32_t *total_frames_out,
                                       uint32_t *detections_out,
                                       bool *model_loaded_out);

/**
 * @brief Test mode: Process audio without model (for integration testing)
 * 
 * @param handle OpenWakeWord handle
 * @param audio_samples Audio samples (16-bit PCM)
 * @param sample_count Number of samples
 * @param confidence_out Output confidence (0.0-1.0)
 * @return ESP_OK on success
 * 
 * @note Only available when CONFIG_OPENWAKEWORD_TEST_MODE is enabled
 */
esp_err_t openwakeword_test_mode_process(openwakeword_handle_t handle,
                                         const int16_t *audio_samples,
                                         size_t sample_count,
                                         float *confidence_out);

#ifdef __cplusplus
}
#endif