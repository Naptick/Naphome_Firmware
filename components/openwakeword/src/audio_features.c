/**
 * @file audio_features.c
 * @brief Audio feature extraction for OpenWakeWord
 * 
 * Implements melspectrogram extraction for wake word detection.
 * Based on OpenWakeWord's AudioFeatures class.
 */

#include "audio_features.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_err.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ESP-DSP for FFT (optional, fallback to simple implementation if not available)
#ifdef CONFIG_DSP_ENABLED
#include "dsps_fft2r.h"
#include "dsp_err.h"
#define USE_ESP_DSP_FFT 1
#else
#define USE_ESP_DSP_FFT 0
#endif

static const char *TAG = "audio_features";

// Melspectrogram parameters (matching OpenWakeWord defaults)
#define MELSPEC_N_MELS 40
#define MELSPEC_N_FFT 512
#define MELSPEC_HOP_LENGTH 160
#define MELSPEC_FMIN 0
#define MELSPEC_FMAX 8000
#define MELSPEC_SAMPLE_RATE 16000

// Mel scale conversion
#define HZ_TO_MEL(hz) (2595.0 * log10(1.0 + (hz) / 700.0))
#define MEL_TO_HZ(mel) (700.0 * (pow(10, (mel) / 2595.0) - 1.0))

struct audio_features {
    uint32_t sample_rate;
    float *melspectrogram_buffer;
    size_t melspectrogram_size;
    
    // FFT buffers
    float *fft_buffer;           // Complex FFT input/output [Re, Im, Re, Im, ...]
    float *magnitude_buffer;      // FFT magnitude spectrum
    float *mel_filter_bank;       // Pre-computed mel filter bank
    bool fft_initialized;
    
    // Window function (Hanning)
    float *window;
};

// Generate Hanning window
static void generate_hanning_window(float *window, int size)
{
    for (int i = 0; i < size; i++) {
        window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (size - 1)));
    }
}

// Generate mel filter bank
static esp_err_t generate_mel_filter_bank(float *filter_bank, uint32_t sample_rate, int n_fft, int n_mels)
{
    float mel_max = HZ_TO_MEL(sample_rate / 2.0f);
    float mel_min = HZ_TO_MEL(MELSPEC_FMIN);
    float mel_spacing = (mel_max - mel_min) / (n_mels + 1);
    
    // Generate mel center frequencies
    float *mel_centers = malloc((n_mels + 2) * sizeof(float));
    if (!mel_centers) {
        return ESP_ERR_NO_MEM;
    }
    
    for (int i = 0; i < n_mels + 2; i++) {
        mel_centers[i] = MEL_TO_HZ(mel_min + i * mel_spacing);
    }
    
    // Convert to FFT bin frequencies
    float *fft_freqs = malloc((n_fft / 2 + 1) * sizeof(float));
    if (!fft_freqs) {
        free(mel_centers);
        return ESP_ERR_NO_MEM;
    }
    
    for (int i = 0; i <= n_fft / 2; i++) {
        fft_freqs[i] = (float)i * sample_rate / n_fft;
    }
    
    // Generate triangular filters
    int filter_size = n_mels * (n_fft / 2 + 1);
    memset(filter_bank, 0, filter_size * sizeof(float));
    
    for (int i = 0; i < n_mels; i++) {
        float left = mel_centers[i];
        float center = mel_centers[i + 1];
        float right = mel_centers[i + 2];
        
        for (int j = 0; j <= n_fft / 2; j++) {
            float freq = fft_freqs[j];
            float weight = 0.0f;
            
            if (freq >= left && freq <= center) {
                weight = (freq - left) / (center - left);
            } else if (freq > center && freq <= right) {
                weight = (right - freq) / (right - center);
            }
            
            filter_bank[i * (n_fft / 2 + 1) + j] = weight;
        }
    }
    
    free(mel_centers);
    free(fft_freqs);
    return ESP_OK;
}

