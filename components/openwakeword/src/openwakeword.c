/**
 * @file openwakeword.c
 * @brief OpenWakeWord implementation for ESP32-S3
 * 
 * This is a port of OpenWakeWord to ESP32 using TensorFlow Lite Micro.
 * 
 * Implementation status:
 * - [ ] TensorFlow Lite Micro integration
 * - [ ] Model loading from partition/SPIFFS
 * - [ ] Audio preprocessing (melspectrogram)
 * - [ ] Inference engine
 * - [ ] VAD integration
 */

#include "openwakeword.h"
#include "audio_features.h"
#include "model_loader.h"
#include "tflite_wrapper.h"

// Forward declaration for test mode
extern esp_err_t openwakeword_test_mode_process(openwakeword_handle_t handle,
                                                 const int16_t *audio_samples,
                                                 size_t sample_count,
                                                 float *confidence_out);

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>

// Melspectrogram size (defined in audio_features.c)
#define MELSPEC_N_MELS 40

// TensorFlow Lite Micro integration
#ifdef CONFIG_OPENWAKEWORD_USE_TFLITE
#define TFLITE_AVAILABLE 1
#else
#define TFLITE_AVAILABLE 0
#endif

static const char *TAG = "openwakeword";

struct openwakeword_handle {
    openwakeword_config_t config;
    openwakeword_callback_t callback;
    void *user_data;
    
    // Model state
#if TFLITE_AVAILABLE
    tflite_wrapper_t *tflite_wrapper;
    uint8_t *model_data;
    size_t model_size;
    float output_buffer[4];  // Buffer for model output
    size_t output_buffer_size;
#endif
    bool model_loaded;
    
    // Audio processing
    int16_t *audio_buffer;
    size_t audio_buffer_size;
    audio_features_t *audio_features;
    float *melspectrogram_buffer;  // Reusable buffer for melspectrogram
    
    // Detection state
    uint32_t last_detection_tick;
    uint32_t cooldown_ticks;
    
    // Statistics
    uint32_t total_frames_processed;
    uint32_t detections_count;
    uint32_t inference_errors;
    uint32_t preprocessing_errors;
    
    // Performance timing (approximate, in ticks)
    uint32_t last_preprocessing_ticks;
    uint32_t last_inference_ticks;
    uint32_t total_preprocessing_ticks;
    uint32_t total_inference_ticks;
    uint32_t preprocessing_count;
    uint32_t inference_count;
};

