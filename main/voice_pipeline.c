#include "voice_pipeline.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "audio_player.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#ifdef GEMINI_ENABLED
#include "gemini_client.h"
#include "device_state.h"
#endif

#ifdef GEMINI_ENABLED
#define MAX_TRANSCRIPT_CHARS GEMINI_MAX_TRANSCRIPT_CHARS
#else
#define MAX_TRANSCRIPT_CHARS 512
#endif

// Forward declaration for microphone LED update
extern void update_mic_leds(float mic1_level, float mic2_level, float mic3_level);

#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_config.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"
#endif

// External LED control functions from main
extern void audio_playback_led_start(void);
extern void audio_playback_led_stop(void);
extern void lights_on(void);
extern void lights_off(void);

typedef enum {
    VOICE_PIPELINE_EVENT_WAKE = 1,
    VOICE_PIPELINE_EVENT_BUTTON = 2,
} voice_pipeline_event_type_t;

typedef struct {
    voice_pipeline_event_type_t type;
    int button_id;
} voice_pipeline_event_msg_t;

struct voice_pipeline {
    voice_pipeline_config_t cfg;
    QueueHandle_t events;
    TaskHandle_t task;
    int16_t *capture_buffer;
    size_t capture_samples;
    uint8_t *tts_buffer;
    size_t tts_buffer_bytes;
    voice_pipeline_wake_callback_t wake_callback;
    void *wake_callback_ctx;
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
    const esp_afe_sr_iface_t *afe_handle;
    esp_afe_sr_data_t *afe_data;
    int16_t *afe_feed_buffer;
    int afe_feed_chunksize;
    int afe_feed_channels;
    bool vad_active;  // VAD state from AFE
    bool vad_was_active;  // Previous VAD state (for edge detection)
    int16_t *afe_playback_ref;  // Playback reference for AEC
    size_t afe_playback_ref_size;
    TickType_t wakenet_cooldown_until;  // Cooldown after wake word detection
    const char *wakenet_model_name;   // WakeNet model name
    // Gemini batch STT audio accumulation
    int16_t *gemini_audio_buffer;  // Buffer for accumulating audio during speech
    size_t gemini_audio_samples;   // Current samples in buffer
    size_t gemini_audio_capacity;  // Maximum capacity
#endif
#ifdef GEMINI_ENABLED
    gemini_realtime_handle_t realtime_handle;
#endif
};

static const char *TAG = "voice_pipeline";
static const size_t VOICE_PIPELINE_TTS_BUFFER_BYTES = 96 * 1024;

static void set_led_state(voice_pipeline_handle_t handle, led_controller_state_t state)
{
    if (handle && handle->cfg.leds) {
        led_controller_set_state(handle->cfg.leds, state);
    }
}

static void publish_interaction(voice_pipeline_handle_t handle,
                                const char *transcript,
                                const intent_router_decision_t *decision,
                                esp_err_t status)
{
    if (handle->cfg.aws_bridge) {
        aws_iot_bridge_publish_interaction(handle->cfg.aws_bridge, transcript, decision, status);
    }
}

static esp_err_t capture_audio_block(voice_pipeline_handle_t handle, size_t total_samples)
{
    TickType_t lock_timeout = pdMS_TO_TICKS(handle->cfg.capture_ms > 0 ? handle->cfg.capture_ms + 500 : 2000);
    esp_err_t err = korvo_audio_acquire(handle->cfg.audio, lock_timeout);
    ESP_RETURN_ON_ERROR(err, TAG, "mic lock");
    size_t captured = 0;
    while (captured < total_samples) {
        size_t remaining = total_samples - captured;
        size_t chunk = remaining > 512 ? 512 : remaining;
        size_t read = 0;
        err = korvo_audio_capture_locked(handle->cfg.audio,
                                         handle->capture_buffer + captured,
                                         chunk,
                                         &read,
                                         pdMS_TO_TICKS(500));
        if (err != ESP_OK) {
            break;
        }
        if (read == 0) {
            err = ESP_ERR_TIMEOUT;
            break;
        }
        captured += read;
    }
    korvo_audio_release(handle->cfg.audio);
    ESP_RETURN_ON_ERROR(err, TAG, "mic read");
    ESP_LOGI(TAG, "Captured %zu samples", captured);
    return ESP_OK;
}

static void pick_response_text(const intent_router_decision_t *decision, char *out, size_t out_len)
{
    if (!decision || decision->action == INTENT_ROUTER_ACTION_NONE) {
        strlcpy(out, "Sorry, I didn't catch that.", out_len);
        return;
    }
    switch (decision->action) {
        case INTENT_ROUTER_ACTION_SPOTIFY_PLAY:
            if (decision->argument[0] != '\0') {
                snprintf(out, out_len, "Playing %s on Spotify.", decision->argument);
            } else {
                strlcpy(out, "Playing Spotify.", out_len);
            }
            break;
        case INTENT_ROUTER_ACTION_SPOTIFY_PAUSE:
            strlcpy(out, "Pausing Spotify.", out_len);
            break;
        case INTENT_ROUTER_ACTION_SPOTIFY_RESUME:
            strlcpy(out, "Resuming Spotify.", out_len);
            break;
        case INTENT_ROUTER_ACTION_SPOTIFY_VOLUME_DELTA:
            if (decision->volume_delta > 0) {
                strlcpy(out, "Turning it up.", out_len);
            } else {
                strlcpy(out, "Turning it down.", out_len);
            }
            break;
        case INTENT_ROUTER_ACTION_LIGHTS_OFF:
            strlcpy(out, "Lights off.", out_len);
            break;
        case INTENT_ROUTER_ACTION_LIGHTS_ON:
            strlcpy(out, "Lights on.", out_len);
            break;
        default:
            if (decision->action != INTENT_ROUTER_ACTION_NONE) {
                strlcpy(out, "Working on it.", out_len);
            }
            break;
    }
}

