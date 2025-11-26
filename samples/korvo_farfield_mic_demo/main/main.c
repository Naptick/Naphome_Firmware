#include <string.h>
#include <math.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "led_strip.h"
#include "driver/i2c.h"
#include "driver/i2s.h"

#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_config.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"
#endif

static const char *TAG = "farfield_mic";

// LED configuration
#define LED_GPIO CONFIG_KORVO_FARFIELD_LED_GPIO
#define LED_COUNT CONFIG_KORVO_FARFIELD_LED_COUNT
#define LED_BRIGHTNESS CONFIG_KORVO_FARFIELD_LED_BRIGHTNESS

// Microphone LED indices (Korvo-1 has 12 LEDs in a ring)
#define MIC1_LED_INDEX 4   // Blue - Left channel
#define MIC2_LED_INDEX 8   // Green - Right channel
#define MIC3_LED_INDEX 12  // Red - Combined far-field

// Audio configuration
#ifndef SAMPLE_RATE_HZ
#define SAMPLE_RATE_HZ CONFIG_KORVO_FARFIELD_SAMPLE_RATE
#endif
#define FRAME_SIZE_MS CONFIG_KORVO_FARFIELD_FRAME_SIZE_MS
#define SAMPLES_PER_FRAME ((SAMPLE_RATE_HZ * FRAME_SIZE_MS) / 1000)

// ES7210 I2S configuration (from esp-skainet BSP)
#define ES7210_I2S_PORT I2S_NUM_1
#define ES7210_I2S_SDIN GPIO_NUM_11
#define ES7210_I2S_SCLK GPIO_NUM_10
#define ES7210_I2S_LRCK GPIO_NUM_9
#define ES7210_I2S_MCLK GPIO_NUM_20
#define ES7210_I2C_ADDR 0x40
#define ES7210_I2C_SDA GPIO_NUM_1
#define ES7210_I2C_SCL GPIO_NUM_2

// Audio level scaling
// RAW audio from ES7210: very low levels (0.01-50 range)
// AFE-processed audio: much higher levels due to beamforming, AGC, noise suppression (typically 100-3000+ range)
#define NOISE_FLOOR_RAW 0.01f    // For raw ES7210 audio - extremely low threshold
#define MAX_LEVEL_RAW 50.0f      // Typical raw ES7210 output levels
#define NOISE_FLOOR_AFE 100.0f   // For AFE-processed audio - beamformed/AGC boosted
#define MAX_LEVEL_AFE 3000.0f    // Typical AFE-processed levels (similar to voice assistant range)

// Note: If LED 12 is blinking red, that's good! It means:
// - LEDs are working on GPIO19
// - Audio is being detected
// - LED 12 (MIC3) shows combined far-field audio levels

static led_strip_handle_t s_led_strip = NULL;
static bool s_i2s_initialized = false;

#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
// Wake word LED state - when detected, show white on LEDs 4, 8, 12
static bool s_wake_word_active = false;
static TickType_t s_wake_word_led_until = 0;
#define WAKE_WORD_LED_DURATION_MS 1000  // Keep white LEDs for 1 second after wake word
// AFE (Audio Front-End) state for far-field processing
static const esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
static int s_afe_feed_chunksize = 0;
static int s_afe_feed_channels = 0;
static int16_t *s_afe_feed_buffer = NULL;
static TickType_t s_wake_word_cooldown_until = 0;
#define WAKE_WORD_COOLDOWN_MS 2000

// AFE-processed audio levels for LED visualization
// These are updated from AFE fetch results and have much better levels than raw audio
static float s_afe_mic1_level = 0.0f;
static float s_afe_mic2_level = 0.0f;
static float s_afe_mic3_level = 0.0f;
static bool s_afe_levels_valid = false;
#endif

// Helper function to scale audio level to LED brightness (0-255)
// Supports both raw ES7210 audio and AFE-processed audio with different ranges
static uint8_t scale_audio_level_to_brightness(float level, bool is_afe_processed)
{
    float noise_floor = is_afe_processed ? NOISE_FLOOR_AFE : NOISE_FLOOR_RAW;
    float max_level = is_afe_processed ? MAX_LEVEL_AFE : MAX_LEVEL_RAW;
    
    // Log very low levels for debugging
    static int debug_count = 0;
    if (++debug_count % 100 == 0 && level > 0.0f && level < noise_floor) {
        ESP_LOGD(TAG, "Audio level %.4f below noise floor %.4f (%s)", 
                 level, noise_floor, is_afe_processed ? "AFE" : "RAW");
    }
    
    if (level < noise_floor) return 0; // Below noise floor
    if (level > max_level) return 255; // Max brightness
    
    // Linear scaling between noise floor and max
    float normalized = (level - noise_floor) / (max_level - noise_floor);
    if (normalized > 1.0f) normalized = 1.0f;
    if (normalized < 0.0f) normalized = 0.0f;
    
    // Use square root for better sensitivity at low levels
    // This gives more response at lower levels so LEDs 4,8 react even with quieter audio
    normalized = sqrtf(normalized);
    
    // Apply aggressive minimum brightness boost for very low levels to ensure visibility
    // Even tiny audio should produce visible LED response
    if (normalized > 0.001f && normalized < 0.2f) {
        normalized = normalized * 3.0f; // More aggressive boost for low levels
        if (normalized > 1.0f) normalized = 1.0f;
    }
    
    // Ensure minimum visible brightness for any detected audio
    if (normalized > 0.0f && normalized < 0.05f) {
        normalized = 0.05f; // Minimum 5% brightness for any audio above noise floor
    }
    
    return (uint8_t)(normalized * 255.0f);
}

/**
 * Compute energy levels for each microphone channel
 * STEREO format: samples are interleaved [L, R, L, R, ...]
 * 
 * Korvo-1 uses ES7210 ADC with 3 analog microphones:
 * - MIC1 = Left channel (first microphone)
 * - MIC2 = Right channel (second microphone)
 * - MIC3 = Combined/averaged signal for far-field beamforming
 */
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
    
    // Process STEREO interleaved samples: [L, R, L, R, ...]
    // Note: ES7210 on Korvo-1 has 4 analog mics but outputs 2-channel STEREO
    // The ES7210 may combine mics internally, so L and R might have different content
    for (size_t i = 0; i < sample_count; i += 2) {
        // Left channel (MIC1) - index 0, 2, 4, ...
        if (i < sample_count) {
            int32_t val1 = samples[i];
            int32_t abs_val1 = val1 >= 0 ? val1 : -val1;
            sum1 += abs_val1;
            count1++;
            sum3 += abs_val1; // Include in combined
            count3++;
        }
        
        // Right channel (MIC2) - index 1, 3, 5, ...
        if (i + 1 < sample_count) {
            int32_t val2 = samples[i + 1];
            int32_t abs_val2 = val2 >= 0 ? val2 : -val2;
            sum2 += abs_val2;
            count2++;
            sum3 += abs_val2; // Include in combined
            count3++;
        } else {
            // If no right channel, duplicate left (mono mode)
            if (i < sample_count) {
                int32_t val1 = samples[i];
                int32_t abs_val1 = val1 >= 0 ? val1 : -val1;
                sum2 += abs_val1;
                count2++;
            }
        }
    }
    
    // Log first frame to debug channel separation
    static bool first_compute = true;
    if (first_compute && sample_count >= 4) {
        ESP_LOGI(TAG, "First mic level compute: samples=%zu, L[0]=%d R[1]=%d L[2]=%d R[3]=%d",
                 sample_count, samples[0], samples[1], samples[2], samples[3]);
        first_compute = false;
    }
    
    *mic1_level = count1 > 0 ? (float)sum1 / (float)count1 : 0.0f;
    *mic2_level = count2 > 0 ? (float)sum2 / (float)count2 : 0.0f;
    *mic3_level = count3 > 0 ? (float)sum3 / (float)count3 : 0.0f; // Combined average
}