// Simple FFT implementation (fallback if ESP-DSP not available)
// This is a basic DFT implementation - very slow, use ESP-DSP for production
static void simple_fft(float *data, int n, int is_inverse)
{
    ESP_LOGW(TAG, "Using simple FFT fallback - consider enabling ESP-DSP for better performance");
    
    // Allocate temporary buffers
    float *real = malloc(n * sizeof(float));
    float *imag = malloc(n * sizeof(float));
    if (!real || !imag) {
        ESP_LOGE(TAG, "Failed to allocate FFT buffers");
        return;
    }
    
    // Extract real and imaginary parts
    for (int i = 0; i < n; i++) {
        real[i] = data[i * 2];
        imag[i] = data[i * 2 + 1];
    }
    
    // Simple DFT (very slow, O(n^2))
    float sign = is_inverse ? 1.0f : -1.0f;
    for (int k = 0; k < n; k++) {
        float sum_real = 0.0f;
        float sum_imag = 0.0f;
        
        for (int j = 0; j < n; j++) {
            float angle = sign * 2.0f * M_PI * k * j / n;
            float cos_val = cosf(angle);
            float sin_val = sinf(angle);
            sum_real += real[j] * cos_val - imag[j] * sin_val;
            sum_imag += real[j] * sin_val + imag[j] * cos_val;
        }
        
        data[k * 2] = sum_real;
        data[k * 2 + 1] = sum_imag;
    }
    
    // Normalize for inverse FFT
    if (is_inverse) {
        for (int i = 0; i < n; i++) {
            data[i * 2] /= n;
            data[i * 2 + 1] /= n;
        }
    }
    
    free(real);
    free(imag);
}

audio_features_t *audio_features_init(uint32_t sample_rate)
{
    audio_features_t *features = calloc(1, sizeof(audio_features_t));
    if (!features) {
        return NULL;
    }
    
    features->sample_rate = sample_rate;
    
    // Allocate buffers
    int n_fft_bins = MELSPEC_N_FFT / 2 + 1;
    features->melspectrogram_size = MELSPEC_N_MELS;
    features->melspectrogram_buffer = malloc(features->melspectrogram_size * sizeof(float));
    
    // FFT buffer: complex numbers [Re, Im, Re, Im, ...]
    features->fft_buffer = malloc(MELSPEC_N_FFT * 2 * sizeof(float));
    features->magnitude_buffer = malloc(n_fft_bins * sizeof(float));
    features->window = malloc(MELSPEC_N_FFT * sizeof(float));
    
    // Mel filter bank: n_mels x n_fft_bins
    int filter_bank_size = MELSPEC_N_MELS * n_fft_bins;
    features->mel_filter_bank = malloc(filter_bank_size * sizeof(float));
    
    if (!features->melspectrogram_buffer || !features->fft_buffer || 
        !features->magnitude_buffer || !features->window || !features->mel_filter_bank) {
        audio_features_deinit(features);
        return NULL;
    }
    
    // Initialize window function
    generate_hanning_window(features->window, MELSPEC_N_FFT);
    
    // Initialize mel filter bank
    esp_err_t err = generate_mel_filter_bank(features->mel_filter_bank, sample_rate, 
                                              MELSPEC_N_FFT, MELSPEC_N_MELS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate mel filter bank");
        audio_features_deinit(features);
        return NULL;
    }
    
#if USE_ESP_DSP_FFT
    // Initialize ESP-DSP FFT
    err = dsps_fft2r_init_fc32(NULL, MELSPEC_N_FFT);
    if (err == ESP_OK) {
        features->fft_initialized = true;
        ESP_LOGI(TAG, "Using ESP-DSP FFT");
    } else {
        ESP_LOGW(TAG, "ESP-DSP FFT init failed, using fallback");
        features->fft_initialized = false;
    }
#else
    features->fft_initialized = false;
    ESP_LOGI(TAG, "ESP-DSP not enabled, using simple FFT fallback");
#endif
    
    ESP_LOGI(TAG, "Initialized audio features extractor");
    ESP_LOGI(TAG, "  Sample rate: %u Hz", (unsigned int)sample_rate);
    ESP_LOGI(TAG, "  N_FFT: %d, N_MELS: %d", MELSPEC_N_FFT, MELSPEC_N_MELS);
    ESP_LOGI(TAG, "  Melspectrogram size: %zu", features->melspectrogram_size);
    
    return features;
}