static void voice_pipeline_process_interaction(voice_pipeline_handle_t handle)
{
    if (!handle || !handle->cfg.audio) {
        return;
    }

    const size_t samples = handle->capture_samples;
    set_led_state(handle, LED_CONTROLLER_STATE_LISTENING);
    if (capture_audio_block(handle, samples) != ESP_OK) {
        ESP_LOGE(TAG, "Audio capture failed");
        publish_interaction(handle, "capture-failed", NULL, ESP_FAIL);
        set_led_state(handle, LED_CONTROLLER_STATE_ERROR);
        set_led_state(handle, LED_CONTROLLER_STATE_IDLE);
        return;
    }

    // Use Gemini STT exclusively
    char transcript_text[MAX_TRANSCRIPT_CHARS] = {0};
    esp_err_t stt_err = ESP_FAIL;
    
    set_led_state(handle, LED_CONTROLLER_STATE_THINKING);
    
    // Gemini STT pathway
    ESP_LOGI(TAG, "🎤 [Gemini STT] Starting transcription (%zu samples, %d Hz)", samples, handle->cfg.sample_rate_hz);
#ifdef GEMINI_ENABLED
    gemini_transcription_t transcript = {0};
    stt_err = gemini_transcribe_wav(handle->capture_buffer, samples, handle->cfg.sample_rate_hz, &transcript);
    if (stt_err == ESP_OK) {
        strncpy(transcript_text, transcript.text, sizeof(transcript_text) - 1);
        transcript_text[sizeof(transcript_text) - 1] = '\0';
        ESP_LOGI(TAG, "✅ [Gemini STT] Success: \"%s\"", transcript_text);
    } else {
        ESP_LOGE(TAG, "❌ [Gemini STT] Failed: %s", esp_err_to_name(stt_err));
    }
#else
    ESP_LOGE(TAG, "❌ [Gemini STT] Gemini not enabled (GEMINI_ENABLED not defined)");
    stt_err = ESP_ERR_NOT_SUPPORTED;
#endif
    
    if (handle->cfg.aws_bridge) {
        aws_iot_bridge_record_stt_result(handle->cfg.aws_bridge, stt_err);
    }
    if (stt_err != ESP_OK) {
        ESP_LOGE(TAG, "Transcription error (%s)", esp_err_to_name(stt_err));
        publish_interaction(handle, "stt-error", NULL, stt_err);
        set_led_state(handle, LED_CONTROLLER_STATE_ERROR);
        set_led_state(handle, LED_CONTROLLER_STATE_IDLE);
        return;
    }

    ESP_LOGI(TAG, "Heard: \"%s\"", transcript_text);
    
    // Filter out wake word "Naptick" from transcript (case-insensitive)
    char filtered_text[MAX_TRANSCRIPT_CHARS] = {0};
    strlcpy(filtered_text, transcript_text, sizeof(filtered_text));
    const char *wake_word = "naptick";
    size_t wake_word_len = strlen(wake_word);
    
    // Simple case-insensitive removal: convert to lowercase, find matches, remove from original
    char *text = filtered_text;
    while (*text) {
        // Check if current position matches wake word (case-insensitive)
        bool matches = true;
        for (size_t i = 0; i < wake_word_len; i++) {
            if (tolower((unsigned char)text[i]) != wake_word[i] || text[i] == '\0') {
                matches = false;
                break;
            }
        }
        // Check word boundaries
        bool is_word_start = (text == filtered_text || isspace((unsigned char)text[-1]));
        bool is_word_end = (text[wake_word_len] == '\0' || isspace((unsigned char)text[wake_word_len]));
        
        if (matches && is_word_start && is_word_end) {
            // Remove wake word and following spaces
            char *after = text + wake_word_len;
            while (*after && isspace((unsigned char)*after)) {
                after++;
            }
            memmove(text, after, strlen(after) + 1);
            // Trim leading spaces
            while (*text && isspace((unsigned char)*text)) {
                memmove(text, text + 1, strlen(text));
            }
        } else {
            text++;
        }
    }
    
    intent_router_decision_t decision = intent_router_route(handle->cfg.router, filtered_text);
    esp_err_t action_err = ESP_OK;
    switch (decision.action) {
        case INTENT_ROUTER_ACTION_SPOTIFY_PLAY:
            action_err = spotify_client_play(handle->cfg.spotify, decision.argument);
            break;
        case INTENT_ROUTER_ACTION_SPOTIFY_PAUSE:
            action_err = spotify_client_pause(handle->cfg.spotify);
            break;
        case INTENT_ROUTER_ACTION_SPOTIFY_RESUME:
            action_err = spotify_client_resume(handle->cfg.spotify);
            break;
        case INTENT_ROUTER_ACTION_SPOTIFY_VOLUME_DELTA:
            action_err = spotify_client_volume_delta(handle->cfg.spotify, decision.volume_delta);
            break;
        case INTENT_ROUTER_ACTION_LIGHTS_OFF:
            lights_off();
            action_err = ESP_OK;
            break;
        case INTENT_ROUTER_ACTION_LIGHTS_ON:
            lights_on();
            action_err = ESP_OK;
            break;
        case INTENT_ROUTER_ACTION_NONE:
        default:
            action_err = ESP_ERR_NOT_SUPPORTED;
            break;
    }
    if (handle->cfg.aws_bridge) {
        aws_iot_bridge_record_spotify_result(handle->cfg.aws_bridge, action_err);
    }

    char response_text[96] = {0};
    pick_response_text(&decision, response_text, sizeof(response_text));
    size_t wav_bytes = 0;
    set_led_state(handle, LED_CONTROLLER_STATE_SPEAKING);
    esp_err_t tts_err = gemini_tts_generate(response_text,
                                            handle->cfg.tts_voice,
                                            handle->tts_buffer,
                                            handle->tts_buffer_bytes,
                                            &wav_bytes);
    if (handle->cfg.aws_bridge) {
        aws_iot_bridge_record_tts_result(handle->cfg.aws_bridge, tts_err);
    }
    if (tts_err == ESP_OK) {
        ESP_LOGI(TAG, "Synthesized %d bytes of TTS audio", (int)wav_bytes);
        audio_playback_led_start();
        esp_err_t playback_err = audio_player_play_wav(handle->tts_buffer, wav_bytes);
        if (playback_err != ESP_OK) {
            ESP_LOGW(TAG, "Playback failed (%s)", esp_err_to_name(playback_err));
            audio_playback_led_stop();
        } else {
            // Estimate playback duration: WAV header + data, assume 16kHz mono
            // Rough estimate: ~wav_bytes / 32000 seconds (16-bit samples at 16kHz)
            int estimated_ms = (int)(wav_bytes / 32) + 500; // Add 500ms buffer
            vTaskDelay(pdMS_TO_TICKS(estimated_ms));
            audio_playback_led_stop();
        }
    } else {
        ESP_LOGE(TAG, "TTS failed (%s)", esp_err_to_name(tts_err));
    }

    publish_interaction(handle, transcript_text, &decision, action_err);
    set_led_state(handle, LED_CONTROLLER_STATE_IDLE);
}