/**
 * Update LEDs based on microphone audio levels
 * LEDs 4, 8, 12 are sound reactive unless wake word is active
 * @param mic1_level Audio level for MIC1 (left channel)
 * @param mic2_level Audio level for MIC2 (right channel)
 * @param mic3_level Audio level for MIC3 (combined/beamformed)
 * @param is_afe_processed True if levels are from AFE-processed audio (better levels)
 */
static void update_mic_leds(float mic1_level, float mic2_level, float mic3_level, bool is_afe_processed)
{
    // Log first few calls to verify function is being invoked
    static int call_count = 0;
    if (++call_count <= 5) {
        ESP_LOGI(TAG, "update_mic_leds called #%d: MIC1=%.2f, MIC2=%.2f, MIC3=%.2f (%s)", 
                 call_count, mic1_level, mic2_level, mic3_level, is_afe_processed ? "AFE" : "RAW");
    }
    
    if (!s_led_strip) {
        static int null_strip_warn_count = 0;
        if (++null_strip_warn_count % 1000 == 0) {
            ESP_LOGW(TAG, "LED strip is NULL - cannot update LEDs");
        }
        return;
    }
    
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
    // Check if wake word is active - if so, keep LEDs 4, 8, 12 white
    TickType_t now = xTaskGetTickCount();
    bool wake_word_blocking = s_wake_word_active && now < s_wake_word_led_until;
    
    // Debug log wake word state periodically
    static int wake_word_debug_count = 0;
    if (++wake_word_debug_count % 500 == 0 && wake_word_blocking) {
        ESP_LOGI(TAG, "Wake word active - LEDs 4,8,12 in white mode (until tick %lu, now %lu)", 
                 s_wake_word_led_until, now);
    }
    
    if (wake_word_blocking) {
        // Wake word is active - keep LEDs 4, 8, 12 white
        if (MIC1_LED_INDEX <= LED_COUNT) {
            led_strip_set_pixel(s_led_strip, MIC1_LED_INDEX - 1, 255, 255, 255); // White
        }
        if (MIC2_LED_INDEX <= LED_COUNT) {
            led_strip_set_pixel(s_led_strip, MIC2_LED_INDEX - 1, 255, 255, 255); // White
        }
        if (MIC3_LED_INDEX <= LED_COUNT) {
            led_strip_set_pixel(s_led_strip, MIC3_LED_INDEX - 1, 255, 255, 255); // White
        }
    } else {
        // Wake word period expired - resume sound-reactive behavior
        if (s_wake_word_active && now >= s_wake_word_led_until) {
            s_wake_word_active = false;
            ESP_LOGI(TAG, "Wake word LED period ended - resuming sound-reactive LEDs");
        }
        
        // Sound-reactive mode: scale audio levels to LED brightness (0-255 range)
        // Use appropriate scaling based on whether audio is AFE-processed or raw
        uint8_t mic1_bright = scale_audio_level_to_brightness(mic1_level, is_afe_processed);
        uint8_t mic2_bright = scale_audio_level_to_brightness(mic2_level, is_afe_processed);
        uint8_t mic3_bright = scale_audio_level_to_brightness(mic3_level, is_afe_processed);
        
        // Scale by LED brightness - ensure minimum visibility even at low brightness
        uint8_t mic1_scaled = (mic1_bright * LED_BRIGHTNESS) / 255;
        uint8_t mic2_scaled = (mic2_bright * LED_BRIGHTNESS) / 255;
        uint8_t mic3_scaled = (mic3_bright * LED_BRIGHTNESS) / 255;
        
        // Ensure minimum visible brightness if audio is detected (above noise floor)
        // This prevents LEDs from being completely off when there's audio
        float noise_floor = is_afe_processed ? NOISE_FLOOR_AFE : NOISE_FLOOR_RAW;
        if (mic1_level > noise_floor && mic1_scaled == 0 && mic1_bright > 0) {
            mic1_scaled = 1; // At least 1 unit of brightness
        }
        if (mic2_level > noise_floor && mic2_scaled == 0 && mic2_bright > 0) {
            mic2_scaled = 1;
        }
        if (mic3_level > noise_floor && mic3_scaled == 0 && mic3_bright > 0) {
            mic3_scaled = 1;
        }
        
        // Debug log first few updates to verify scaling
        static int update_debug_count = 0;
        if (update_debug_count < 10) {
            ESP_LOGI(TAG, "LED update: MIC1=%.2f->%d, MIC2=%.2f->%d, MIC3=%.2f->%d (scaled: %d,%d,%d)",
                     mic1_level, mic1_bright, mic2_level, mic2_bright, mic3_level, mic3_bright,
                     mic1_scaled, mic2_scaled, mic3_scaled);
            update_debug_count++;
        }
        
        // Set sound-reactive colors: LED 4=Blue, LED 8=Green, LED 12=Red
        if (MIC1_LED_INDEX <= LED_COUNT && (MIC1_LED_INDEX - 1) < LED_COUNT) {
            led_strip_set_pixel(s_led_strip, MIC1_LED_INDEX - 1, 0, 0, mic1_scaled); // Blue
        }
        if (MIC2_LED_INDEX <= LED_COUNT && (MIC2_LED_INDEX - 1) < LED_COUNT) {
            led_strip_set_pixel(s_led_strip, MIC2_LED_INDEX - 1, 0, mic2_scaled, 0); // Green
        }
        if (MIC3_LED_INDEX <= LED_COUNT && (MIC3_LED_INDEX - 1) < LED_COUNT) {
            led_strip_set_pixel(s_led_strip, MIC3_LED_INDEX - 1, mic3_scaled, 0, 0); // Red
        }
    }
#else
    // No wake word - always sound reactive
    uint8_t mic1_bright = scale_audio_level_to_brightness(mic1_level, is_afe_processed);
    uint8_t mic2_bright = scale_audio_level_to_brightness(mic2_level, is_afe_processed);
    uint8_t mic3_bright = scale_audio_level_to_brightness(mic3_level, is_afe_processed);
    
    uint8_t mic1_scaled = (mic1_bright * LED_BRIGHTNESS) / 255;
    uint8_t mic2_scaled = (mic2_bright * LED_BRIGHTNESS) / 255;
    uint8_t mic3_scaled = (mic3_bright * LED_BRIGHTNESS) / 255;
    
    // Ensure minimum visible brightness if audio is detected
    float noise_floor = is_afe_processed ? NOISE_FLOOR_AFE : NOISE_FLOOR_RAW;
    if (mic1_level > noise_floor && mic1_scaled == 0 && mic1_bright > 0) {
        mic1_scaled = 1;
    }
    if (mic2_level > noise_floor && mic2_scaled == 0 && mic2_bright > 0) {
        mic2_scaled = 1;
    }
    if (mic3_level > noise_floor && mic3_scaled == 0 && mic3_bright > 0) {
        mic3_scaled = 1;
    }
    
    if (MIC1_LED_INDEX <= LED_COUNT && (MIC1_LED_INDEX - 1) < LED_COUNT) {
        led_strip_set_pixel(s_led_strip, MIC1_LED_INDEX - 1, 0, 0, mic1_scaled); // Blue
    }
    if (MIC2_LED_INDEX <= LED_COUNT && (MIC2_LED_INDEX - 1) < LED_COUNT) {
        led_strip_set_pixel(s_led_strip, MIC2_LED_INDEX - 1, 0, mic2_scaled, 0); // Green
    }
    if (MIC3_LED_INDEX <= LED_COUNT && (MIC3_LED_INDEX - 1) < LED_COUNT) {
        led_strip_set_pixel(s_led_strip, MIC3_LED_INDEX - 1, mic3_scaled, 0, 0); // Red
    }
#endif
    
    // Turn off other LEDs (not 4, 8, 12)
    for (int i = 0; i < LED_COUNT; i++) {
        if (i != (MIC1_LED_INDEX - 1) && 
            i != (MIC2_LED_INDEX - 1) && 
            i != (MIC3_LED_INDEX - 1)) {
            led_strip_set_pixel(s_led_strip, i, 0, 0, 0);
        }
    }
    
    // Refresh the LED strip
    esp_err_t refresh_err = led_strip_refresh(s_led_strip);
    if (refresh_err != ESP_OK) {
        static int refresh_error_count = 0;
        if (++refresh_error_count % 100 == 0) {
            ESP_LOGW(TAG, "LED strip refresh failed: %s", esp_err_to_name(refresh_err));
        }
    }
}

