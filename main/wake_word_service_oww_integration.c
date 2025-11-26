/**
 * @file wake_word_service_oww_integration.c
 * @brief Integration of OpenWakeWord with wake_word_service
 * 
 * This file shows how to integrate OpenWakeWord into the existing wake_word_service.
 * Copy the relevant parts into wake_word_service.c and wake_word_service.h
 */

#include "wake_word_service.h"
#include "openwakeword.h"
#include "esp_log.h"

static const char *TAG = "wake_word_oww";

// ============================================================================
// Add to wake_word_service.h:
// ============================================================================
/*
#include "openwakeword.h"

typedef struct wake_word_service {
    // ... existing fields ...
    openwakeword_handle_t oww_handle;
    bool use_openwakeword;
    bool oww_initialized;
} wake_word_service_t;
*/

// ============================================================================
// OpenWakeWord detection callback
// ============================================================================
static void openwakeword_detection_callback(const char *wake_word_name, 
                                            float confidence, 
                                            void *user_data)
{
    wake_word_service_t *service = (wake_word_service_t *)user_data;
    if (!service) {
        return;
    }
    
    ESP_LOGI(TAG, "OpenWakeWord detected: %s (confidence: %.2f)", 
             wake_word_name, confidence);
    
    // Trigger the existing wake word callback
    if (service->callback) {
        service->callback(service->callback_ctx);
    }
}

// ============================================================================
// Add to wake_word_service_start() - Initialize OpenWakeWord
// ============================================================================
esp_err_t wake_word_service_init_openwakeword(wake_word_service_t *service,
                                              const wake_word_service_config_t *cfg)
{
#ifdef CONFIG_OPENWAKEWORD_ENABLE
    if (!service || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing OpenWakeWord");
    
    openwakeword_config_t oww_config = {
        .model_path = CONFIG_OPENWAKEWORD_MODEL_PATH,
        .threshold = CONFIG_OPENWAKEWORD_THRESHOLD,
        .sample_rate = 16000,  // Match your audio sample rate
        .frame_size_ms = CONFIG_OPENWAKEWORD_FRAME_SIZE_MS,
        .cooldown_ms = CONFIG_OPENWAKEWORD_COOLDOWN_MS,
        .enable_vad = false,
        .vad_threshold = 0.5f,
    };
    
    esp_err_t err = openwakeword_init(&oww_config, 
                                      openwakeword_detection_callback,
                                      service,
                                      &service->oww_handle);
    
    if (err == ESP_OK && service->oww_handle) {
        service->use_openwakeword = true;
        service->oww_initialized = true;
        ESP_LOGI(TAG, "OpenWakeWord initialized successfully");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "OpenWakeWord init failed: %s, falling back to RMS detector", 
                 esp_err_to_name(err));
        service->use_openwakeword = false;
        service->oww_initialized = false;
        return err;
    }
#else
    service->use_openwakeword = false;
    service->oww_initialized = false;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// ============================================================================
// Add to wake_word_task() - Process audio with OpenWakeWord
// ============================================================================
void wake_word_service_process_audio_openwakeword(wake_word_service_t *service,
                                                    const int16_t *audio_samples,
                                                    size_t sample_count)
{
    if (!service || !audio_samples || sample_count == 0) {
        return;
    }
    
    // Use OpenWakeWord if available and initialized
    if (service->use_openwakeword && service->oww_handle && service->oww_initialized) {
        esp_err_t err = openwakeword_process_audio(service->oww_handle, 
                                                    audio_samples, 
                                                    sample_count);
        
        if (err == ESP_OK || err == ESP_ERR_NOT_FINISHED) {
            // Processing successful or model not loaded yet
            return;
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "OpenWakeWord processing error: %s, falling back to RMS", 
                     esp_err_to_name(err));
            // Fall through to RMS detector
        }
    }
    
    // Fallback to RMS-based detection
    // ... existing RMS detection code from wake_word_service.c ...
}

// ============================================================================
// Add to wake_word_service_stop() - Cleanup OpenWakeWord
// ============================================================================
void wake_word_service_cleanup_openwakeword(wake_word_service_t *service)
{
    if (!service) {
        return;
    }
    
    if (service->oww_handle) {
        ESP_LOGI(TAG, "Deinitializing OpenWakeWord");
        openwakeword_deinit(service->oww_handle);
        service->oww_handle = NULL;
        service->use_openwakeword = false;
        service->oww_initialized = false;
    }
}

// ============================================================================
// Example: Modified wake_word_service_start() with OpenWakeWord
// ============================================================================
/*
wake_word_service_t *wake_word_service_start(const wake_word_service_config_t *cfg,
                                             wake_word_callback_t cb,
                                             void *cb_ctx)
{
    // ... existing initialization code ...
    
    // Try to initialize OpenWakeWord
    #ifdef CONFIG_OPENWAKEWORD_ENABLE
    esp_err_t oww_err = wake_word_service_init_openwakeword(service, cfg);
    if (oww_err == ESP_OK) {
        ESP_LOGI(TAG, "Using OpenWakeWord for wake word detection");
        // Skip RMS detector initialization or keep as fallback
    } else {
        ESP_LOGW(TAG, "OpenWakeWord not available, using RMS detector");
        service->use_openwakeword = false;
        // Initialize RMS detector as fallback
    }
    #else
    service->use_openwakeword = false;
    #endif
    
    // ... rest of initialization ...
}
*/

// ============================================================================
// Example: Modified wake_word_task() with OpenWakeWord
// ============================================================================
/*
static void wake_word_task(void *arg)
{
    wake_word_service_t *service = (wake_word_service_t *)arg;
    
    while (!service->stop_requested) {
        // Get audio frame
        // ... existing audio capture code ...
        
        if (service->use_openwakeword && service->oww_initialized) {
            // Use OpenWakeWord
            wake_word_service_process_audio_openwakeword(service, 
                                                         frame_buffer, 
                                                         samples_read);
        } else {
            // Use RMS-based detection
            // ... existing RMS detection code ...
        }
    }
}
*/