static void voice_pipeline_process_button(voice_pipeline_handle_t handle, int button_id)
{
    if (!handle) {
        return;
    }
    if (handle->cfg.aws_bridge) {
        aws_iot_bridge_record_button(handle->cfg.aws_bridge, button_id);
    }
    set_led_state(handle, LED_CONTROLLER_STATE_THINKING);
    char prompt[96];
    snprintf(prompt, sizeof(prompt), "Someone pressed button %d. Acknowledge briefly and cheerfully.", button_id);
    char llm_text[MAX_TRANSCRIPT_CHARS] = {0};
    esp_err_t llm_err = gemini_generate_text_response(prompt, llm_text, sizeof(llm_text));
    if (llm_err != ESP_OK || llm_text[0] == '\0') {
        snprintf(llm_text, sizeof(llm_text), "Button %d pressed.", button_id);
    }
    set_led_state(handle, LED_CONTROLLER_STATE_SPEAKING);
    size_t wav_bytes = 0;
    esp_err_t tts_err = gemini_tts_generate(llm_text,
                                            handle->cfg.tts_voice,
                                            handle->tts_buffer,
                                            handle->tts_buffer_bytes,
                                            &wav_bytes);
    if (handle->cfg.aws_bridge) {
        aws_iot_bridge_record_tts_result(handle->cfg.aws_bridge, tts_err);
    }
    if (tts_err == ESP_OK) {
        ESP_LOGI(TAG, "Button %d prompt => \"%s\" (%d bytes audio)", button_id, llm_text, (int)wav_bytes);
    } else {
        ESP_LOGE(TAG, "Button speech synthesis failed (%s)", esp_err_to_name(tts_err));
    }
    if (tts_err == ESP_OK) {
        audio_playback_led_start();
        esp_err_t playback_err = audio_player_play_wav(handle->tts_buffer, wav_bytes);
        if (playback_err != ESP_OK) {
            ESP_LOGW(TAG, "Button response playback failed (%s)", esp_err_to_name(playback_err));
            audio_playback_led_stop();
        } else {
            int estimated_ms = (int)(wav_bytes / 32) + 500;
            vTaskDelay(pdMS_TO_TICKS(estimated_ms));
            audio_playback_led_stop();
        }
    }

    publish_interaction(handle, llm_text, NULL, tts_err);
    set_led_state(handle, LED_CONTROLLER_STATE_IDLE);
}

void voice_pipeline_set_wake_callback(voice_pipeline_handle_t handle, voice_pipeline_wake_callback_t callback, void *ctx)
{
    if (handle) {
        handle->wake_callback = callback;
        handle->wake_callback_ctx = ctx;
    }
}

// Task data structure for GPT+TTS processing
typedef struct {
    voice_pipeline_handle_t handle;
    char text[MAX_TRANSCRIPT_CHARS];
    bool active;
} gpt_tts_task_data_t;

// Task to handle GPT/Gemini chat + TTS asynchronously
// Also handles Gemini batch STT when transcription is not provided
static void gpt_tts_response_task(void *arg)
{
    gpt_tts_task_data_t *task_data = (gpt_tts_task_data_t *)arg;
    
    if (!task_data || !task_data->handle) {
        vTaskDelete(NULL);
        return;
    }
    
    voice_pipeline_handle_t handle = task_data->handle;
    const char *transcription = task_data->text;
    
    // If Gemini is enabled and no transcription provided, do batch STT first
    // Check if we need to process audio buffer (indicated by empty text)
    if (handle->cfg.use_gemini && (!transcription || strlen(transcription) == 0)) {
#ifdef GEMINI_ENABLED
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
        // Look for audio buffer in handle (set by VAD callback)
        // For now, we'll use a simpler approach: check if handle has audio buffer
        // Note: This requires the audio buffer to still be in handle when task runs
        // A better approach would be to pass audio buffer via task parameter, but for now
        // we'll rely on the handle's buffer being available
        if (handle->gemini_audio_buffer && handle->gemini_audio_samples > 0) {
            size_t audio_samples = handle->gemini_audio_samples;
            int16_t *audio_buffer = handle->gemini_audio_buffer;
            
            ESP_LOGI(TAG, "=== GEMINI BATCH STT-LLM-TTS PATHWAY START ===");
            float audio_duration = (float)audio_samples / handle->cfg.sample_rate_hz;
            ESP_LOGI(TAG, "📤 [Gemini] Sending audio to STT: %zu samples (%.2f sec) @ %d Hz", 
                     audio_samples, audio_duration, handle->cfg.sample_rate_hz);
            ESP_LOGI(TAG, "Step 1/3: STT - Processing batch STT (%zu samples, %d Hz)", 
                     audio_samples, handle->cfg.sample_rate_hz);
            gemini_transcription_t gemini_transcript = {0};
            esp_err_t stt_err = gemini_transcribe_wav(audio_buffer, 
                                                      audio_samples,
                                                      handle->cfg.sample_rate_hz,
                                                      &gemini_transcript);
            // Clear buffer after processing
            handle->gemini_audio_samples = 0;
            
            if (stt_err == ESP_OK && strlen(gemini_transcript.text) > 0) {
                strncpy(task_data->text, gemini_transcript.text, sizeof(task_data->text) - 1);
                task_data->text[sizeof(task_data->text) - 1] = '\0';
                transcription = task_data->text;
                ESP_LOGI(TAG, "✅ Step 1/3: STT SUCCESS - Transcript: \"%s\"", transcription);
            } else {
                ESP_LOGE(TAG, "❌ Step 1/3: STT FAILED - Error: %s", esp_err_to_name(stt_err));
                task_data->active = false;
                vTaskDelete(NULL);
                return;
            }
        } else {
            ESP_LOGW(TAG, "Gemini: No audio buffer available for STT");
            task_data->active = false;
            vTaskDelete(NULL);
            return;
        }
#else
        ESP_LOGE(TAG, "Gemini requires CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE");
        task_data->active = false;
        vTaskDelete(NULL);
        return;
#endif
#else
        ESP_LOGE(TAG, "Gemini requested but not enabled");
        task_data->active = false;
        vTaskDelete(NULL);
        return;
#endif
    }
    
    if (!transcription || strlen(transcription) == 0) {
        ESP_LOGW(TAG, "No transcription to process");
        task_data->active = false;
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "=== GEMINI STT-LLM-TTS PATHWAY CONTINUED ===");
    ESP_LOGI(TAG, "LLM+TTS task: Processing transcription \"%s\"", transcription);
    
    // Set LED to thinking state
    set_led_state(handle, LED_CONTROLLER_STATE_THINKING);
    
    // Send transcription to Gemini LLM (exclusively) with device state and function calling
    char llm_response[MAX_TRANSCRIPT_CHARS] = {0};
    esp_err_t llm_err = ESP_FAIL;
    
    ESP_LOGI(TAG, "💬 [Gemini Live] Step 2/3: LLM - Sending to Gemini: \"%s\"", transcription);
#ifdef GEMINI_ENABLED
    // Get current device state for context
    char *device_state = device_state_to_json();
    if (device_state) {
        ESP_LOGD(TAG, "💬 [Gemini Live] Device state: %s", device_state);
        llm_err = gemini_generate_text_response_with_tools(transcription, device_state, llm_response, sizeof(llm_response));
        free(device_state);
    } else {
        ESP_LOGW(TAG, "⚠️ [Gemini Live] Failed to get device state, using basic prompt");
        llm_err = gemini_generate_text_response(transcription, llm_response, sizeof(llm_response));
    }
    if (llm_err == ESP_OK && strlen(llm_response) > 0) {
        ESP_LOGI(TAG, "✅ [Gemini Live] Step 2/3: LLM SUCCESS - Response: \"%s\"", llm_response);
    } else {
        ESP_LOGE(TAG, "❌ [Gemini Live] Step 2/3: LLM FAILED - Error: %s", esp_err_to_name(llm_err));
    }
#else
    ESP_LOGE(TAG, "❌ GEMINI NOT ENABLED - GEMINI_ENABLED not defined");
    llm_err = ESP_ERR_NOT_SUPPORTED;
#endif
    
    if (llm_err == ESP_OK && strlen(llm_response) > 0) {
        // Generate TTS from LLM response using Gemini
        set_led_state(handle, LED_CONTROLLER_STATE_SPEAKING);
        size_t wav_bytes = 0;
        esp_err_t tts_err = ESP_FAIL;
        
        ESP_LOGI(TAG, "Step 3/3: TTS - Generating speech from LLM response (voice: %s)", handle->cfg.tts_voice);
#ifdef GEMINI_ENABLED
        tts_err = gemini_tts_generate(llm_response,
                                     handle->cfg.tts_voice,
                                     handle->tts_buffer,
                                     handle->tts_buffer_bytes,
                                     &wav_bytes);
        if (tts_err == ESP_OK && wav_bytes > 0) {
            ESP_LOGI(TAG, "✅ Step 3/3: TTS SUCCESS - Generated %d bytes", (int)wav_bytes);
        } else {
            ESP_LOGE(TAG, "❌ Step 3/3: TTS FAILED - Error: %s", esp_err_to_name(tts_err));
        }
#else
        ESP_LOGE(TAG, "❌ GEMINI NOT ENABLED - GEMINI_ENABLED not defined");
        tts_err = ESP_ERR_NOT_SUPPORTED;
#endif
        
        if (tts_err == ESP_OK && wav_bytes > 0) {
            // Play TTS audio
            ESP_LOGI(TAG, "Playing TTS audio (%d bytes)", (int)wav_bytes);
            audio_playback_led_start();
            esp_err_t playback_err = audio_player_play_wav(handle->tts_buffer, wav_bytes);
            if (playback_err != ESP_OK) {
                ESP_LOGE(TAG, "❌ TTS playback failed: %s", esp_err_to_name(playback_err));
                audio_playback_led_stop();
            } else {
                // Estimate playback duration: WAV header + data, assume 16kHz mono
                int estimated_ms = (int)(wav_bytes / 32) + 500; // Add 500ms buffer
                ESP_LOGI(TAG, "✅ TTS playback started (estimated %d ms)", estimated_ms);
                vTaskDelay(pdMS_TO_TICKS(estimated_ms));
                audio_playback_led_stop();
                ESP_LOGI(TAG, "=== GEMINI STT-LLM-TTS PATHWAY COMPLETE ===");
            }
        } else {
            ESP_LOGE(TAG, "=== GEMINI STT-LLM-TTS PATHWAY FAILED AT TTS ===");
        }
    } else {
        ESP_LOGE(TAG, "=== GEMINI STT-LLM-TTS PATHWAY FAILED AT LLM ===");
    }
    
    set_led_state(handle, LED_CONTROLLER_STATE_IDLE);
    task_data->active = false;
    vTaskDelete(NULL);
}