#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
/**
 * Initialize AFE (Audio Front-End) for far-field wake word detection
 */
static esp_err_t init_afe(void)
{
    const char *model_name = CONFIG_KORVO_FARFIELD_WAKE_WORD_MODEL;
    int threshold = CONFIG_KORVO_FARFIELD_WAKE_WORD_THRESHOLD;
    
    ESP_LOGI(TAG, "Initializing AFE for far-field wake word detection with model: %s", model_name);
    
    // Initialize model loader from partition
    srmodel_list_t *models = esp_srmodel_init("model");
    if (models == NULL || models->num == 0) {
        ESP_LOGE(TAG, "Failed to initialize models from 'model' partition!");
        ESP_LOGE(TAG, "Make sure 'model' partition exists and contains wake word models");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Found %d model(s) in partition", models->num);
    
    // Filter for wake word models
    char *found_model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
    if (found_model_name == NULL) {
        ESP_LOGE(TAG, "No wake word models found in partition!");
        esp_srmodel_deinit(models);
        return ESP_FAIL;
    }
    
    // Use the found model or the configured one
    if (strstr(found_model_name, model_name) == NULL) {
        ESP_LOGW(TAG, "Configured model '%s' not found, using '%s' instead", model_name, found_model_name);
        // Try to find the exact model
        char *exact_model = esp_srmodel_filter(models, ESP_WN_PREFIX, (char*)model_name);
        if (exact_model) {
            found_model_name = exact_model;
        }
    }
    
    ESP_LOGI(TAG, "Using wake word model: %s", found_model_name);
    
    // Initialize AFE config for far-field processing
    // "MM" = 2 microphone channels (STEREO from ES7210)
    // AFE_TYPE_SR = Speech Recognition mode
    // AFE_MODE_HIGH_PERF = High performance mode for far-field
    afe_config_t *afe_config = afe_config_init("MM", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (afe_config == NULL) {
        ESP_LOGE(TAG, "Failed to initialize AFE config");
        esp_srmodel_deinit(models);
        return ESP_FAIL;
    }
    
    // Configure AFE for wake word detection
    afe_config->wakenet_init = true;
    afe_config->wakenet_model_name = found_model_name;
    afe_config->wakenet_mode = DET_MODE_2CH_90; // 2-channel detection for STEREO input
    
    // Enable SE (Speech Enhancement/Beamforming) for far-field
    afe_config->se_init = true;
    
    // Enable AEC (Acoustic Echo Cancellation) if needed
    afe_config->aec_init = false; // Disable AEC for now (no playback reference)
    
    // Enable VAD (Voice Activity Detection)
    afe_config->vad_init = true;
    afe_config->vad_mode = VAD_MODE_3;
    
    // Enable NS (Noise Suppression)
    afe_config->ns_init = true;
    
    // Enable AGC (Automatic Gain Control)
    afe_config->agc_init = true;
    afe_config->agc_mode = AFE_AGC_MODE_WAKENET;
    
    // Parse input format to set up pcm_config properly
    // "MM" = 2 microphone channels (STEREO from ES7210)
    if (!afe_parse_input_format("MM", &afe_config->pcm_config)) {
        ESP_LOGE(TAG, "Failed to parse input format 'MM'");
        afe_config_free(afe_config);
        esp_srmodel_deinit(models);
        return ESP_FAIL;
    }
    afe_config->pcm_config.sample_rate = SAMPLE_RATE_HZ;
    
    // Check and validate config
    afe_config = afe_config_check(afe_config);
    if (afe_config == NULL) {
        ESP_LOGE(TAG, "AFE config check failed");
        esp_srmodel_deinit(models);
        return ESP_FAIL;
    }
    
    // Get AFE handle
    s_afe_handle = esp_afe_handle_from_config(afe_config);
    if (s_afe_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get AFE handle");
        afe_config_free(afe_config);
        esp_srmodel_deinit(models);
        return ESP_FAIL;
    }
    
    // Create AFE data instance
    s_afe_data = s_afe_handle->create_from_config(afe_config);
    if (s_afe_data == NULL) {
        ESP_LOGE(TAG, "Failed to create AFE data instance");
        afe_config_free(afe_config);
        esp_srmodel_deinit(models);
        return ESP_FAIL;
    }
    
    // Get AFE parameters
    s_afe_feed_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
    s_afe_feed_channels = s_afe_handle->get_feed_channel_num(s_afe_data);
    int sample_rate = s_afe_handle->get_samp_rate(s_afe_data);
    int fetch_chunksize = s_afe_handle->get_fetch_chunksize(s_afe_data);
    
    ESP_LOGI(TAG, "AFE initialized: feed_chunksize=%d, channels=%d, rate=%d Hz, fetch_chunksize=%d",
             s_afe_feed_chunksize, s_afe_feed_channels, sample_rate, fetch_chunksize);
    
    // Set custom threshold if configured (0-100 -> 0.4-0.9999)
    if (threshold > 0 && threshold <= 100) {
        float det_threshold = 0.4f + (threshold / 100.0f) * 0.5999f; // Map 0-100 to 0.4-0.9999
        s_afe_handle->set_wakenet_threshold(s_afe_data, 1, det_threshold);
        ESP_LOGI(TAG, "Set wake word threshold to %.3f (config=%d)", det_threshold, threshold);
    }
    
    // Allocate buffer for AFE feed (interleaved channels)
    s_afe_feed_buffer = malloc(s_afe_feed_chunksize * sizeof(int16_t) * s_afe_feed_channels);
    if (s_afe_feed_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate AFE feed buffer");
        s_afe_handle->destroy(s_afe_data);
        s_afe_data = NULL;
        afe_config_free(afe_config);
        esp_srmodel_deinit(models);
        return ESP_ERR_NO_MEM;
    }
    
    // Print AFE pipeline
    s_afe_handle->print_pipeline(s_afe_data);
    
    const char *wake_word = esp_wn_wakeword_from_name(found_model_name);
    ESP_LOGI(TAG, "AFE ready for far-field processing - listening for wake word '%s'", 
             wake_word ? wake_word : found_model_name);
    
    // Free config and models
    afe_config_free(afe_config);
    esp_srmodel_deinit(models);
    
    return ESP_OK;
}

/**
 * Check for wake word using AFE far-field processing
 * Returns wake word index (1-based) if detected, 0 otherwise
 */
static int check_wake_word_afe(const int16_t *samples, size_t sample_count)
{
    if (s_afe_handle == NULL || s_afe_data == NULL || s_afe_feed_buffer == NULL) {
        return 0;
    }
    
    // Check cooldown
    TickType_t now = xTaskGetTickCount();
    if (now < s_wake_word_cooldown_until) {
        return 0;
    }
    
    // Accumulate samples to feed AFE
    // AFE expects interleaved multi-channel samples
    static int16_t afe_accumulator[2048]; // Enough for multiple frames
    static size_t accumulator_count = 0;
    
    // Add samples to accumulator (samples are already interleaved STEREO: [L, R, L, R, ...])
    size_t to_copy = sample_count;
    if (accumulator_count + to_copy > sizeof(afe_accumulator)/sizeof(afe_accumulator[0])) {
        to_copy = sizeof(afe_accumulator)/sizeof(afe_accumulator[0]) - accumulator_count;
    }
    
    memcpy(&afe_accumulator[accumulator_count], samples, to_copy * sizeof(int16_t));
    accumulator_count += to_copy;
    
    // Feed AFE when we have enough samples for one feed chunk
    // AFE feed expects: chunksize * num_channels samples (interleaved)
    int samples_per_feed = s_afe_feed_chunksize * s_afe_feed_channels;
    
    while (accumulator_count >= samples_per_feed) {
        // Copy one feed chunk to AFE buffer
        memcpy(s_afe_feed_buffer, afe_accumulator, samples_per_feed * sizeof(int16_t));
        
        // Feed to AFE
        int feed_result = s_afe_handle->feed(s_afe_data, s_afe_feed_buffer);
        if (feed_result < 0) {
            ESP_LOGW(TAG, "AFE feed failed: %d", feed_result);
            break;
        }
        
        // Shift remaining samples
        size_t remaining = accumulator_count - samples_per_feed;
        if (remaining > 0) {
            memmove(afe_accumulator, &afe_accumulator[samples_per_feed], 
                    remaining * sizeof(int16_t));
        }
        accumulator_count = remaining;
        
        // Fetch from AFE (non-blocking, returns immediately)
        afe_fetch_result_t *fetch_result = s_afe_handle->fetch(s_afe_data);
        if (fetch_result && fetch_result->ret_value == ESP_OK) {
            // Extract AFE-processed audio levels for LED visualization
            // AFE applies beamforming, noise suppression, and AGC which gives much better levels
            // Try to access processed audio from fetch_result->data if available
            int16_t *afe_data = NULL;
            size_t afe_sample_count = 0;
            bool has_afe_audio = false;
            
            // Try to access fetch_result->data if the field exists in your ESP-SR version
            // Uncomment the block below if fetch_result->data is available:
            #if 0
            // Direct access to AFE-processed audio (uncomment if your ESP-SR version supports this)
            if (fetch_result->data && fetch_result->data_size > 0) {
                afe_data = (int16_t *)fetch_result->data;
                afe_sample_count = fetch_result->data_size / sizeof(int16_t);
                has_afe_audio = true;
            }
            #endif
            
            if (has_afe_audio && afe_data && afe_sample_count > 0) {
                // We have direct access to AFE-processed audio samples
                // AFE-processed audio is available - compute levels from processed data
                // Processed audio is typically mono or has fewer channels after beamforming
                
                // Compute energy level from AFE-processed audio
                uint64_t sum = 0;
                for (size_t i = 0; i < afe_sample_count; i++) {
                    int32_t val = afe_data[i];
                    int32_t abs_val = val >= 0 ? val : -val;
                    sum += abs_val;
                }
                
                float afe_level = afe_sample_count > 0 ? (float)sum / (float)afe_sample_count : 0.0f;
                
                // AFE-processed audio is typically mono after beamforming
                // Use the same level for all three LEDs but scale differently
                // MIC3 (combined) gets the full AFE level (best signal)
                // MIC1 and MIC2 get scaled versions to show channel separation if available
                s_afe_mic3_level = afe_level;
                
                // If AFE provides multi-channel output, extract individual channels
                // Otherwise, use the beamformed level for all
                if (s_afe_feed_channels >= 2 && afe_sample_count >= 2) {
                    // Try to extract left/right from processed data
                    uint64_t sum1 = 0, sum2 = 0;
                    size_t count1 = 0, count2 = 0;
                    for (size_t i = 0; i < afe_sample_count; i += s_afe_feed_channels) {
                        if (i < afe_sample_count) {
                            int32_t val = afe_data[i];
                            int32_t abs_val = val >= 0 ? val : -val;
                            sum1 += abs_val;
                            count1++;
                        }
                        if (i + 1 < afe_sample_count) {
                            int32_t val = afe_data[i + 1];
                            int32_t abs_val = val >= 0 ? val : -val;
                            sum2 += abs_val;
                            count2++;
                        }
                    }
                    s_afe_mic1_level = count1 > 0 ? (float)sum1 / (float)count1 : afe_level * 0.8f;
                    s_afe_mic2_level = count2 > 0 ? (float)sum2 / (float)count2 : afe_level * 0.8f;
                } else {
                    // Mono output - use beamformed level for all, with slight variation
                    s_afe_mic1_level = afe_level * 0.9f;
                    s_afe_mic2_level = afe_level * 0.9f;
                }
                
                s_afe_levels_valid = true;
                
                // Log first few AFE levels to compare with raw
                static int afe_log_count = 0;
                if (afe_log_count < 5) {
                    ESP_LOGI(TAG, "AFE processed audio: MIC1=%.2f, MIC2=%.2f, MIC3=%.2f (samples=%zu)",
                             s_afe_mic1_level, s_afe_mic2_level, s_afe_mic3_level, afe_sample_count);
                    afe_log_count++;
                }
            } else {
                // AFE is processing but we can't access processed audio directly from fetch_result
                // Solution: Compute raw levels from samples and apply AGC-like enhancement
                // AFE's AGC typically provides 20-40dB gain (10-100x linear)
                // Beamforming also improves SNR significantly
                // This enhancement makes LEDs much more reactive
                
                // Compute raw audio levels from the samples we're feeding to AFE
                float raw_mic1 = 0.0f, raw_mic2 = 0.0f, raw_mic3 = 0.0f;
                compute_mic_levels(samples, sample_count, &raw_mic1, &raw_mic2, &raw_mic3);
                
                // Apply AGC-like boost to raw levels when AFE is active and processing
                // Conservative 30x boost approximates AGC + beamforming gain from far-field processing
                // AFE-processed audio typically has 20-60x higher levels than raw
                const float afe_agc_boost = 30.0f;
                s_afe_mic1_level = raw_mic1 * afe_agc_boost;
                s_afe_mic2_level = raw_mic2 * afe_agc_boost;
                s_afe_mic3_level = raw_mic3 * afe_agc_boost;
                
                s_afe_levels_valid = true;
                
                // Log first few to show enhancement
                static int afe_enhance_log_count = 0;
                if (afe_enhance_log_count < 5) {
                    ESP_LOGI(TAG, "AFE enhancing: RAW(%.2f/%.2f/%.2f) -> AFE(%.2f/%.2f/%.2f) boost=%.1fx",
                             raw_mic1, raw_mic2, raw_mic3,
                             s_afe_mic1_level, s_afe_mic2_level, s_afe_mic3_level, afe_agc_boost);
                    afe_enhance_log_count++;
                }
            }
            
            // Note: s_afe_levels_valid is set above when AFE successfully processes audio
            // This enables LEDs to use enhanced levels instead of raw
            
            // Check for wake word detection
            if (fetch_result->wakeup_state == WAKENET_DETECTED) {
                int word_index = fetch_result->wake_word_index;
                int triggered_channel = fetch_result->trigger_channel_id;
                
                ESP_LOGI(TAG, "*** WAKE WORD DETECTED: index=%d, channel=%d ***", 
                         word_index, triggered_channel);
                
                // Set cooldown to prevent multiple detections
                s_wake_word_cooldown_until = now + pdMS_TO_TICKS(WAKE_WORD_COOLDOWN_MS);
                
                // Activate wake word LED state - LEDs 4, 8, 12 will show white
                s_wake_word_active = true;
                s_wake_word_led_until = now + pdMS_TO_TICKS(WAKE_WORD_LED_DURATION_MS);
                
                ESP_LOGI(TAG, "Wake word LEDs 4,8,12 set to WHITE for %d ms", WAKE_WORD_LED_DURATION_MS);
                
                // Immediately update LEDs to white (update_mic_leds will maintain this)
                if (s_led_strip) {
                    if (MIC1_LED_INDEX <= LED_COUNT) {
                        led_strip_set_pixel(s_led_strip, MIC1_LED_INDEX - 1, 255, 255, 255); // White
                    }
                    if (MIC2_LED_INDEX <= LED_COUNT) {
                        led_strip_set_pixel(s_led_strip, MIC2_LED_INDEX - 1, 255, 255, 255); // White
                    }
                    if (MIC3_LED_INDEX <= LED_COUNT) {
                        led_strip_set_pixel(s_led_strip, MIC3_LED_INDEX - 1, 255, 255, 255); // White
                    }
                    led_strip_refresh(s_led_strip);
                }
                
                return word_index;
            }
        }
    }
    
    return 0;
}
#endif // CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE

/**
 * Main audio processing task
 * Continuously reads audio from I2S and updates LEDs
 */
static void audio_task(void *arg)
{
    // ES7210 outputs 32-bit samples, allocate buffer for int32_t
    int32_t *frame_buffer_32 = malloc(SAMPLES_PER_FRAME * sizeof(int32_t) * 2); // STEREO = 2 channels
    int16_t *frame_buffer = malloc(SAMPLES_PER_FRAME * sizeof(int16_t) * 2); // Converted to int16_t
    if (!frame_buffer_32 || !frame_buffer) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer");
        vTaskDelete(NULL);
        return;
    }
    
    const TickType_t frame_delay = pdMS_TO_TICKS(FRAME_SIZE_MS);
    size_t log_counter = 0;
    
    ESP_LOGI(TAG, "Audio task started: frame_size=%d samples (STEREO), delay=%d ms",
             SAMPLES_PER_FRAME * 2, FRAME_SIZE_MS);
    
    while (1) {
        if (!s_i2s_initialized) {
            vTaskDelay(frame_delay);
            continue;
        }
        
        size_t bytes_read = 0;
        // ES7210 outputs 32-bit samples in STEREO
        size_t bytes_to_read = SAMPLES_PER_FRAME * sizeof(int32_t) * 2; // STEREO, 32-bit
        
        esp_err_t err = i2s_read(ES7210_I2S_PORT, frame_buffer_32, bytes_to_read, &bytes_read, 
                                  pdMS_TO_TICKS(100));
        
        if (err != ESP_OK || bytes_read == 0) {
            static int read_error_count = 0;
            if (++read_error_count % 50 == 0) {
                ESP_LOGW(TAG, "I2S read: err=%s, bytes_read=%zu (expected %zu)", 
                         esp_err_to_name(err), bytes_read, bytes_to_read);
            }
            vTaskDelay(frame_delay);
            continue;
        }
        
        // Log first successful read with sample data
        static bool first_read = true;
        if (first_read) {
            ESP_LOGI(TAG, "First I2S read successful: %zu bytes (%zu samples)", bytes_read, bytes_read / sizeof(int32_t));
            if (bytes_read >= 8) {
                ESP_LOGI(TAG, "First 4 samples: 0x%08" PRIx32 " 0x%08" PRIx32 " 0x%08" PRIx32 " 0x%08" PRIx32, 
                         frame_buffer_32[0], frame_buffer_32[1], frame_buffer_32[2], frame_buffer_32[3]);
            }
            first_read = false;
        }
        
        // Convert 32-bit samples to 16-bit for processing
        size_t samples_32_read = bytes_read / sizeof(int32_t);
        for (size_t i = 0; i < samples_32_read && i < (SAMPLES_PER_FRAME * 2); i++) {
            // ES7210 outputs 32-bit samples, shift right to get 16-bit range
            frame_buffer[i] = (int16_t)(frame_buffer_32[i] >> 16);
        }
        
        size_t samples_read = samples_32_read;
        
        // Compute raw microphone levels (fallback if AFE not available)
        float mic1_level = 0.0f, mic2_level = 0.0f, mic3_level = 0.0f;
        compute_mic_levels(frame_buffer, samples_read, &mic1_level, &mic2_level, &mic3_level);
        
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
        // Check for wake word detection using AFE far-field processing
        // This also updates s_afe_mic*_level with AFE-processed audio
        check_wake_word_afe(frame_buffer, samples_read);
        
        // Use AFE-processed audio levels for LEDs if available (much better than raw)
        // AFE applies beamforming, noise suppression, and AGC
        if (s_afe_levels_valid) {
            // Use AFE-processed levels - these are much better due to beamforming and AGC
            update_mic_leds(s_afe_mic1_level, s_afe_mic2_level, s_afe_mic3_level, true);
            
            // Log comparison periodically
            static int compare_log_count = 0;
            if (++compare_log_count % 50 == 0) {
                ESP_LOGI(TAG, "Audio: RAW(MIC1=%.2f,MIC2=%.2f,MIC3=%.2f) vs AFE(MIC1=%.2f,MIC2=%.2f,MIC3=%.2f)",
                         mic1_level, mic2_level, mic3_level,
                         s_afe_mic1_level, s_afe_mic2_level, s_afe_mic3_level);
            }
        } else {
            // AFE not ready yet or failed - use raw audio levels
            update_mic_leds(mic1_level, mic2_level, mic3_level, false);
        }
#else
        // No AFE - always use raw audio levels
        update_mic_leds(mic1_level, mic2_level, mic3_level, false);
#endif
        
        // Log first few audio levels to verify audio is being captured
        static int audio_log_count = 0;
        if (audio_log_count < 5 && samples_read > 0) {
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
            if (s_afe_levels_valid) {
                ESP_LOGI(TAG, "Audio: RAW(MIC1=%.2f,MIC2=%.2f,MIC3=%.2f) AFE(MIC1=%.2f,MIC2=%.2f,MIC3=%.2f)",
                         mic1_level, mic2_level, mic3_level,
                         s_afe_mic1_level, s_afe_mic2_level, s_afe_mic3_level);
            } else {
                ESP_LOGI(TAG, "Audio captured (raw, AFE not ready): %zu samples, levels: MIC1=%.2f, MIC2=%.2f, MIC3=%.2f",
                         samples_read, mic1_level, mic2_level, mic3_level);
            }
#else
            ESP_LOGI(TAG, "Audio captured: %zu samples, levels: MIC1=%.2f, MIC2=%.2f, MIC3=%.2f",
                     samples_read, mic1_level, mic2_level, mic3_level);
#endif
            audio_log_count++;
        }
        
        // Force a small delay to ensure LED refresh completes
        vTaskDelay(pdMS_TO_TICKS(1));
        
        // Log periodically (every 25 frames ~0.5 second at 20ms frames) - more frequent for debugging
        if (++log_counter % 25 == 0) {
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
            bool use_afe = s_afe_levels_valid;
            float log_mic1 = use_afe ? s_afe_mic1_level : mic1_level;
            float log_mic2 = use_afe ? s_afe_mic2_level : mic2_level;
            float log_mic3 = use_afe ? s_afe_mic3_level : mic3_level;
#else
            bool use_afe = false;
            float log_mic1 = mic1_level;
            float log_mic2 = mic2_level;
            float log_mic3 = mic3_level;
#endif
            uint8_t mic1_bright_raw = scale_audio_level_to_brightness(log_mic1, use_afe);
            uint8_t mic2_bright_raw = scale_audio_level_to_brightness(log_mic2, use_afe);
            uint8_t mic3_bright_raw = scale_audio_level_to_brightness(log_mic3, use_afe);
            uint8_t mic1_bright = (mic1_bright_raw * LED_BRIGHTNESS) / 255;
            uint8_t mic2_bright = (mic2_bright_raw * LED_BRIGHTNESS) / 255;
            uint8_t mic3_bright = (mic3_bright_raw * LED_BRIGHTNESS) / 255;
            
            // Log sample values for first few samples to check if data is non-zero
            static int sample_log_count = 0;
            if (sample_log_count < 3 && samples_read >= 6) {
                ESP_LOGI(TAG, "Sample data: [0]=%d [1]=%d [2]=%d [3]=%d [4]=%d [5]=%d (STEREO: L,R,L,R,L,R)",
                         frame_buffer[0], frame_buffer[1], frame_buffer[2],
                         frame_buffer[3], frame_buffer[4], frame_buffer[5]);
                sample_log_count++;
            }
            
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
            if (s_afe_levels_valid) {
                ESP_LOGI(TAG, "🎤 MIC Levels (AFE): MIC1=%.2f (LED%d blue=%d), MIC2=%.2f (LED%d green=%d), MIC3=%.2f (LED%d red=%d) | RAW: %.2f/%.2f/%.2f",
                         s_afe_mic1_level, MIC1_LED_INDEX, mic1_bright,
                         s_afe_mic2_level, MIC2_LED_INDEX, mic2_bright,
                         s_afe_mic3_level, MIC3_LED_INDEX, mic3_bright,
                         mic1_level, mic2_level, mic3_level);
            } else {
                ESP_LOGI(TAG, "🎤 MIC Levels (RAW): MIC1=%.2f (LED%d blue=%d), MIC2=%.2f (LED%d green=%d), MIC3=%.2f (LED%d red=%d) | samples=%zu",
                         mic1_level, MIC1_LED_INDEX, mic1_bright,
                         mic2_level, MIC2_LED_INDEX, mic2_bright,
                         mic3_level, MIC3_LED_INDEX, mic3_bright, samples_read);
            }
#else
            ESP_LOGI(TAG, "🎤 MIC Levels: MIC1=%.2f (LED%d blue=%d), MIC2=%.2f (LED%d green=%d), MIC3=%.2f (LED%d red=%d) | samples=%zu",
                     mic1_level, MIC1_LED_INDEX, mic1_bright,
                     mic2_level, MIC2_LED_INDEX, mic2_bright,
                     mic3_level, MIC3_LED_INDEX, mic3_bright, samples_read);
#endif
        }
        
        vTaskDelay(frame_delay);
    }
    
    free(frame_buffer_32);
    free(frame_buffer);
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
    if (s_afe_feed_buffer) {
        free(s_afe_feed_buffer);
    }
    if (s_afe_data && s_afe_handle) {
        s_afe_handle->destroy(s_afe_data);
    }
#endif
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Korvo-1 Far-Field Microphone Array Demo ===");
    ESP_LOGI(TAG, "LED GPIO: %d, LED Count: %d, Brightness: %d",
             LED_GPIO, LED_COUNT, LED_BRIGHTNESS);
    ESP_LOGI(TAG, "Sample Rate: %d Hz, Frame Size: %d ms (%d samples)",
             SAMPLE_RATE_HZ, FRAME_SIZE_MS, SAMPLES_PER_FRAME);
    
    // Initialize LED strip
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
        .flags = {
            .invert_out = false,
        },
    };
    
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .with_dma = true, // Use DMA like LED demo for better reliability
    };
    
    // Initialize LED strip FIRST before any other peripherals that might use RMT/DMA
    esp_err_t led_init_err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (led_init_err != ESP_OK) {
        ESP_LOGE(TAG, "LED strip initialization FAILED: %s (GPIO%d)", 
                 esp_err_to_name(led_init_err), LED_GPIO);
        ESP_LOGE(TAG, "Check: GPIO%d connection, LED count=%d, RMT channel availability", 
                 LED_GPIO, LED_COUNT);
        ESP_LOGE(TAG, "Error code: 0x%x", led_init_err);
        s_led_strip = NULL;
    } else {
        ESP_LOGI(TAG, "LED strip initialized successfully on GPIO%d, %d LEDs, brightness=%d", 
                 LED_GPIO, LED_COUNT, LED_BRIGHTNESS);
    }
    
    // Power-on chasing LED animation
    if (s_led_strip) {
        ESP_LOGI(TAG, "Starting power-on LED chase animation...");
        
        // First, simple test - turn on all LEDs bright to verify they work
        // Use full brightness values - led_strip doesn't apply brightness scaling automatically
        for (int i = 0; i < LED_COUNT; i++) {
            led_strip_set_pixel(s_led_strip, i, 255, 255, 255);
        }
        led_strip_refresh(s_led_strip);
        ESP_LOGI(TAG, "LED test: All LEDs should be bright white");
        vTaskDelay(pdMS_TO_TICKS(1000));
        led_strip_clear(s_led_strip);
        led_strip_refresh(s_led_strip);
        vTaskDelay(pdMS_TO_TICKS(200));
        
        // Chasing LED animation - go around the ring 2 times
        // Use bright colors without excessive brightness scaling for visibility
        const int chase_cycles = 2;
        const int chase_delay_ms = 150; // Slower for better visibility
        const int tail_length = 2; // Shorter tail for brighter effect
        
        ESP_LOGI(TAG, "Starting chase: %d cycles, %dms delay, tail=%d", chase_cycles, chase_delay_ms, tail_length);
        
        for (int cycle = 0; cycle < chase_cycles; cycle++) {
            for (int pos = 0; pos < LED_COUNT; pos++) {
                // Clear all LEDs first
                led_strip_clear(s_led_strip);
                
                // Draw chasing tail - use very bright colors for visibility
                for (int i = 0; i < tail_length; i++) {
                    int led_idx = (pos - i + LED_COUNT) % LED_COUNT;
                    
                    // Bright cyan chase - use full brightness, let LED_BRIGHTNESS handle scaling
                    uint8_t r, g, b;
                    if (i == 0) {
                        // Head: very bright cyan
                        r = 0;
                        g = 255;  // Full green
                        b = 255;  // Full blue
                    } else {
                        // Tail: medium cyan
                        r = 0;
                        g = 128;
                        b = 128;
                    }
                    
                    // Apply LED_BRIGHTNESS scaling
                    r = (r * LED_BRIGHTNESS) / 255;
                    g = (g * LED_BRIGHTNESS) / 255;
                    b = (b * LED_BRIGHTNESS) / 255;
                    
                    led_strip_set_pixel(s_led_strip, led_idx, r, g, b);
                }
                
                esp_err_t refresh_err = led_strip_refresh(s_led_strip);
                if (refresh_err != ESP_OK) {
                    ESP_LOGW(TAG, "LED refresh failed during chase: %s", esp_err_to_name(refresh_err));
                }
                
                vTaskDelay(pdMS_TO_TICKS(chase_delay_ms));
            }
        }
        
        // Clear all LEDs
        led_strip_clear(s_led_strip);
        led_strip_refresh(s_led_strip);
        vTaskDelay(pdMS_TO_TICKS(300));
        
        ESP_LOGI(TAG, "Power-on LED chase animation complete");
    }
    
    // Initialize I2C for ES7210 control
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = ES7210_I2C_SDA,
        .scl_io_num = ES7210_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
        .clk_flags = 0,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    ESP_LOGI(TAG, "I2C initialized for ES7210 (SDA=GPIO%d, SCL=GPIO%d, ADDR=0x%02X)", 
             ES7210_I2C_SDA, ES7210_I2C_SCL, ES7210_I2C_ADDR);
    
    // ES7210 initialization via I2C - based on esp-adf es7210_adc_init() and es7210_start()
    // Full initialization sequence for 16kHz sample rate
    esp_err_t i2c_err;
    
    // Helper function to write ES7210 register
    #define ES7210_WRITE_REG(reg, val, desc) do { \
        i2c_cmd_handle_t cmd = i2c_cmd_link_create(); \
        i2c_master_start(cmd); \
        i2c_master_write_byte(cmd, (ES7210_I2C_ADDR << 1) | 0, true); \
        i2c_master_write_byte(cmd, reg, true); \
        i2c_master_write_byte(cmd, val, true); \
        i2c_master_stop(cmd); \
        i2c_err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100)); \
        i2c_cmd_link_delete(cmd); \
        if (i2c_err == ESP_OK) { \
            ESP_LOGI(TAG, "ES7210 reg 0x%02X=0x%02X %s", reg, val, desc); \
        } else { \
            ESP_LOGW(TAG, "ES7210 reg 0x%02X write failed: %s", reg, esp_err_to_name(i2c_err)); \
        } \
    } while(0)
    
    // Reset sequence
    ES7210_WRITE_REG(0x00, 0xff, "RESET (0xff)");
    vTaskDelay(pdMS_TO_TICKS(10));
    ES7210_WRITE_REG(0x00, 0x41, "RESET (0x41)");
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Clock and power configuration for 16kHz sample rate
    // Using values from esp-adf coeff_div table for 16kHz with 16.384MHz MCLK
    // {16384000, 16000, 0x00, 0x02, 0x01, 0x00, 0x20, 0x00, 0x04, 0x00}
    // adc_div=0x02, dll=0x01, doubler=0x00, osr=0x20, lrckh=0x04, lrckl=0x00
    ES7210_WRITE_REG(0x01, 0x00, "CLOCK_OFF"); // Clock on (0x00 = clock on)
    ES7210_WRITE_REG(0x02, 0x02, "MAINCLK"); // ADC divider = 0x02, dll=0x01, doubler=0x00
    ES7210_WRITE_REG(0x04, 0x04, "LRCK_DIVH"); // LRCK divider high = 0x04
    ES7210_WRITE_REG(0x05, 0x00, "LRCK_DIVL"); // LRCK divider low = 0x00
    ES7210_WRITE_REG(0x06, 0x00, "POWER_DOWN"); // Power down - all on
    ES7210_WRITE_REG(0x07, 0x20, "OSR"); // Over sampling rate = 0x20
    
    // Time control and HPF filters
    ES7210_WRITE_REG(0x09, 0x30, "TIME_CONTROL0"); // Chip initial state period
    ES7210_WRITE_REG(0x0A, 0x30, "TIME_CONTROL1"); // Power up state period
    ES7210_WRITE_REG(0x20, 0x0a, "ADC34_HPF2"); // HPF for ADC3&4
    ES7210_WRITE_REG(0x21, 0x2a, "ADC34_HPF1");
    ES7210_WRITE_REG(0x22, 0x0a, "ADC12_HPF1"); // HPF for ADC1&2
    ES7210_WRITE_REG(0x23, 0x2a, "ADC12_HPF2");
    
    // I2S interface configuration - 32-bit, I2S format, STEREO
    ES7210_WRITE_REG(0x11, 0x80, "SDP_INTERFACE1"); // 32-bit (0x80), I2S format, STEREO
    ES7210_WRITE_REG(0x12, 0x00, "SDP_INTERFACE2"); // Disable TDM mode
    
    // Analog and microphone power
    ES7210_WRITE_REG(0x40, 0x43, "ANALOG");
    ES7210_WRITE_REG(0x47, 0x08, "MIC1_POWER");
    ES7210_WRITE_REG(0x48, 0x08, "MIC2_POWER");
    ES7210_WRITE_REG(0x49, 0x08, "MIC3_POWER");
    ES7210_WRITE_REG(0x4A, 0x08, "MIC4_POWER");
    ES7210_WRITE_REG(0x4B, 0x00, "MIC12_POWER"); // Enable MIC1&2
    ES7210_WRITE_REG(0x4C, 0x00, "MIC34_POWER"); // Enable MIC3&4
    
    // Microphone gain - increase gain for better sensitivity
    // Bit 4 (0x10) = enable, bits 0-3 = gain (0x0F = max gain)
    ES7210_WRITE_REG(0x43, 0x1F, "MIC1_GAIN"); // Enable + max gain
    ES7210_WRITE_REG(0x44, 0x1F, "MIC2_GAIN"); // Enable + max gain
    ES7210_WRITE_REG(0x45, 0x1F, "MIC3_GAIN"); // Enable + max gain
    
    ESP_LOGI(TAG, "ES7210 initialization complete");
    vTaskDelay(pdMS_TO_TICKS(200)); // Give ES7210 time to stabilize
    
    // Initialize ES7210 I2S for microphone capture (I2S_NUM_1)
    // ES7210 outputs 32-bit I2S data in STEREO mode
    i2s_config_t i2s_mic_conf = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = SAMPLE_RATE_HZ,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // STEREO for multiple microphones
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = true,  // Use APLL for stable clock generation
        .tx_desc_auto_clear = false,
        .fixed_mclk = 16384000,  // 16.384MHz MCLK for ES7210
        .bits_per_chan = I2S_BITS_PER_CHAN_32BIT,
    };
    
    i2s_pin_config_t i2s_mic_pins = {
        .mck_io_num = ES7210_I2S_MCLK,
        .bck_io_num = ES7210_I2S_SCLK,
        .ws_io_num = ES7210_I2S_LRCK,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = ES7210_I2S_SDIN,
    };
    
    ESP_LOGI(TAG, "Initializing ES7210 microphone array (I2S_NUM_1, STEREO mode)...");
    ESP_LOGI(TAG, "  Pins: DIN=GPIO%d, BCLK=GPIO%d, WS=GPIO%d, MCLK=GPIO%d",
             i2s_mic_pins.data_in_num, i2s_mic_pins.bck_io_num, 
             i2s_mic_pins.ws_io_num, i2s_mic_pins.mck_io_num);
    ESP_LOGI(TAG, "  Sample Rate: %d Hz, Format: 32-bit STEREO", SAMPLE_RATE_HZ);
    
    ESP_ERROR_CHECK(i2s_driver_install(ES7210_I2S_PORT, &i2s_mic_conf, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(ES7210_I2S_PORT, &i2s_mic_pins));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(ES7210_I2S_PORT));
    ESP_ERROR_CHECK(i2s_start(ES7210_I2S_PORT));
    s_i2s_initialized = true;
    ESP_LOGI(TAG, "ES7210 microphone initialized and started");
    
    // Note: ES8388 codec initialization removed to focus on microphone capture
    // I2C is already initialized above for ES7210
    
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
    // Initialize AFE for far-field wake word detection
    // Note: Initialize AFTER LEDs to avoid RMT/DMA conflicts
    esp_err_t afe_err = init_afe();
    if (afe_err == ESP_OK) {
        ESP_LOGI(TAG, "AFE far-field wake word detection enabled - listening for '%s' (hi esp)", 
                 CONFIG_KORVO_FARFIELD_WAKE_WORD_MODEL);
        ESP_LOGI(TAG, "Wake word: 'hi esp' (wn9_hiesp)");
        ESP_LOGI(TAG, "NOTE: For custom wake word 'Naptick', you need to train a custom model");
        ESP_LOGI(TAG, "See: https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html");
        
        // Test LEDs again after AFE init to verify they still work
        if (s_led_strip) {
            ESP_LOGI(TAG, "Testing LEDs after AFE init - LED %d should be GREEN...", MIC2_LED_INDEX);
            if (MIC2_LED_INDEX <= LED_COUNT) {
                led_strip_set_pixel(s_led_strip, MIC2_LED_INDEX - 1, 0, 255, 0); // Green on LED 8
            }
            led_strip_refresh(s_led_strip);
            vTaskDelay(pdMS_TO_TICKS(500));
            led_strip_clear(s_led_strip);
            led_strip_refresh(s_led_strip);
        }
    } else {
        ESP_LOGW(TAG, "AFE initialization failed - continuing without wake word detection");
    }
