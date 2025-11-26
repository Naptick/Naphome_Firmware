/**
 * @file openwakeword_test_mode.c
 * @brief Test mode for OpenWakeWord without requiring a model
 * 
 * Allows testing audio preprocessing and integration without TFLite model
 */

#include "openwakeword.h"
#include "audio_features.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "oww_test";

#ifdef CONFIG_OPENWAKEWORD_TEST_MODE

// Test mode: Simulate wake word detection based on audio energy
// This allows testing the integration without a trained model
static float test_mode_simulate_detection(openwakeword_handle_t handle, 
                                          const int16_t *audio_samples,
                                          size_t sample_count)
{
    if (!handle || !audio_samples || sample_count == 0) {
        return 0.0f;
    }
    
    // Compute RMS energy
    float sum_sq = 0.0f;
    for (size_t i = 0; i < sample_count; i++) {
        float sample = (float)audio_samples[i] / 32768.0f;
        sum_sq += sample * sample;
    }
    float rms = sqrtf(sum_sq / sample_count);
    
    // Convert to dB
    float db = 20.0f * log10f(rms + 1e-10f);
    
    // Simple heuristic: high energy + some variation suggests speech
    // This is a very crude detector, just for testing integration
    float confidence = 0.0f;
    
    // Threshold-based detection for testing
    // Adjust these values based on your environment
    const float ENERGY_THRESHOLD_DB = -30.0f;  // Adjust for your mic sensitivity
    const float MIN_CONFIDENCE = 0.3f;
    const float MAX_CONFIDENCE = 0.9f;
    
    if (db > ENERGY_THRESHOLD_DB) {
        // Map energy to confidence
        float normalized = (db - ENERGY_THRESHOLD_DB) / 20.0f;  // 20dB range
        confidence = MIN_CONFIDENCE + normalized * (MAX_CONFIDENCE - MIN_CONFIDENCE);
        if (confidence > MAX_CONFIDENCE) confidence = MAX_CONFIDENCE;
        if (confidence < MIN_CONFIDENCE) confidence = 0.0f;
    }
    
    // Log occasionally for debugging
    static int log_counter = 0;
    if (++log_counter % 100 == 0) {
        ESP_LOGI(TAG, "Test mode: RMS=%.4f, dB=%.1f, confidence=%.2f", 
                 rms, db, confidence);
    }
    
    return confidence;
}

// Export test mode function
esp_err_t openwakeword_test_mode_process(openwakeword_handle_t handle,
                                         const int16_t *audio_samples,
                                         size_t sample_count,
                                         float *confidence_out)
{
    if (!handle || !audio_samples || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if test mode is enabled
    #ifndef CONFIG_OPENWAKEWORD_TEST_MODE
    return ESP_ERR_NOT_SUPPORTED;
    #endif
    
    // Extract melspectrogram to test audio preprocessing
    if (handle->audio_features) {
        size_t mel_size = 0;
        esp_err_t err = audio_features_extract_melspectrogram(
            handle->audio_features,
            audio_samples,
            sample_count,
            handle->melspectrogram_buffer,
            &mel_size
        );
        
        if (err == ESP_OK && mel_size > 0) {
            // Log melspectrogram stats occasionally
            static int mel_log_counter = 0;
            if (++mel_log_counter % 200 == 0) {
                float mel_sum = 0.0f;
                float mel_max = 0.0f;
                for (size_t i = 0; i < mel_size; i++) {
                    mel_sum += handle->melspectrogram_buffer[i];
                    if (handle->melspectrogram_buffer[i] > mel_max) {
                        mel_max = handle->melspectrogram_buffer[i];
                    }
                }
                float mel_avg = mel_sum / mel_size;
                ESP_LOGI(TAG, "Test mode melspectrogram: size=%zu, avg=%.3f, max=%.3f", 
                         mel_size, mel_avg, mel_max);
            }
        }
    }
    
    // Simulate detection
    float confidence = test_mode_simulate_detection(handle, audio_samples, sample_count);
    
    if (confidence_out) {
        *confidence_out = confidence;
    }
    
    return ESP_OK;
}

#else

// Stub when test mode is disabled
esp_err_t openwakeword_test_mode_process(openwakeword_handle_t handle,
                                         const int16_t *audio_samples,
                                         size_t sample_count,
                                         float *confidence_out)
{
    (void)handle;
    (void)audio_samples;
    (void)sample_count;
    (void)confidence_out;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif // CONFIG_OPENWAKEWORD_TEST_MODE