// Realtime transcription callback
static void realtime_transcript_cb(const char *text, bool is_final, void *ctx)
{
    voice_pipeline_handle_t handle = (voice_pipeline_handle_t)ctx;
    if (!handle || !text) {
        return;
    }
    
    // Output STT to console - clear and visible
    if (is_final) {
        // Final transcription - complete sentence
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Gemini STT (FINAL): \"%s\"", text);
        ESP_LOGI(TAG, "========================================");
    } else {
        // Partial transcription - incremental updates
        ESP_LOGI(TAG, "Gemini STT (partial): \"%s\"", text);
    }
    
    if (is_final && strlen(text) > 0) {
        // Send STT transcription to Gemini LLM and TTS the response
        // Do this in a separate task to avoid blocking callback
        static gpt_tts_task_data_t gpt_tts_task_data = {0};
        
        // Copy text and trigger async processing
        if (!gpt_tts_task_data.active && strlen(text) < sizeof(gpt_tts_task_data.text)) {
            gpt_tts_task_data.handle = handle;
            strncpy(gpt_tts_task_data.text, text, sizeof(gpt_tts_task_data.text) - 1);
            gpt_tts_task_data.text[sizeof(gpt_tts_task_data.text) - 1] = '\0';
            gpt_tts_task_data.active = true;
            
            // Spawn task to handle LLM chat + TTS (Gemini only)
            const char *task_name = "gemini_llm_tts_task";
            ESP_LOGI(TAG, "📤 [Realtime] Routing transcription to Gemini LLM: \"%s\"", text);
            xTaskCreate(gpt_tts_response_task, task_name, 8192, &gpt_tts_task_data, 5, NULL);
        } else {
            ESP_LOGW(TAG, "LLM+TTS task already active, skipping");
        }
        
        // Also process intent routing for local actions (quick, synchronous)
        intent_router_decision_t decision = intent_router_route(handle->cfg.router, text);
        
        // Handle intent actions
        esp_err_t action_err = ESP_OK;
        switch (decision.action) {
            case INTENT_ROUTER_ACTION_SPOTIFY_PLAY:
                action_err = spotify_client_play(handle->cfg.spotify, decision.argument);
                break;
            case INTENT_ROUTER_ACTION_SPOTIFY_PAUSE:
                action_err = spotify_client_pause(handle->cfg.spotify);
                break;
            case INTENT_ROUTER_ACTION_SPOTIFY_RESUME:
                action_err = spotify_client_resume(handle->cfg.spotify);
                break;
            case INTENT_ROUTER_ACTION_SPOTIFY_VOLUME_DELTA:
                action_err = spotify_client_volume_delta(handle->cfg.spotify, decision.volume_delta);
                break;
            case INTENT_ROUTER_ACTION_LIGHTS_OFF:
                lights_off();
                break;
            case INTENT_ROUTER_ACTION_LIGHTS_ON:
                lights_on();
                break;
            default:
                break;
        }
        
        publish_interaction(handle, text, &decision, action_err);
    }
}

static void realtime_error_cb(esp_err_t error, void *ctx)
{
    voice_pipeline_handle_t handle = (voice_pipeline_handle_t)ctx;
    ESP_LOGE(TAG, "Realtime API error: %s", esp_err_to_name(error));
    if (handle && handle->cfg.aws_bridge) {
        publish_interaction(handle, "realtime-error", NULL, error);
    }
}