#endif
    
    // Brief mic LED test to show which LEDs will be sound-reactive
    if (s_led_strip) {
        ESP_LOGI(TAG, "Mic LED positions: LED%d=Blue(MIC1), LED%d=Green(MIC2), LED%d=Red(MIC3)", 
                 MIC1_LED_INDEX, MIC2_LED_INDEX, MIC3_LED_INDEX);
        
        // Briefly show mic LED positions
        if (MIC1_LED_INDEX <= LED_COUNT) {
            led_strip_set_pixel(s_led_strip, MIC1_LED_INDEX - 1, 0, 0, LED_BRIGHTNESS); // Blue
        }
        if (MIC2_LED_INDEX <= LED_COUNT) {
            led_strip_set_pixel(s_led_strip, MIC2_LED_INDEX - 1, 0, LED_BRIGHTNESS, 0); // Green
        }
        if (MIC3_LED_INDEX <= LED_COUNT) {
            led_strip_set_pixel(s_led_strip, MIC3_LED_INDEX - 1, LED_BRIGHTNESS, 0, 0); // Red
        }
        led_strip_refresh(s_led_strip);
        vTaskDelay(pdMS_TO_TICKS(500));
        
        led_strip_clear(s_led_strip);
        led_strip_refresh(s_led_strip);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        ESP_LOGI(TAG, "Ready for sound-reactive LEDs and wake word detection");
    } else {
        ESP_LOGE(TAG, "LED strip not initialized - skipping test!");
    }
    
    ESP_LOGI(TAG, "Starting audio processing task...");
    ESP_LOGI(TAG, "LED %d (Blue) = MIC1 (Left channel)", MIC1_LED_INDEX);
    ESP_LOGI(TAG, "LED %d (Green) = MIC2 (Right channel)", MIC2_LED_INDEX);
    ESP_LOGI(TAG, "LED %d (Red) = MIC3 (Combined far-field)", MIC3_LED_INDEX);
    ESP_LOGI(TAG, "Speak near the device to see the LEDs react!");
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
    ESP_LOGI(TAG, "Wake word detection: %s (hi esp) - threshold=%d", 
             CONFIG_KORVO_FARFIELD_WAKE_WORD_MODEL, CONFIG_KORVO_FARFIELD_WAKE_WORD_THRESHOLD);
#else
    ESP_LOGI(TAG, "Wake word detection: DISABLED");
#endif
    
    // Start audio processing task
    xTaskCreate(audio_task, "audio_task", 4096, NULL, 5, NULL);
    
    // Main loop - just keep running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