esp_err_t openwakeword_init(const openwakeword_config_t *config,
                            openwakeword_callback_t callback,
                            void *user_data,
                            openwakeword_handle_t *handle_out)
{
    if (!config || !callback || !handle_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing OpenWakeWord");
    ESP_LOGI(TAG, "  Model: %s", config->model_path ? config->model_path : "default");
    ESP_LOGI(TAG, "  Threshold: %.2f", config->threshold);
    ESP_LOGI(TAG, "  Sample rate: %u Hz", (unsigned int)config->sample_rate);
    ESP_LOGI(TAG, "  Frame size: %u ms", (unsigned int)config->frame_size_ms);
    
    openwakeword_handle_t handle = calloc(1, sizeof(struct openwakeword_handle));
    if (!handle) {
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(&handle->config, config, sizeof(openwakeword_config_t));
    handle->callback = callback;
    handle->user_data = user_data;
    
    // Initialize statistics
    handle->total_frames_processed = 0;
    handle->detections_count = 0;
    handle->inference_errors = 0;
    handle->preprocessing_errors = 0;
    handle->preprocessing_count = 0;
    handle->inference_count = 0;
    handle->total_preprocessing_ticks = 0;
    handle->total_inference_ticks = 0;
    
    // Calculate audio buffer size
    handle->audio_buffer_size = (config->sample_rate * config->frame_size_ms) / 1000;
    handle->audio_buffer = malloc(handle->audio_buffer_size * sizeof(int16_t));
    if (!handle->audio_buffer) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize audio features
    handle->audio_features = audio_features_init(config->sample_rate);
    if (!handle->audio_features) {
        free(handle->audio_buffer);
        free(handle);
        return ESP_ERR_NO_MEM;
    }
    
    // Calculate cooldown ticks
    handle->cooldown_ticks = pdMS_TO_TICKS(config->cooldown_ms);
    handle->last_detection_tick = 0;
    
    // Allocate melspectrogram buffer
    handle->melspectrogram_buffer = malloc(MELSPEC_N_MELS * sizeof(float));
    if (!handle->melspectrogram_buffer) {
        audio_features_deinit(handle->audio_features);
        free(handle->audio_buffer);
        free(handle);
        return ESP_ERR_NO_MEM;
    }
    
    // Load TensorFlow Lite model
    handle->model_loaded = false;
#if TFLITE_AVAILABLE
    handle->model_data = NULL;
    handle->model_size = 0;
    handle->tflite_wrapper = NULL;
    handle->output_buffer_size = sizeof(handle->output_buffer) / sizeof(float);
    
    if (config->model_path && strlen(config->model_path) > 0) {
        ESP_LOGI(TAG, "Loading TFLite model from: %s", config->model_path);
        
        // Try to load from SPIFFS first
        esp_err_t err = model_loader_load_from_spiffs(config->model_path,
                                                       &handle->model_data,
                                                       &handle->model_size);
        
        if (err != ESP_OK) {
            // Fallback: try loading from raw partition
            ESP_LOGW(TAG, "SPIFFS load failed, trying raw partition");
            err = model_loader_load_from_partition_raw("model",
                                                        &handle->model_data,
                                                        &handle->model_size);
        }
        
        if (err == ESP_OK && handle->model_data) {
            ESP_LOGI(TAG, "Model loaded: %zu bytes", handle->model_size);
            
            // Initialize TFLite wrapper
            size_t arena_size = CONFIG_OPENWAKEWORD_TENSOR_ARENA_SIZE;
            handle->tflite_wrapper = tflite_wrapper_create(
                handle->model_data,
                handle->model_size,
                arena_size
            );
            
            if (handle->tflite_wrapper && tflite_wrapper_is_initialized(handle->tflite_wrapper)) {
                handle->model_loaded = true;
                size_t input_size = tflite_wrapper_get_input_size(handle->tflite_wrapper);
                size_t output_size = tflite_wrapper_get_output_size(handle->tflite_wrapper);
                ESP_LOGI(TAG, "TFLite wrapper initialized");
                ESP_LOGI(TAG, "  Input: %zu floats, Output: %zu floats", input_size, output_size);
            } else {
                ESP_LOGW(TAG, "TFLite wrapper initialization failed");
                ESP_LOGW(TAG, "Add esp-tflite-micro component to enable inference");
                if (handle->tflite_wrapper) {
                    tflite_wrapper_destroy(handle->tflite_wrapper);
                    handle->tflite_wrapper = NULL;
                }
                handle->model_loaded = false;
            }
        } else {
            ESP_LOGE(TAG, "Failed to load model: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "No model path specified");
    }
#else
    ESP_LOGW(TAG, "TensorFlow Lite Micro not enabled (CONFIG_OPENWAKEWORD_USE_TFLITE)");
    ESP_LOGW(TAG, "To enable:");
    ESP_LOGW(TAG, "  1. Add esp-tflite-micro: git submodule add https://github.com/espressif/esp-tflite-micro components/esp-tflite-micro");
    ESP_LOGW(TAG, "  2. Enable CONFIG_OPENWAKEWORD_USE_TFLITE in menuconfig");
#endif
    
    *handle_out = handle;
    return ESP_OK;
}

esp_err_t openwakeword_process_audio(openwakeword_handle_t handle,
                                     const int16_t *audio_samples,
                                     size_t sample_count)
{
    if (!handle || !audio_samples) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if we're in cooldown period
    uint32_t now = xTaskGetTickCount();
    if (now - handle->last_detection_tick < handle->cooldown_ticks) {
        return ESP_OK;
    }
    
    // Validate sample count matches expected frame size
    size_t expected_samples = handle->audio_buffer_size;
    if (sample_count != expected_samples) {
        ESP_LOGW(TAG, "Sample count mismatch: got %zu, expected %zu", sample_count, expected_samples);
        // Continue with available samples
    }
    
    size_t samples_to_process = (sample_count < expected_samples) ? sample_count : expected_samples;
    
    // Copy audio to buffer
    memcpy(handle->audio_buffer, audio_samples, samples_to_process * sizeof(int16_t));
    
    if (!handle->model_loaded) {
        return ESP_ERR_NOT_FINISHED;
    }
    
    // Step 1: Extract audio features (melspectrogram)
    TickType_t preprocess_start = xTaskGetTickCount();
    size_t mel_size = 0;
    
    esp_err_t err = audio_features_extract_melspectrogram(
        handle->audio_features,
        audio_samples,
        sample_count,
        handle->melspectrogram_buffer,
        &mel_size
    );
    TickType_t preprocess_end = xTaskGetTickCount();
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to extract melspectrogram: %s", esp_err_to_name(err));
        handle->preprocessing_errors++;
        return err;
    }
    
    // Update preprocessing statistics
    TickType_t preprocess_ticks = preprocess_end - preprocess_start;
    handle->total_preprocessing_ticks += preprocess_ticks;
    handle->preprocessing_count++;
    handle->total_frames_processed++;
    
    // Step 2: Run TensorFlow Lite inference
#if TFLITE_AVAILABLE
    // Check for test mode first
    #ifdef CONFIG_OPENWAKEWORD_TEST_MODE
    if (!handle->model_loaded || !handle->tflite_wrapper) {
        // Use test mode when model not loaded
        float test_confidence = 0.0f;
        esp_err_t test_err = openwakeword_test_mode_process(
            handle, audio_samples, sample_count, &test_confidence);
        
        if (test_err == ESP_OK && test_confidence > handle->config.threshold) {
            // Test mode detection
            handle->last_detection_tick = now;
            handle->detections_count++;
            
            ESP_LOGI(TAG, "*** TEST MODE: WAKE WORD DETECTED *** Confidence: %.2f (threshold: %.2f)", 
                     test_confidence, handle->config.threshold);
            
            if (handle->callback) {
                handle->callback("hey_naptick", test_confidence, handle->user_data);
            }
        }
        
        if (handle->total_frames_processed % 100 == 0) {
            ESP_LOGD(TAG, "Test mode: processed %u frames", handle->total_frames_processed);
        }
        return ESP_OK;
    }
    #endif // CONFIG_OPENWAKEWORD_TEST_MODE
    
    if (!handle->model_loaded || !handle->tflite_wrapper) {
        // Model not loaded or TFLite not initialized
        if (handle->total_frames_processed % 100 == 0) {
            ESP_LOGD(TAG, "Processed %u frames (model not loaded)", 
                     handle->total_frames_processed);
        }
        return ESP_ERR_NOT_FINISHED;
    }
    
    float confidence = 0.0f;
    size_t output_size = handle->output_buffer_size;
    
    // Run TFLite inference
    TickType_t inference_start = xTaskGetTickCount();
    esp_err_t err = tflite_wrapper_invoke(
        handle->tflite_wrapper,
        handle->melspectrogram_buffer,
        mel_size,
        handle->output_buffer,
        &output_size
    );
    TickType_t inference_end = xTaskGetTickCount();
    
    // Update inference statistics
    TickType_t inference_ticks = inference_end - inference_start;
    handle->total_inference_ticks += inference_ticks;
    handle->inference_count++;
    
    if (err == ESP_OK && output_size > 0) {
        // Get confidence from output
        // Assuming binary classification: [not_wake_word, wake_word]
        if (output_size >= 2) {
            confidence = handle->output_buffer[1];  // Wake word probability
        } else if (output_size >= 1) {
            confidence = handle->output_buffer[0];  // Single output
        }
        
        ESP_LOGD(TAG, "Inference: confidence=%.3f, threshold=%.3f", 
                 confidence, handle->config.threshold);
    } else if (err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "TFLite inference failed: %s", esp_err_to_name(err));
        handle->inference_errors++;
        return err;
    }
    
    // Step 3: Check if score exceeds threshold
    if (confidence > handle->config.threshold) {
        // Step 4: Call callback if detected
        handle->last_detection_tick = now;
        handle->detections_count++;
        
        ESP_LOGI(TAG, "*** WAKE WORD DETECTED *** Confidence: %.2f (threshold: %.2f)", 
                 confidence, handle->config.threshold);
        
        if (handle->callback) {
            handle->callback("hey_naptick", confidence, handle->user_data);
        }
    }
#else
    // Without TFLite, just log that we're processing
    if (handle->total_frames_processed % 100 == 0) {
        ESP_LOGD(TAG, "Processed %" PRIu32 " audio frames (TFLite not enabled)", 
                 handle->total_frames_processed);
    }
#endif
    
    return ESP_OK;
}

esp_err_t openwakeword_deinit(openwakeword_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Deinitializing OpenWakeWord");
    ESP_LOGI(TAG, "  Total frames processed: %" PRIu32, handle->total_frames_processed);
    ESP_LOGI(TAG, "  Detections: %" PRIu32, handle->detections_count);
    
    if (handle->audio_features) {
        audio_features_deinit(handle->audio_features);
    }
    
    if (handle->audio_buffer) {
        free(handle->audio_buffer);
    }
    
    if (handle->melspectrogram_buffer) {
        free(handle->melspectrogram_buffer);
    }
    
#if TFLITE_AVAILABLE
    // Free TFLite resources
    if (handle->tflite_wrapper) {
        tflite_wrapper_destroy(handle->tflite_wrapper);
        handle->tflite_wrapper = NULL;
    }
    
    if (handle->model_data) {
        model_loader_free(handle->model_data);
        handle->model_data = NULL;
    }
#endif
    
    free(handle);
    return ESP_OK;
}

esp_err_t openwakeword_get_input_requirements(openwakeword_handle_t handle,
                                               size_t *samples_out)
{
    if (!handle || !samples_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *samples_out = handle->audio_buffer_size;
    return ESP_OK;
}

esp_err_t openwakeword_get_statistics(openwakeword_handle_t handle,
                                       uint32_t *total_frames_out,
                                       uint32_t *detections_out,
                                       bool *model_loaded_out)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (total_frames_out) {
        *total_frames_out = handle->total_frames_processed;
    }
    if (detections_out) {
        *detections_out = handle->detections_count;
    }
    if (model_loaded_out) {
        *model_loaded_out = handle->model_loaded;
    }
    
    return ESP_OK;
}