// Continuous audio streaming task (skips wake word)
static void voice_pipeline_realtime_stream_task(void *arg)
{
    voice_pipeline_handle_t handle = (voice_pipeline_handle_t)arg;
    if (!handle || !handle->cfg.audio) {
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Starting continuous audio monitoring with Gemini");
    ESP_LOGI(TAG, "Pipeline: I2S -> AEC -> BSS/NS -> VAD -> Gemini Batch STT -> Gemini LLM -> Gemini TTS");
    
#ifdef GEMINI_ENABLED
    handle->realtime_handle = gemini_realtime_start(
        handle->cfg.sample_rate_hz,
        realtime_transcript_cb,
        realtime_error_cb,
        handle
    );
    
    if (!handle->realtime_handle) {
        ESP_LOGE(TAG, "Failed to start Gemini realtime placeholder");
        vTaskDelete(NULL);
        return;
    }
#endif
    
    int16_t *frame_buffer = malloc(512 * sizeof(int16_t));
    if (!frame_buffer) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer");
#ifdef GEMINI_ENABLED
        gemini_realtime_stop(handle->realtime_handle);
#endif
        vTaskDelete(NULL);
        return;
    }
    
    const size_t frame_samples = 512;
    const TickType_t frame_delay = pdMS_TO_TICKS(20); // ~20ms frames
    
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
    // AFE accumulator for processing pipeline: I2S -> AEC -> BSS/NS -> VAD
    static int16_t afe_accumulator[2048];
    static size_t afe_accumulator_count = 0;
    int16_t *afe_processed_buffer = NULL;
    handle->vad_active = false;
    handle->vad_was_active = false;
    
    if (handle->afe_handle && handle->afe_data && handle->afe_feed_buffer) {
        afe_processed_buffer = malloc(handle->afe_feed_chunksize * sizeof(int16_t));
        ESP_LOGI(TAG, "AFE pipeline enabled: AEC -> BSS/NS -> VAD");
        ESP_LOGI(TAG, "  feed_chunksize=%d, channels=%d", 
                 handle->afe_feed_chunksize, handle->afe_feed_channels);
        
        // Allocate playback reference buffer for AEC if needed
        // AEC needs reference signal from speaker output
        handle->afe_playback_ref = NULL;
        handle->afe_playback_ref_size = 0;
    }
#endif
    
    while (1) {
        // Capture audio frame
        size_t samples_read = 0;
        esp_err_t err = korvo_audio_capture(handle->cfg.audio, frame_buffer, frame_samples, 
                                           &samples_read, pdMS_TO_TICKS(100));
        
        if (err != ESP_OK || samples_read == 0) {
            vTaskDelay(frame_delay);
            continue;
        }
        
        // Compute mic levels and update LEDs (4, 8, 12) for sound reactivity
        // Extract left/right channels from stereo audio
        float mic1_level = 0.0f, mic2_level = 0.0f, mic3_level = 0.0f;
        if (samples_read >= 2) {
            // MIC1 = Left channel, MIC2 = Right channel, MIC3 = Combined
            uint64_t sum1 = 0, sum2 = 0, sum3 = 0;
            size_t count = 0;
            for (size_t i = 0; i < samples_read && i < frame_samples; i += 2) {
                if (i + 1 < samples_read) {
                    int32_t val1 = frame_buffer[i];     // Left channel (MIC1)
                    int32_t val2 = frame_buffer[i + 1]; // Right channel (MIC2)
                    int32_t val3 = (val1 + val2) / 2;   // Combined (MIC3)
                    
                    // Use absolute values
                    sum1 += val1 >= 0 ? val1 : -val1;
                    sum2 += val2 >= 0 ? val2 : -val2;
                    sum3 += val3 >= 0 ? val3 : -val3;
                    count++;
                }
            }
            mic1_level = count > 0 ? (float)sum1 / count : 0.0f;
            mic2_level = count > 0 ? (float)sum2 / count : 0.0f;
            mic3_level = count > 0 ? (float)sum3 / count : 0.0f;
        }
        
        // Update microphone LEDs (4, 8, 12) for sound reactivity
        update_mic_leds(mic1_level, mic2_level, mic3_level);
        
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
        // Process through AFE pipeline: I2S -> AEC -> BSS/NS -> VAD -> Gemini
        if (handle->afe_handle && handle->afe_data && handle->afe_feed_buffer && afe_processed_buffer) {
            // Step 1: Accumulate I2S samples for AFE feed
            size_t to_copy = samples_read;
            size_t accumulator_size = sizeof(afe_accumulator);
            size_t element_size = sizeof(afe_accumulator[0]);
            size_t max_elements = element_size > 0 ? accumulator_size / element_size : 0;
            if (max_elements > 0 && afe_accumulator_count + to_copy > max_elements) {
                to_copy = max_elements - afe_accumulator_count;
            }
            memcpy(&afe_accumulator[afe_accumulator_count], frame_buffer, to_copy * sizeof(int16_t));
            afe_accumulator_count += to_copy;
            
            // Step 2: Feed to AFE when we have enough samples
            // AFE processes: AEC -> BSS/NS -> VAD
            int samples_per_feed = handle->afe_feed_chunksize * handle->afe_feed_channels;
            // Prevent divide-by-zero: ensure samples_per_feed is valid
            if (samples_per_feed <= 0) {
                ESP_LOGE(TAG, "Invalid samples_per_feed: %d (chunksize=%d, channels=%d)", 
                         samples_per_feed, handle->afe_feed_chunksize, handle->afe_feed_channels);
                samples_per_feed = 512; // Default fallback
            }
            while (afe_accumulator_count >= samples_per_feed && samples_per_feed > 0) {
                memcpy(handle->afe_feed_buffer, afe_accumulator, samples_per_feed * sizeof(int16_t));
                
                // Feed to AFE pipeline: AEC -> BSS/NS -> VAD
                // Note: AEC may require playback reference signal for echo cancellation
                // For now, feed mic audio only (AEC will adapt or work in reference-free mode)
                int feed_result = handle->afe_handle->feed(handle->afe_data, handle->afe_feed_buffer);
                
                // If AEC requires playback reference, it would be fed here:
                // handle->afe_handle->feed_reference(handle->afe_data, playback_samples);
                if (feed_result >= 0) {
                    // Step 3: Fetch processed audio, VAD state, and WakeNet detection
                    afe_fetch_result_t *fetch_result = handle->afe_handle->fetch(handle->afe_data);
                    if (fetch_result && fetch_result->ret_value == ESP_OK) {
                        TickType_t now = xTaskGetTickCount();
                        
                        // Check WakeNet wake word detection (parallel processing)
                        // Dual pipeline:
                        //   Path 1: I2S -> AEC -> BSS/NS -> VAD -> Gemini streaming/batch processing
                        //   Path 2: I2S -> AEC -> BSS/NS -> WakeNet -> Local Control (triggered on wake word)
                        if (handle->wakenet_model_name && fetch_result->wakeup_state == WAKENET_DETECTED) {
                            // Check cooldown to prevent multiple detections
                            if (now >= handle->wakenet_cooldown_until) {
                                int word_index = fetch_result->wake_word_index;
                                int triggered_channel = fetch_result->trigger_channel_id;
                                const char *wake_word = handle->wakenet_model_name ? handle->wakenet_model_name : "wake_word";
                                
                                // Get human-readable wake word name
                                const char *wake_word_name = esp_wn_wakeword_from_name(wake_word);
                                const char *display_name = wake_word_name ? wake_word_name : wake_word;
                                
                                ESP_LOGI(TAG, "*** WAKE WORD DETECTED (local control): %s (index=%d, channel=%d) ***", 
                                         display_name, word_index, triggered_channel);
                                
                                // Trigger local control callback
                                if (handle->wake_callback) {
                                    handle->wake_callback(display_name, word_index, handle->wake_callback_ctx);
                                }
                                
                                // Set cooldown (2 seconds)
                                handle->wakenet_cooldown_until = now + pdMS_TO_TICKS(2000);
                            }
                        }
                        
                        // Check VAD state - only send to Gemini processing when speech is detected
                        // VAD runs after AEC -> BSS/NS in the AFE pipeline
                        bool vad_detected = false;
                        static int vad_check_count = 0;
                        
                        // Try to access VAD state from fetch_result
                        // ESP-SR AFE may expose VAD state in fetch_result
                        // Common fields: vad_state, vad_result, or VAD flag
                        #if 0
                        // Uncomment if your ESP-SR version exposes VAD state
                        if (fetch_result->vad_state == VAD_DETECTED || 
                            fetch_result->vad_state == VAD_SPEECH ||
                            fetch_result->vad_result == VAD_RESULT_SPEECH) {
                            vad_detected = true;
                        }
                        #endif
                        
                        // Fallback: Energy-based VAD on AFE-processed audio
                        // After AEC -> BSS/NS, audio should be cleaner
                        // Use moderate threshold - AFE processing improves SNR but we want to be sensitive
                        // VAD threshold - lower is more sensitive
                        // Set to very low value to ensure audio passes through for testing
                        static float vad_energy_threshold = 100.0f; // Very sensitive - will trigger on most audio
                        float energy = 0.0f;
                        
                        // Check energy on processed audio (after AEC/NS)
                        // Try to get processed audio from fetch_result if available
                        int16_t *audio_to_check = afe_accumulator; // Fallback to input
                        size_t samples_to_check = handle->afe_feed_chunksize;
                        
                        #if 0
                        // If fetch_result->data contains processed audio, use that
                        if (fetch_result->data && fetch_result->data_size > 0) {
                            audio_to_check = (int16_t *)fetch_result->data;
                            size_t size_check = sizeof(int16_t);
                            if (size_check > 0) {
                                samples_to_check = fetch_result->data_size / size_check;
                            } else {
                                samples_to_check = 0;
                            }
                        }
                        #endif
                        
                        // Ensure samples_to_check is valid before loop
                        if (samples_to_check > 0 && audio_to_check) {
                            for (size_t i = 0; i < samples_to_check; i++) {
                                int32_t val = audio_to_check[i];
                                energy += val >= 0 ? val : -val;
                            }
                            energy = energy / (float)samples_to_check;
                        } else {
                            energy = 0.0f;
                        }
                        vad_detected = (energy > vad_energy_threshold);
                        
                        // TEMPORARY: For testing, allow audio through if energy is above a very low threshold
                        // This ensures we can test the full pipeline even with quiet audio
                        static bool vad_bypass_for_testing = true;
                        if (vad_bypass_for_testing && energy > 50.0f) {
                            vad_detected = true;
                        }
                        
                        handle->vad_active = vad_detected;
                        
                        // Log energy levels periodically for debugging - very frequent for testing
                        static int energy_log_count = 0;
                        if (++energy_log_count % 20 == 0) {  // Every 20 AFE cycles = very frequent
                            ESP_LOGI(TAG, "VAD energy: %.1f (threshold=%.1f, detected=%d, bypass=%d)", 
                                    energy, vad_energy_threshold, vad_detected, vad_bypass_for_testing);
                        }
                        
                        // Step 4: Handle audio based on AI provider
                        if (handle->cfg.use_gemini && !handle->realtime_handle) {
                            // Pipeline: I2S -> AEC -> BSS/NS -> VAD -> Gemini Batch STT
                            // Gemini: Accumulate audio during speech, batch STT when speech ends
                            if (vad_detected) {
                                // Accumulate audio samples for batch STT
                                int16_t *audio_to_accumulate = afe_accumulator;
                                size_t samples_to_accumulate = handle->afe_feed_chunksize;
                                
                                // Allocate buffer if needed
                                if (!handle->gemini_audio_buffer) {
                                    // Validate sample_rate_hz to prevent overflow/divide-by-zero
                                    if (handle->cfg.sample_rate_hz > 0 && handle->cfg.sample_rate_hz <= 48000) {
                                        handle->gemini_audio_capacity = handle->cfg.sample_rate_hz * 5; // 5 seconds max
                                        handle->gemini_audio_buffer = malloc(handle->gemini_audio_capacity * sizeof(int16_t));
                                        handle->gemini_audio_samples = 0;
                                    } else {
                                        ESP_LOGE(TAG, "Invalid sample_rate_hz for audio buffer: %d", handle->cfg.sample_rate_hz);
                                        handle->gemini_audio_capacity = 0;
                                    }
                                }
                                
                                // Add samples to buffer if space available
                                if (handle->gemini_audio_buffer && 
                                    handle->gemini_audio_samples + samples_to_accumulate <= handle->gemini_audio_capacity) {
                                    memcpy(handle->gemini_audio_buffer + handle->gemini_audio_samples,
                                           audio_to_accumulate,
                                           samples_to_accumulate * sizeof(int16_t));
                                    handle->gemini_audio_samples += samples_to_accumulate;
                                    
                                    // Log audio accumulation periodically
                                    static int accumulate_log_count = 0;
                                    if (++accumulate_log_count % 50 == 0) {  // Every ~1 second at 50Hz
                                        float duration = (float)handle->gemini_audio_samples / handle->cfg.sample_rate_hz;
                                        ESP_LOGI(TAG, "🎙️ [Gemini] Accumulating audio: %zu samples (%.2f sec)", 
                                                handle->gemini_audio_samples, duration);
                                    }
                                } else if (!handle->gemini_audio_buffer) {
                                    ESP_LOGW(TAG, "⚠️ [Gemini] Audio buffer not allocated, cannot accumulate");
                                } else {
                                    ESP_LOGW(TAG, "⚠️ [Gemini] Audio buffer full (%zu/%zu), dropping samples", 
                                            handle->gemini_audio_samples, handle->gemini_audio_capacity);
                                }
                                
                                if (!handle->vad_was_active) {
                                    ESP_LOGI(TAG, "🎙️ [Gemini] Speech started, accumulating audio for batch STT");
                                }
                                handle->vad_was_active = true;
                            } else if (handle->vad_was_active && handle->gemini_audio_samples > 0) {
                                // Speech ended - process accumulated audio with batch STT
                                float duration = (float)handle->gemini_audio_samples / handle->cfg.sample_rate_hz;
                                ESP_LOGI(TAG, "🎙️ [Gemini] Speech ended (%zu samples, %.2f sec), sending to batch STT", 
                                        handle->gemini_audio_samples, duration);
                                
                                // Process in background task to avoid blocking
                                // Keep audio in handle buffer - task will process it
                                static gpt_tts_task_data_t gemini_stt_task_data = {0};
                                if (!gemini_stt_task_data.active) {
                                    gemini_stt_task_data.handle = handle;
                                    gemini_stt_task_data.text[0] = '\0'; // Signal to do STT first (empty = process audio buffer)
                                    gemini_stt_task_data.active = true;
                                    
                                    // Trigger STT processing task (will read from handle->gemini_audio_buffer)
                                    ESP_LOGI(TAG, "🎙️ [Gemini Live] Creating batch STT task...");
                                    xTaskCreate(gpt_tts_response_task, "gemini_stt_task", 8192, &gemini_stt_task_data, 5, NULL);
                                    // Note: Don't reset gemini_audio_samples here - task will reset it after processing
                                } else {
                                    ESP_LOGW(TAG, "⚠️ [Gemini Live] STT task already active, dropping audio");
                                    handle->gemini_audio_samples = 0; // Reset to prevent buffer overflow
                                }
                                handle->vad_was_active = false;
                            }
                        } else {
                            // Gemini realtime placeholder: record audio chunks when VAD detects speech
                            if (vad_detected && handle->realtime_handle) {
                                // Send AFE-processed audio (after AEC -> BSS/NS -> VAD) to Gemini placeholder
                                int16_t *audio_to_send = afe_accumulator;
                                size_t samples_to_send = handle->afe_feed_chunksize;
                                
                                // Try to get processed audio from fetch_result
                                // AFE-processed audio is cleaner (AEC removed echo, NS removed noise)
                                #if 0
                                // Uncomment if fetch_result->data contains processed audio
                                if (fetch_result->data && fetch_result->data_size > 0) {
                                    audio_to_send = (int16_t *)fetch_result->data;
                                    size_t size_check = sizeof(int16_t);
                                    if (size_check > 0) {
                                        samples_to_send = fetch_result->data_size / size_check;
                                    } else {
                                        samples_to_send = 0;
                                    }
                                }
                                #endif
                                
                                // Send processed audio to Gemini realtime placeholder
                                esp_err_t send_err = gemini_realtime_send_audio(handle->realtime_handle, audio_to_send, samples_to_send);
                                
                                static int vad_log_count = 0;
                                static int audio_send_count = 0;
                                audio_send_count++;
                                
                                if (++vad_log_count % 10 == 0) {  // Every ~200ms
                                    float duration = (float)samples_to_send / handle->cfg.sample_rate_hz;
                                    ESP_LOGI(TAG, "📤 [Gemini Live] Sending audio: %zu samples (%.3f sec), total chunks: %d", 
                                            samples_to_send, duration, audio_send_count);
                                    if (send_err != ESP_OK) {
                                        ESP_LOGW(TAG, "⚠️ [Gemini Live] Send error: %s", esp_err_to_name(send_err));
                                    }
                                }
                            } else if (!vad_detected) {
                                static int vad_silence_count = 0;
                                if (++vad_silence_count % 20 == 0) {  // More frequent for testing
                                    ESP_LOGI(TAG, "⚠️ VAD INACTIVE: skipping Gemini streaming (energy=%.1f < threshold=%.1f)", 
                                            energy, vad_energy_threshold);
                                }
                            }
                        }
                    }
                }
                
                // Shift remaining samples
                size_t remaining = afe_accumulator_count - samples_per_feed;
                if (remaining > 0) {
                    memmove(afe_accumulator, &afe_accumulator[samples_per_feed], 
                           remaining * sizeof(int16_t));
                }
                afe_accumulator_count = remaining;
            }
        } else
#endif
        {
            // No AFE - send raw audio directly to Gemini placeholder
            // Resample to 24kHz if needed (simplified: just send as-is for now)
            if (handle->realtime_handle) {
                gemini_realtime_send_audio(handle->realtime_handle, frame_buffer, samples_read);
            }
        }
        
        vTaskDelay(frame_delay);
    }
    
    if (frame_buffer) free(frame_buffer);
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
    if (afe_processed_buffer) free(afe_processed_buffer);
#endif
    vTaskDelete(NULL);
}

