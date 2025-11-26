/**
 * @file main.c
 * @brief OpenWakeWord Demo for Korvo-1
 * 
 * Demonstrates OpenWakeWord wake word detection with "Hey, Naptick"
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// Use korvo_audio from korvo_voice_assistant sample
// The CMakeLists.txt adds this to include path
#include "korvo_audio.h"
#include "openwakeword.h"

static const char *TAG = "oww_demo";

// OpenWakeWord detection callback
static void wake_word_detected(const char *wake_word_name, float confidence, void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "*** WAKE WORD DETECTED ***");
    ESP_LOGI(TAG, "  Word: %s", wake_word_name);
    ESP_LOGI(TAG, "  Confidence: %.2f", confidence);
    ESP_LOGI(TAG, "========================================");
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Korvo-1 OpenWakeWord Demo ===");
    ESP_LOGI(TAG, "Listening for 'Hey, Naptick'");
    
    // Initialize Korvo audio
    korvo_audio_t audio = {0};
    esp_err_t err = korvo_audio_init(&audio, 16000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Audio initialized: 16kHz sample rate");
    
    // Configure OpenWakeWord
    openwakeword_config_t oww_config = {
        .model_path = CONFIG_OPENWAKEWORD_MODEL_PATH,
        .threshold = CONFIG_OPENWAKEWORD_THRESHOLD,
        .sample_rate = 16000,
        .frame_size_ms = CONFIG_OPENWAKEWORD_FRAME_SIZE_MS,
        .cooldown_ms = CONFIG_OPENWAKEWORD_COOLDOWN_MS,
        .enable_vad = false,
        .vad_threshold = 0.5f,
    };
    
    openwakeword_handle_t oww_handle = NULL;
    err = openwakeword_init(&oww_config, wake_word_detected, NULL, &oww_handle);
    
    if (err != ESP_OK || !oww_handle) {
        ESP_LOGW(TAG, "OpenWakeWord init returned: %s", esp_err_to_name(err));
        ESP_LOGI(TAG, "This is expected if TFLite is not yet integrated");
        ESP_LOGI(TAG, "Audio preprocessing will still work for testing");
    } else {
        ESP_LOGI(TAG, "OpenWakeWord initialized successfully");
        
        // Get input requirements
        size_t required_samples = 0;
        openwakeword_get_input_requirements(oww_handle, &required_samples);
        ESP_LOGI(TAG, "Required samples per frame: %zu", required_samples);
    }
    
    // Calculate frame size: 80ms @ 16kHz = 1280 samples
    const size_t frame_samples = (16000 * CONFIG_OPENWAKEWORD_FRAME_SIZE_MS) / 1000;
    int16_t *audio_buffer = malloc(frame_samples * sizeof(int16_t));
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        korvo_audio_shutdown(&audio);
        return;
    }
    
    ESP_LOGI(TAG, "Starting audio capture loop...");
    ESP_LOGI(TAG, "Frame size: %zu samples (%d ms)", frame_samples, CONFIG_OPENWAKEWORD_FRAME_SIZE_MS);
    ESP_LOGI(TAG, "Say 'Hey, Naptick' to test wake word detection");
    
    uint32_t frame_count = 0;
    
    while (1) {
        // Read audio frame
        size_t samples_read = 0;
        err = korvo_audio_capture(&audio, audio_buffer, frame_samples, &samples_read, pdMS_TO_TICKS(100));
        
        if (err == ESP_OK && samples_read > 0) {
            frame_count++;
            
            if (oww_handle) {
                // Process with OpenWakeWord
                esp_err_t process_err = openwakeword_process_audio(oww_handle, audio_buffer, samples_read);
                if (process_err == ESP_ERR_NOT_FINISHED) {
                    // Model not loaded yet, this is expected
                    if (frame_count % 100 == 0) {
                        ESP_LOGD(TAG, "Processing audio (model not loaded yet, frame %lu)", frame_count);
                    }
                } else if (process_err != ESP_OK) {
                    ESP_LOGW(TAG, "OpenWakeWord processing error: %s", esp_err_to_name(process_err));
                }
            } else {
                // Log that we're capturing audio
                if (frame_count % 100 == 0) {
                    ESP_LOGI(TAG, "Capturing audio: %zu samples (frame %lu, OpenWakeWord not initialized)", 
                             samples_read, frame_count);
                }
            }
        } else if (err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Audio capture error: %s", esp_err_to_name(err));
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Cleanup (never reached in this demo)
    if (oww_handle) {
        openwakeword_deinit(oww_handle);
    }
    free(audio_buffer);
    korvo_audio_shutdown(&audio);
}