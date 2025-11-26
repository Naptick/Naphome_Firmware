/**
 * @file openwakeword_integration_example.c
 * @brief Example integration of OpenWakeWord with wake_word_service
 * 
 * This is an example showing how to integrate OpenWakeWord with the existing
 * wake_word_service. Copy relevant parts to wake_word_service.c
 */

#include "openwakeword.h"
#include "wake_word_service.h"
#include "esp_log.h"

static const char *TAG = "oww_integration";

// Example: Add OpenWakeWord handle to wake_word_service struct
// Add this to wake_word_service.h and wake_word_service.c:

/*
struct wake_word_service {
    // ... existing fields ...
    openwakeword_handle_t oww_handle;
    bool use_openwakeword;
};
*/

// Example callback for OpenWakeWord detection
static void openwakeword_detection_callback(const char *wake_word_name, 
                                            float confidence, 
                                            void *user_data)
{
    wake_word_service_t *service = (wake_word_service_t *)user_data;
    ESP_LOGI(TAG, "OpenWakeWord detected: %s (confidence: %.2f)", wake_word_name, confidence);
    
    // Trigger the existing wake word callback
    if (service && service->callback) {
        service->callback(service->callback_ctx);
    }
}

// Example: Initialize OpenWakeWord in wake_word_service_start()
esp_err_t example_init_openwakeword(wake_word_service_t *service)
{
#ifdef CONFIG_OPENWAKEWORD_ENABLE
    openwakeword_config_t oww_config = {
        .model_path = CONFIG_OPENWAKEWORD_MODEL_PATH,
        .threshold = CONFIG_OPENWAKEWORD_THRESHOLD,
        .sample_rate = 16000,  // Match your audio sample rate
        .frame_size_ms = CONFIG_OPENWAKEWORD_FRAME_SIZE_MS,
        .cooldown_ms = CONFIG_OPENWAKEWORD_COOLDOWN_MS,
        .enable_vad = false,  // Optional VAD
        .vad_threshold = 0.5f,
    };
    
    esp_err_t err = openwakeword_init(&oww_config, 
                                      openwakeword_detection_callback,
                                      service,
                                      &service->oww_handle);
    if (err == ESP_OK) {
        service->use_openwakeword = true;
        ESP_LOGI(TAG, "OpenWakeWord initialized successfully");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "OpenWakeWord init failed: %s, falling back to RMS detector", 
                 esp_err_to_name(err));
        service->use_openwakeword = false;
        return err;
    }
#else
    service->use_openwakeword = false;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// Example: Process audio with OpenWakeWord in wake_word_task()
void example_process_audio_with_openwakeword(wake_word_service_t *service,
                                              const int16_t *audio_samples,
                                              size_t sample_count)
{
    if (service->use_openwakeword && service->oww_handle) {
        // Use OpenWakeWord for detection
        esp_err_t err = openwakeword_process_audio(service->oww_handle, 
                                                    audio_samples, 
                                                    sample_count);
        if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
            ESP_LOGW(TAG, "OpenWakeWord processing error: %s", esp_err_to_name(err));
        }
    } else {
        // Fall back to RMS-based detection
        // ... existing RMS detection code ...
    }
}

// Example: Cleanup in wake_word_service_stop()
void example_cleanup_openwakeword(wake_word_service_t *service)
{
    if (service->oww_handle) {
        openwakeword_deinit(service->oww_handle);
        service->oww_handle = NULL;
        service->use_openwakeword = false;
    }
}