static void voice_pipeline_task(void *arg)
{
    voice_pipeline_handle_t handle = (voice_pipeline_handle_t)arg;
    
    // If realtime streaming is enabled, start continuous streaming task
    if (handle->cfg.use_realtime_streaming && handle->cfg.skip_wake_word) {
        voice_pipeline_realtime_stream_task(arg);
        return;
    }
    
    // Otherwise use traditional wake word + interaction flow
    voice_pipeline_event_msg_t evt;
    while (xQueueReceive(handle->events, &evt, portMAX_DELAY) == pdTRUE) {
        if (evt.type == VOICE_PIPELINE_EVENT_WAKE) {
            voice_pipeline_process_interaction(handle);
        } else if (evt.type == VOICE_PIPELINE_EVENT_BUTTON) {
            voice_pipeline_process_button(handle, evt.button_id);
        }
    }
    vTaskDelete(NULL);
}

voice_pipeline_handle_t voice_pipeline_create(const voice_pipeline_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg && cfg->audio && cfg->router && cfg->spotify, NULL, TAG, "invalid config");
    voice_pipeline_handle_t handle = calloc(1, sizeof(struct voice_pipeline));
    if (!handle) {
        return NULL;
    }
    handle->cfg = *cfg;
    // Validate sample_rate_hz to prevent divide-by-zero
    if (cfg->sample_rate_hz <= 0) {
        ESP_LOGE(TAG, "Invalid sample_rate_hz: %d", cfg->sample_rate_hz);
        free(handle);
        return NULL;
    }
    
    handle->capture_samples = (cfg->sample_rate_hz * cfg->capture_ms) / 1000;
    if (handle->capture_samples == 0) {
        handle->capture_samples = cfg->sample_rate_hz / 2;
    }
    handle->capture_buffer = malloc(handle->capture_samples * sizeof(int16_t));
    handle->tts_buffer_bytes = VOICE_PIPELINE_TTS_BUFFER_BYTES;
    handle->tts_buffer = malloc(handle->tts_buffer_bytes);
    handle->events = xQueueCreate(8, sizeof(voice_pipeline_event_msg_t));
    handle->realtime_handle = NULL;
    handle->wake_callback = NULL;
    handle->wake_callback_ctx = NULL;
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
    handle->wakenet_cooldown_until = 0;
    handle->wakenet_model_name = NULL;
    handle->gemini_audio_buffer = NULL;
    handle->gemini_audio_samples = 0;
    handle->gemini_audio_capacity = 0;