esp_err_t audio_features_extract_melspectrogram(audio_features_t *features,
                                                  const int16_t *audio_samples,
                                                  size_t sample_count,
                                                  float *melspectrogram_out,
                                                  size_t *melspectrogram_size_out)
{
    if (!features || !audio_samples || !melspectrogram_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Limit sample count to N_FFT
    size_t samples_to_process = (sample_count > MELSPEC_N_FFT) ? MELSPEC_N_FFT : sample_count;
    
    // Step 1: Convert int16 to float and apply window
    for (size_t i = 0; i < MELSPEC_N_FFT; i++) {
        float sample = (i < samples_to_process) ? 
                       ((float)audio_samples[i] / 32768.0f) : 0.0f;
        // Apply Hanning window
        sample *= features->window[i];
        // Store as complex: real part = sample, imaginary part = 0
        features->fft_buffer[i * 2] = sample;     // Real
        features->fft_buffer[i * 2 + 1] = 0.0f;  // Imaginary
    }
    
    // Step 2: Compute FFT
#if USE_ESP_DSP_FFT
    if (features->fft_initialized) {
        // Use ESP-DSP optimized FFT
        #ifdef CONFIG_IDF_TARGET_ESP32S3
        esp_err_t err = dsps_fft2r_fc32_aes3(features->fft_buffer, MELSPEC_N_FFT);
        #else
        esp_err_t err = dsps_fft2r_fc32_ansi(features->fft_buffer, MELSPEC_N_FFT);
        #endif
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "FFT computation failed");
            return err;
        }
    } else {
        simple_fft(features->fft_buffer, MELSPEC_N_FFT, 0);
    }
#else
    simple_fft(features->fft_buffer, MELSPEC_N_FFT, 0);
#endif
    
    // Step 3: Compute magnitude spectrum
    int n_bins = MELSPEC_N_FFT / 2 + 1;
    for (int i = 0; i < n_bins; i++) {
        float real = features->fft_buffer[i * 2];
        float imag = features->fft_buffer[i * 2 + 1];
        features->magnitude_buffer[i] = sqrtf(real * real + imag * imag);
    }
    
    // Step 4: Apply mel filter bank
    memset(melspectrogram_out, 0, MELSPEC_N_MELS * sizeof(float));
    for (int mel = 0; mel < MELSPEC_N_MELS; mel++) {
        float sum = 0.0f;
        for (int bin = 0; bin < n_bins; bin++) {
            float filter_weight = features->mel_filter_bank[mel * n_bins + bin];
            sum += features->magnitude_buffer[bin] * filter_weight;
        }
        melspectrogram_out[mel] = sum;
    }
    
    // Step 5: Take logarithm (log10 with small epsilon to avoid log(0))
    const float epsilon = 1e-10f;
    for (int i = 0; i < MELSPEC_N_MELS; i++) {
        melspectrogram_out[i] = log10f(melspectrogram_out[i] + epsilon);
    }
    
    if (melspectrogram_size_out) {
        *melspectrogram_size_out = MELSPEC_N_MELS;
    }
    
    return ESP_OK;
}

void audio_features_deinit(audio_features_t *features)
{
    if (!features) {
        return;
    }
    
#if USE_ESP_DSP_FFT
    if (features->fft_initialized) {
        dsps_fft2r_deinit_fc32();
    }
#endif
    
    if (features->melspectrogram_buffer) free(features->melspectrogram_buffer);
    if (features->fft_buffer) free(features->fft_buffer);
    if (features->magnitude_buffer) free(features->magnitude_buffer);
    if (features->window) free(features->window);
    if (features->mel_filter_bank) free(features->mel_filter_bank);
    
    free(features);
}