#endif
    
    if (!handle->capture_buffer || !handle->tts_buffer || !handle->events) {
        free(handle->capture_buffer);
        free(handle->tts_buffer);
        if (handle->events) {
            vQueueDelete(handle->events);
        }
        free(handle);
        return NULL;
    }
    
#if CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE
    // Initialize AFE for far-field processing
    // Pipeline: I2S -> AEC -> BSS/NS -> VAD -> Gemini streaming/batch
    if (cfg->use_realtime_streaming) {
        ESP_LOGI(TAG, "Initializing AFE pipeline: I2S -> AEC -> BSS/NS -> VAD -> Gemini processing");
        srmodel_list_t *models = esp_srmodel_init("model");
        if (models && models->num > 0) {
            // Filter for WakeNet models, prefer the configured model
            char *model_name = NULL;
            if (cfg->wakenet_model) {
                // Try to find the exact configured model first
                model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, (char*)cfg->wakenet_model);
            }
            // Fallback to any WakeNet model
            if (!model_name) {
                model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
            }
            if (model_name) {
                afe_config_t *afe_config = afe_config_init("MM", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
                if (afe_config) {
                    // Pipeline: I2S -> AEC -> BSS/NS -> VAD -> Gemini
                    // Configure AFE processing stages in order:
                    
                    // 1. AEC: Acoustic Echo Cancellation (first stage)
                    //    Removes echo from speaker playback
                    //    Note: May require playback reference signal
                    afe_config->aec_init = true;
                    
                    // 2. SE: Speech Enhancement/Beamforming (BSS-like source separation)
                    //    Separates speech from noise using multi-microphone array
                    afe_config->se_init = true;
                    
                    // 3. NS: Noise Suppression (after AEC)
                    //    Removes remaining background noise
                    afe_config->ns_init = true;
                    
                    // 4. VAD: Voice Activity Detection (after NS, gates Gemini processing)
                    //    Only send audio to Gemini when speech is detected
                    afe_config->vad_init = true;
                    afe_config->vad_mode = VAD_MODE_3; // Aggressive VAD mode
                    
                    // AGC: Automatic Gain Control (throughout pipeline)
                    afe_config->agc_init = true;
                    afe_config->agc_mode = AFE_AGC_MODE_WAKENET;
                    
                    // WakeNet: Enable in parallel for local control
                    // Gemini handles transcription, WakeNet handles local commands
                    if (cfg->enable_wakenet_local && cfg->wakenet_model) {
                        afe_config->wakenet_init = true;
                        afe_config->wakenet_model_name = cfg->wakenet_model;
                        afe_config->wakenet_mode = DET_MODE_2CH_90; // 2-channel detection for STEREO
                        ESP_LOGI(TAG, "WakeNet enabled in parallel: model=%s for local control", cfg->wakenet_model);
                    } else {
                        afe_config->wakenet_init = false;
                        ESP_LOGI(TAG, "WakeNet disabled - using VAD to gate Gemini processing only");
                    }
                    
                    if (afe_parse_input_format("MM", &afe_config->pcm_config)) {
                        afe_config->pcm_config.sample_rate = cfg->sample_rate_hz;
                        afe_config = afe_config_check(afe_config);
                        
                        if (afe_config) {
                            handle->afe_handle = esp_afe_handle_from_config(afe_config);
                            if (handle->afe_handle) {
                                handle->afe_data = handle->afe_handle->create_from_config(afe_config);
                                if (handle->afe_data) {
                                    handle->afe_feed_chunksize = handle->afe_handle->get_feed_chunksize(handle->afe_data);
                                    handle->afe_feed_channels = handle->afe_handle->get_feed_channel_num(handle->afe_data);
                                    
                                    // Validate AFE parameters to prevent divide-by-zero
                                    if (handle->afe_feed_chunksize <= 0) {
                                        ESP_LOGE(TAG, "Invalid AFE feed_chunksize: %d", handle->afe_feed_chunksize);
                                        handle->afe_feed_chunksize = 512; // Default fallback
                                    }
                                    if (handle->afe_feed_channels <= 0) {
                                        ESP_LOGE(TAG, "Invalid AFE feed_channels: %d", handle->afe_feed_channels);
                                        handle->afe_feed_channels = 1; // Default fallback
                                    }
                                    
                                    handle->afe_feed_buffer = malloc(handle->afe_feed_chunksize * sizeof(int16_t) * handle->afe_feed_channels);
                                    if (handle->afe_feed_buffer) {
                                        handle->wakenet_model_name = cfg->wakenet_model ? strdup(cfg->wakenet_model) : NULL;
                                        handle->wakenet_cooldown_until = 0;
                                        
                                        // Set WakeNet threshold if enabled
                                        if (afe_config->wakenet_init && cfg->wakenet_threshold > 0 && cfg->wakenet_threshold <= 100) {
                                            float det_threshold = 0.4f + (cfg->wakenet_threshold / 100.0f) * 0.5999f;
                                            handle->afe_handle->set_wakenet_threshold(handle->afe_data, 1, det_threshold);
                                            ESP_LOGI(TAG, "WakeNet threshold set to %.3f (config=%d)", det_threshold, cfg->wakenet_threshold);
                                        }
                                        
                                        ESP_LOGI(TAG, "AFE pipeline initialized: I2S -> AEC -> BSS/NS -> VAD");
                                        ESP_LOGI(TAG, "  AEC: %s, SE/BSS: %s, NS: %s, VAD: %s, WakeNet: %s",
                                                 afe_config->aec_init ? "enabled" : "disabled",
                                                 afe_config->se_init ? "enabled" : "disabled",
                                                 afe_config->ns_init ? "enabled" : "disabled",
                                                 afe_config->vad_init ? "enabled" : "disabled",
                                                 afe_config->wakenet_init ? cfg->wakenet_model : "disabled");
                                        
                                        // Print AFE pipeline to confirm processing order
                                        handle->afe_handle->print_pipeline(handle->afe_data);
                                    }
                                }
                            }
                        }
                        afe_config_free(afe_config);
                    }
                }
            }
            esp_srmodel_deinit(models);
        }
    }
#endif
    
    return handle;
}

esp_err_t voice_pipeline_start(voice_pipeline_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle && handle->events, ESP_ERR_INVALID_ARG, TAG, "invalid handle");
    if (handle->task) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t rc = xTaskCreatePinnedToCore(voice_pipeline_task,
                                            "voice_pipeline",
                                            8192,
                                            handle,
                                            4,
                                            &handle->task,
                                            tskNO_AFFINITY);
    return rc == pdPASS ? ESP_OK : ESP_FAIL;
}

void voice_pipeline_handle_wake(voice_pipeline_handle_t handle)
{
    if (!handle || !handle->events) {
        return;
    }
    voice_pipeline_event_msg_t evt = {
        .type = VOICE_PIPELINE_EVENT_WAKE,
        .button_id = 0,
    };
    if (xQueueSend(handle->events, &evt, 0) != pdPASS) {
        ESP_LOGW(TAG, "Wake event queue full");
    }
}

void voice_pipeline_handle_button(voice_pipeline_handle_t handle, int button_id)
{
    if (!handle || !handle->events) {
        return;
    }
    voice_pipeline_event_msg_t evt = {
        .type = VOICE_PIPELINE_EVENT_BUTTON,
        .button_id = button_id,
    };
    if (xQueueSend(handle->events, &evt, 0) != pdPASS) {
        ESP_LOGW(TAG, "Button event queue full");
    }
}
