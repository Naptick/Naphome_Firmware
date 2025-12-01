#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio_player.h"
#include "kva_config_defaults.h"
#include "aws_iot_bridge.h"
#include "button_service.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "intent_router.h"
#include "korvo_audio.h"
#include "led_controller.h"
#include "somnus_mqtt.h"
#include "aws_iot_service.h"
#include "nvs_flash.h"
#include "spotify_client.h"
#include "spotify_player.h"
#include "serial_command_parser.h"
#include "voice_pipeline.h"
#include "wake_word_service.h"
#include "sensor_integration.h"
#include "matter_bridge.h"
#include "sensor_manager.h"
#include "driver/i2c.h"
// Compatibility: use old I2C API for ESP-IDF v4.4
#define i2c_master_bus_handle_t i2c_port_t
#define i2c_master_dev_handle_t i2c_cmd_handle_t
#include "driver/gpio.h"
#include "esp_system.h"
#include "version_info.h"
#include "device_state.h"

static const char *TAG = "naphome_assistant";

// Shutdown handler for crash reporting
static void shutdown_handler(void)
{
    ESP_LOGE(TAG, "*** SYSTEM SHUTDOWN ***");
    ESP_LOGE(TAG, "Free heap: %u bytes", (unsigned int)esp_get_free_heap_size());
    ESP_LOGE(TAG, "Minimum free heap: %u bytes", (unsigned int)esp_get_minimum_free_heap_size());
}

// Forward declaration for microphone LED update function
void update_mic_leds(float mic1_level, float mic2_level, float mic3_level);

/**
 * @brief Scan I2C bus for devices
 * @param i2c_port I2C port number (I2C_NUM_0 or I2C_NUM_1)
 * @param sda_gpio SDA GPIO pin
 * @param scl_gpio SCL GPIO pin
 * @param bus_name Name for logging (e.g., "Sensor Bus" or "Audio Bus")
 */
static void scan_i2c_bus(i2c_port_t i2c_port, int sda_gpio, int scl_gpio, const char *bus_name)
{
    ESP_LOGI(TAG, "=== Scanning %s (I2C_NUM_%d, SDA=GPIO%d, SCL=GPIO%d) ===", 
             bus_name, i2c_port, sda_gpio, scl_gpio);
    
    // Use ESP-IDF v4.4 I2C API for scanning
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = (gpio_num_t)sda_gpio,
        .scl_io_num = (gpio_num_t)scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    
    esp_err_t ret = i2c_param_config(i2c_port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  Failed to configure I2C: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = i2c_driver_install(i2c_port, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  Failed to install I2C driver: %s", esp_err_to_name(ret));
        return;
    }
    
    // Scan all possible 7-bit addresses (0x08 to 0x77)
    int found_count = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  ✓ Found device at address 0x%02X", addr);
            found_count++;
        }
    }
    
    // Clean up
    i2c_driver_delete(i2c_port);
    
    if (found_count == 0) {
        ESP_LOGW(TAG, "  No devices found on %s", bus_name);
    } else {
        ESP_LOGI(TAG, "  Total: %d device(s) found on %s", found_count, bus_name);
    }
    ESP_LOGI(TAG, "=== End scan of %s ===\n", bus_name);
}

#define WIFI_LED_INDEX 0
#define SPOTIFY_LED_INDEX 1
#define AWS_LED_INDEX 2
#define WAKE_WORD_LED_INDEX 3
#define MUTE_LED_INDEX 4
#define AUDIO_PLAYBACK_LED_INDEX 5
#define MIC1_LED_INDEX 6
#define MIC2_LED_INDEX 7
#define MIC3_LED_INDEX 8
#define STATUS_LED_COUNT 9

static const char* led_names[] = {
    "WIFI",
    "SPOTIFY",
    "AWS",
    "WAKE_WORD",
    "MUTE",
    "AUDIO_PLAYBACK",
    "MIC1",
    "MIC2",
    "MIC3"
};

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static led_controller_t *s_led_controller_handle = NULL;
static esp_timer_handle_t s_wifi_led_timer;
static esp_timer_handle_t s_wake_word_led_timer;
static esp_timer_handle_t s_audio_playback_timer;
static esp_timer_handle_t s_trippy_fade_timer;
static bool s_wifi_led_on;
static bool s_muted = false;
static bool s_audio_playing = false;
static uint8_t s_audio_playback_brightness = 0;
static bool s_lights_enabled = true; // Track if lights are on or off
static bool s_aws_connected = false; // Track AWS IoT connection state

static void set_status_led(uint8_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    // Status LEDs are now animated by trippy fade, so this function is a no-op
    // But we keep it for compatibility in case any code still calls it
    (void)index;
    (void)red;
    (void)green;
    (void)blue;
}

// Helper function to scale audio level to LED brightness
// More sensitive scaling for better visual feedback - LEDs should be very visible
static uint8_t scale_audio_level_to_brightness(float level)
{
    // Handle very high levels (like 30935) - these might be initial values or DC offset
    // Normalize to a reasonable range
    if (level > 30000.0f) {
        // Very high values - likely DC offset or initialization, scale down
        level = level / 120.0f; // Bring 30935 down to ~258
    }
    
    // Lower noise floor and threshold for better visibility
    if (level < 100.0f) return 0; // Below noise floor
    if (level > 5000.0f) return 255; // Max brightness
    // Linear scaling between noise floor and max
    float normalized = (level - 100.0f) / (5000.0f - 100.0f);
    if (normalized > 1.0f) normalized = 1.0f;
    if (normalized < 0.0f) normalized = 0.0f;
    // Use square root for more linear visual response
    normalized = sqrt(normalized);
    // Ensure minimum brightness when audio is present for visibility
    uint8_t brightness = (uint8_t)(normalized * 255.0f);
    return brightness > 0 ? (brightness < 30 ? 30 : brightness) : 0; // Minimum 30 for visibility
}

// Update microphone LEDs (4, 8, 12) based on audio levels
// LED 4 = MIC1 (left channel), LED 8 = MIC2 (right channel), LED 12 = MIC3 (combined)
// Note: All LEDs are now animated by trippy fade, so this function is disabled
void update_mic_leds(float mic1_level, float mic2_level, float mic3_level)
{
    // All LEDs are animated by trippy fade, so mic LEDs are part of the animation
    (void)mic1_level;
    (void)mic2_level;
    (void)mic3_level;
}

void lights_off(void)
{
    if (!s_led_controller_handle) {
        return;
    }
    s_lights_enabled = false;
    // Stop trippy fade animation
    led_controller_stop_trippy_fade(s_led_controller_handle);
    if (s_trippy_fade_timer) {
        esp_timer_stop(s_trippy_fade_timer);
    }
    // Turn off ALL LEDs
    for (uint8_t i = 0; i < s_led_controller_handle->led_count; ++i) {
        led_controller_set_pixel_color(s_led_controller_handle, i, 0, 0, 0);
    }
    ESP_LOGI(TAG, "Lights turned off");
}

void lights_on(void)
{
    if (!s_led_controller_handle) {
        return;
    }
    s_lights_enabled = true;
    // Restart trippy fade animation - it will animate all LEDs
    led_controller_start_trippy_fade(s_led_controller_handle);
    if (s_trippy_fade_timer) {
        esp_timer_start_periodic(s_trippy_fade_timer, 50 * 1000);
    }
    ESP_LOGI(TAG, "Lights turned on - trippy fade animating all LEDs");
}

static void wifi_led_timer_cb(void *arg)
{
    (void)arg;
    if (!s_led_controller_handle) {
        return;
    }
    s_wifi_led_on = !s_wifi_led_on;
    if (s_wifi_led_on) {
        set_status_led(WIFI_LED_INDEX, 0, 80, 80);
    } else {
        set_status_led(WIFI_LED_INDEX, 0, 0, 0);
    }
}

static void trippy_fade_timer_cb(void *arg)
{
    (void)arg;
    if (s_led_controller_handle && s_lights_enabled) {
        led_controller_update_trippy_fade(s_led_controller_handle);
    }
}

typedef enum {
    WIFI_LED_OFF = 0,
    WIFI_LED_CONNECTING,
    WIFI_LED_CONNECTED,
    WIFI_LED_FAILED,
} wifi_led_mode_t;

static void wifi_led_set_mode(wifi_led_mode_t mode)
{
    if (!s_led_controller_handle) {
        return;
    }
    switch (mode) {
        case WIFI_LED_CONNECTING:
            if (!s_wifi_led_timer) {
                const esp_timer_create_args_t args = {
                    .callback = wifi_led_timer_cb,
                    .name = "wifi_led",
                };
                esp_err_t err = esp_timer_create(&args, &s_wifi_led_timer);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to create Wi-Fi LED timer (%s)", esp_err_to_name(err));
                    s_wifi_led_timer = NULL;
                }
            }
            s_wifi_led_on = true;
            set_status_led(WIFI_LED_INDEX, 0, 80, 80);
            if (s_wifi_led_timer) {
                esp_timer_start_periodic(s_wifi_led_timer, 350 * 1000);
            }
            break;
        case WIFI_LED_CONNECTED:
            if (s_wifi_led_timer) {
                esp_timer_stop(s_wifi_led_timer);
            }
            set_status_led(WIFI_LED_INDEX, 0, 80, 80);
            break;
        case WIFI_LED_FAILED:
            if (s_wifi_led_timer) {
                esp_timer_stop(s_wifi_led_timer);
            }
            set_status_led(WIFI_LED_INDEX, 80, 0, 0);
            break;
        case WIFI_LED_OFF:
        default:
            if (s_wifi_led_timer) {
                esp_timer_stop(s_wifi_led_timer);
            }
            set_status_led(WIFI_LED_INDEX, 0, 0, 0);
            break;
    }
}

static void wake_word_led_timer_cb(void *arg)
{
    (void)arg;
    if (!s_led_controller_handle) {
        return;
    }
    static bool wake_led_on = false;
    wake_led_on = !wake_led_on;
    if (wake_led_on) {
        set_status_led(WAKE_WORD_LED_INDEX, 255, 165, 0); // Orange
    } else {
        set_status_led(WAKE_WORD_LED_INDEX, 0, 0, 0);
    }
}

static void wake_word_led_activate(void)
{
    if (!s_led_controller_handle) {
        return;
    }
    if (!s_wake_word_led_timer) {
        const esp_timer_create_args_t args = {
            .callback = wake_word_led_timer_cb,
            .name = "wake_word_led",
        };
        esp_err_t err = esp_timer_create(&args, &s_wake_word_led_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create wake word LED timer (%s)", esp_err_to_name(err));
            s_wake_word_led_timer = NULL;
            return;
        }
    }
    esp_timer_start_periodic(s_wake_word_led_timer, 100 * 1000); // Fast blink: 100ms
}

static void wake_word_led_deactivate(void)
{
    if (s_wake_word_led_timer) {
        esp_timer_stop(s_wake_word_led_timer);
    }
    set_status_led(WAKE_WORD_LED_INDEX, 0, 0, 0);
}

static void update_mute_led(void)
{
    if (!s_led_controller_handle) {
        return;
    }
    if (s_muted) {
        set_status_led(MUTE_LED_INDEX, 80, 0, 0); // Red when muted
    } else {
        set_status_led(MUTE_LED_INDEX, 0, 0, 0); // Off when not muted
    }
}

static void audio_playback_timer_cb(void *arg)
{
    (void)arg;
    if (!s_led_controller_handle || !s_audio_playing) {
        return;
    }
    // Pulse animation: fade in and out
    static bool fading_in = true;
    if (fading_in) {
        s_audio_playback_brightness += 8;
        if (s_audio_playback_brightness >= 128) {
            s_audio_playback_brightness = 128;
            fading_in = false;
        }
    } else {
        s_audio_playback_brightness -= 8;
        if (s_audio_playback_brightness == 0) {
            fading_in = true;
        }
    }
    // Blue pulsing LED for audio playback
    uint8_t blue = s_audio_playback_brightness;
    set_status_led(AUDIO_PLAYBACK_LED_INDEX, 0, 0, blue);
}

void audio_playback_led_start(void)
{
    if (!s_led_controller_handle) {
        return;
    }
    s_audio_playing = true;
    s_audio_playback_brightness = 0;
    if (!s_audio_playback_timer) {
        const esp_timer_create_args_t args = {
            .callback = audio_playback_timer_cb,
            .name = "audio_playback_led",
        };
        esp_err_t err = esp_timer_create(&args, &s_audio_playback_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create audio playback LED timer (%s)", esp_err_to_name(err));
            s_audio_playback_timer = NULL;
            return;
        }
    }
    esp_timer_start_periodic(s_audio_playback_timer, 50 * 1000); // 50ms for smooth animation
}

void audio_playback_led_stop(void)
{
    s_audio_playing = false;
    if (s_audio_playback_timer) {
        esp_timer_stop(s_audio_playback_timer);
    }
    set_status_led(AUDIO_PLAYBACK_LED_INDEX, 0, 0, 0);
}

void aws_led_set_connected(bool connected)
{
    s_aws_connected = connected;
    // Update device state context for Gemini function calling
    if (s_led_controller_handle) {
        device_state_set_context(s_led_controller_handle, s_lights_enabled, s_aws_connected, s_muted, s_audio_playing);
    }
    if (!s_led_controller_handle || !s_lights_enabled) {
        return;
    }
    if (connected) {
        set_status_led(AWS_LED_INDEX, 0, 120, 0); // Green when connected
        ESP_LOGI(TAG, "AWS LED: Connected (green)");
    } else {
        set_status_led(AWS_LED_INDEX, 120, 60, 0); // Amber when disconnected/reconnecting
        ESP_LOGI(TAG, "AWS LED: Disconnected (amber)");
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retrying Wi-Fi connection...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_connect(void)
{
    ESP_RETURN_ON_FALSE(strlen(CONFIG_KVA_WIFI_SSID) > 0, ESP_ERR_INVALID_STATE, TAG, "Set Wi-Fi SSID via menuconfig");
    s_wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_event_group, ESP_ERR_NO_MEM, TAG, "event group");
    wifi_led_set_mode(WIFI_LED_CONNECTING);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_KVA_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_KVA_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID=%s", CONFIG_KVA_WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(20000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi connected");
        wifi_led_set_mode(WIFI_LED_CONNECTED);
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Wi-Fi connect failed");
    wifi_led_set_mode(WIFI_LED_FAILED);
    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;
    return ESP_FAIL;
}

static void wake_word_led_timeout_cb(void *arg)
{
    (void)arg;
    wake_word_led_deactivate();
}

// Wake word callback from traditional wake word service (legacy)
static void wake_word_callback(void *ctx)
{
    voice_pipeline_handle_t pipeline = (voice_pipeline_handle_t)ctx;
    wake_word_led_activate();
    // Only process wake word if not muted
    if (!s_muted) {
        voice_pipeline_handle_wake(pipeline);
    } else {
        ESP_LOGI(TAG, "Wake word detected but device is muted");
    }
    // Schedule deactivation after 2 seconds
    static esp_timer_handle_t timeout_timer = NULL;
    if (!timeout_timer) {
        const esp_timer_create_args_t args = {
            .callback = wake_word_led_timeout_cb,
            .name = "wake_word_timeout",
        };
        esp_timer_create(&args, &timeout_timer);
    }
    esp_timer_stop(timeout_timer);
    esp_timer_start_once(timeout_timer, 2000 * 1000); // 2 seconds
}

// WakeNet local control callback (parallel to Gemini streaming)
// This runs when WakeNet9l detects "hi esp" for local control
// Gemini continuous streaming continues in parallel
static void wakenet_local_control_cb(const char *wake_word, int word_index, void *ctx)
{
    voice_pipeline_handle_t pipeline = (voice_pipeline_handle_t)ctx;
    ESP_LOGI(TAG, "WakeNet local control triggered: '%s' (index=%d)", wake_word, word_index);
    
    wake_word_led_activate();
    
    // Trigger local control actions
    // This runs in parallel with Gemini continuous streaming
    if (!s_muted && pipeline) {
        ESP_LOGI(TAG, "Executing local control for wake word: %s", wake_word);
        
        // Example local control actions:
        // - Toggle lights
        // - Change volume
        // - Trigger specific intents
        // - Execute pre-defined commands
        
        // Trigger local control actions directly
        // Note: We don't call voice_pipeline_handle_wake() here because that would
        // capture audio separately and interfere with continuous Gemini streaming.
        // Instead, execute local actions directly based on wake word.
        
        // Example: Execute local commands based on wake word
        // You can extend this to handle different wake words or add voice commands after wake word
        ESP_LOGI(TAG, "Local control: Execute actions for '%s'", wake_word);
        
        // Add your local control logic here, e.g.:
        // - Toggle lights: lights_on() / lights_off()
        // - Adjust volume via spotify_client
        // - Trigger specific intents
        // - Execute pre-defined local commands
    } else {
        ESP_LOGI(TAG, "WakeNet detected but device is muted");
    }
    
    // Schedule LED deactivation
    static esp_timer_handle_t timeout_timer = NULL;
    if (!timeout_timer) {
        const esp_timer_create_args_t args = {
            .callback = wake_word_led_timeout_cb,
            .name = "wakenet_local_timeout",
        };
        esp_timer_create(&args, &timeout_timer);
    }
    esp_timer_stop(timeout_timer);
    esp_timer_start_once(timeout_timer, 2000 * 1000); // 2 seconds
}

static void button_callback(int button_id, void *ctx)
{
    voice_pipeline_handle_t pipeline = (voice_pipeline_handle_t)ctx;
    // Button 1 toggles mute (if configured)
    if (button_id == 1 && CONFIG_KVA_BUTTON1_GPIO >= 0) {
        s_muted = !s_muted;
        update_mute_led();
        ESP_LOGI(TAG, "Mute %s", s_muted ? "enabled" : "disabled");
        // Still process button for voice pipeline if not muted
        if (!s_muted) {
            voice_pipeline_handle_button(pipeline, button_id);
        }
    } else {
        voice_pipeline_handle_button(pipeline, button_id);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Naphome Voice Assistant Starting");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Firmware Version: %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "Git Commit: %s", GIT_COMMIT_HASH);
    ESP_LOGI(TAG, "Git Date: %s", GIT_COMMIT_DATE);
    ESP_LOGI(TAG, "Build Time: %s", BUILD_TIMESTAMP);
    ESP_LOGI(TAG, "Free heap: %u bytes", (unsigned int)esp_get_free_heap_size());
    ESP_LOGI(TAG, "========================================\n");
    
    // Register shutdown handler for crash reporting
    esp_register_shutdown_handler(shutdown_handler);
    
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "NVS initialized");
    
    korvo_audio_t audio = {0};
    ESP_ERROR_CHECK(korvo_audio_init(&audio, CONFIG_KVA_SAMPLE_RATE));

    audio_player_config_t audio_cfg = {
        .i2s_port = CONFIG_KVA_CODEC_I2S_PORT,
        .bclk_gpio = CONFIG_KVA_CODEC_I2S_BCLK,
        .lrclk_gpio = CONFIG_KVA_CODEC_I2S_LRCLK,
        .data_gpio = CONFIG_KVA_CODEC_I2S_DATA,
        .mclk_gpio = CONFIG_KVA_CODEC_I2S_MCLK,
        .i2c_scl_gpio = CONFIG_KVA_CODEC_I2C_SCL,
        .i2c_sda_gpio = CONFIG_KVA_CODEC_I2C_SDA,
        .default_sample_rate = 44100,
    };
    if (CONFIG_KVA_CODEC_I2S_BCLK >= 0 &&
        CONFIG_KVA_CODEC_I2S_LRCLK >= 0 &&
        CONFIG_KVA_CODEC_I2S_DATA >= 0) {
        esp_err_t audio_init_err = audio_player_init(&audio_cfg);
        if (audio_init_err != ESP_OK) {
            ESP_LOGW(TAG, "Audio player init failed (%s)", esp_err_to_name(audio_init_err));
        }
    } else {
        ESP_LOGW(TAG, "Audio player disabled (missing codec pin config)");
    }

    led_controller_t leds = {0};
    led_controller_t *led_handle = NULL;
    if (CONFIG_KVA_LED_COUNT > 0 && CONFIG_KVA_LED_STRIP_GPIO >= 0) {
        led_controller_config_t led_cfg = {
            .data_gpio = CONFIG_KVA_LED_STRIP_GPIO,
            .led_count = CONFIG_KVA_LED_COUNT,
            .brightness = CONFIG_KVA_LED_BRIGHTNESS,
            .reserved_pixels = STATUS_LED_COUNT,
        };
        if (led_controller_init(&leds, &led_cfg) == ESP_OK) {
            led_handle = &leds;
            s_led_controller_handle = led_handle;
            ESP_LOGI(TAG, "LED controller initialized: GPIO%d, %d pixels, brightness=%d", 
                     CONFIG_KVA_LED_STRIP_GPIO, CONFIG_KVA_LED_COUNT, CONFIG_KVA_LED_BRIGHTNESS);
            // Ensure lights are enabled
            s_lights_enabled = true;
            // Initialize all status LEDs - make them clearly visible at boot
            // Wi-Fi LED will be set by wifi_led_set_mode, but show it's ready with visible cyan
            // Status LEDs will be animated by trippy fade, no need to set them individually
            ESP_LOGI(TAG, "LEDs enabled and initialized - trippy fade will animate all LEDs");
            
            // Initialize device state context for Gemini function calling
            device_state_set_context(s_led_controller_handle, s_lights_enabled, s_aws_connected, s_muted, s_audio_playing);
            ESP_LOGI(TAG, "Device state context initialized for Gemini function calling");
            
            // Start trippy fade animation timer
            const esp_timer_create_args_t trippy_timer_args = {
                .callback = trippy_fade_timer_cb,
                .name = "trippy_fade",
            };
            esp_err_t trippy_timer_err = esp_timer_create(&trippy_timer_args, &s_trippy_fade_timer);
            if (trippy_timer_err == ESP_OK) {
                // Start the trippy fade animation
                led_controller_start_trippy_fade(s_led_controller_handle);
                // Update animation every 50ms for smooth 20fps animation
                esp_timer_start_periodic(s_trippy_fade_timer, 50 * 1000);
                ESP_LOGI(TAG, "Trippy fade animation started!");
            } else {
                ESP_LOGW(TAG, "Failed to create trippy fade timer: %s", esp_err_to_name(trippy_timer_err));
            }
        } else {
            ESP_LOGE(TAG, "LED controller init failed on GPIO%d", CONFIG_KVA_LED_STRIP_GPIO);
        }
    }

    // Connect to WiFi and log status
    ESP_LOGI(TAG, "Initiating WiFi connection...");
    esp_err_t wifi_ret = wifi_connect();
    if (wifi_ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connection initiated");
    } else {
        ESP_LOGE(TAG, "WiFi connection failed: %s", esp_err_to_name(wifi_ret));
    }

    intent_router_t router = {0};
    intent_router_config_t router_cfg = {
        .default_volume_step = CONFIG_KVA_SPOTIFY_VOLUME_STEP,
    };
    ESP_ERROR_CHECK(intent_router_init(&router, &router_cfg));

    spotify_client_t spotify = {0};
    spotify_client_config_t spotify_cfg = {
        .device_name = CONFIG_KVA_SPOTIFY_DEVICE_NAME,
        .volume_step = CONFIG_KVA_SPOTIFY_VOLUME_STEP,
    };
    set_status_led(SPOTIFY_LED_INDEX, 80, 40, 0); // amber while starting
    esp_err_t spotify_init_err = spotify_client_init(&spotify, &spotify_cfg);
    if (spotify_init_err != ESP_OK) {
        set_status_led(SPOTIFY_LED_INDEX, 80, 0, 0);
        ESP_LOGE(TAG, "Spotify client init failed (%s)", esp_err_to_name(spotify_init_err));
    } else {
        set_status_led(SPOTIFY_LED_INDEX, 0, 80, 0);
    }

#if CONFIG_KVA_SPOTIFY_USE_CSPOT
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "cspot ENABLED - Starting Spotify Connect player...");
    ESP_LOGI(TAG, "Device name: %s", CONFIG_KVA_SPOTIFY_DEVICE_NAME);
    ESP_LOGI(TAG, "========================================");
    spotify_player_config_t cspot_cfg = {
        .device_name = CONFIG_KVA_SPOTIFY_DEVICE_NAME,
        .credentials_path = "/spiffs/spotify_blob.json",
        .zeroconf_port = 8080,
    };
    esp_err_t cspot_err = spotify_player_start(&cspot_cfg);
    if (cspot_err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Spotify Connect player failed to start: %s", esp_err_to_name(cspot_err));
    } else {
        ESP_LOGI(TAG, "✅ Spotify Connect player started successfully (device: %s)", CONFIG_KVA_SPOTIFY_DEVICE_NAME);
    }
#else
    ESP_LOGW(TAG, "========================================");
    ESP_LOGW(TAG, "⚠️  Spotify Connect (cspot) is DISABLED in configuration");
    ESP_LOGW(TAG, "Set CONFIG_KVA_SPOTIFY_USE_CSPOT=y to enable");
    ESP_LOGW(TAG, "========================================");
#endif

    // Initialize AWS IoT bridge and MQTT service
    aws_iot_bridge_t aws_bridge = {0};
    aws_iot_bridge_config_t aws_cfg = {
        .telemetry_period_ms = CONFIG_KVA_AWS_TELEMETRY_PERIOD_MS,
    };
    set_status_led(AWS_LED_INDEX, 80, 40, 0); // amber while initializing
    esp_err_t aws_err = aws_iot_bridge_init(&aws_bridge, &aws_cfg);
    if (aws_err != ESP_OK) {
        set_status_led(AWS_LED_INDEX, 80, 0, 0);
        ESP_LOGE(TAG, "AWS IoT bridge init failed (%s)", esp_err_to_name(aws_err));
        aws_bridge = (aws_iot_bridge_t){0}; // Zero out on failure
    } else {
        ESP_LOGI(TAG, "AWS IoT bridge initialized");
        // Start Somnus MQTT service (handles AWS IoT connection)
        esp_err_t mqtt_err = somnus_mqtt_start(NULL);
        if (mqtt_err != ESP_OK) {
            set_status_led(AWS_LED_INDEX, 80, 0, 0);
            ESP_LOGW(TAG, "Somnus MQTT start failed (%s); telemetry publish disabled", esp_err_to_name(mqtt_err));
        } else {
            ESP_LOGI(TAG, "Somnus MQTT service started - AWS IoT connection in progress");
            // LED will be updated by connection callback when connected
            set_status_led(AWS_LED_INDEX, 80, 40, 0); // Keep amber until connected
        }
    }

    // Initialize Matter bridge for sensor telemetry and device control
#if CONFIG_NAPHOME_MATTER_BRIDGE_ENABLE
    matter_bridge_config_t matter_cfg = {
        .enable_matter_console = false,
    };
    esp_err_t matter_init_err = matter_bridge_init(&matter_cfg);
    if (matter_init_err == ESP_OK) {
        matter_bridge_start();
        ESP_LOGI(TAG, "Matter bridge initialized");
        
        // Register sensors with Matter bridge
        matter_bridge_sensor_registration_t env_reg = {
            .sensor_name = "sht45",
            .sensor_kind = MATTER_BRIDGE_SENSOR_KIND_ENVIRONMENT,
            .endpoint_label = "Environment",
        };
        matter_bridge_register_sensor(&env_reg);
        
        matter_bridge_sensor_registration_t iaq_reg = {
            .sensor_name = "scd40",
            .sensor_kind = MATTER_BRIDGE_SENSOR_KIND_IAQ,
            .endpoint_label = "Air Quality",
        };
        matter_bridge_register_sensor(&iaq_reg);
        
        matter_bridge_sensor_registration_t voc_reg = {
            .sensor_name = "sgp40",
            .sensor_kind = MATTER_BRIDGE_SENSOR_KIND_IAQ,
            .endpoint_label = "VOC Sensor",
        };
        matter_bridge_register_sensor(&voc_reg);
        
        matter_bridge_sensor_registration_t light_reg = {
            .sensor_name = "vcnl4040",
            .sensor_kind = MATTER_BRIDGE_SENSOR_KIND_LIGHT,
            .endpoint_label = "Ambient Light",
        };
        matter_bridge_register_sensor(&light_reg);
        
        matter_bridge_sensor_registration_t pm_reg = {
            .sensor_name = "ec10",
            .sensor_kind = MATTER_BRIDGE_SENSOR_KIND_PM,
            .endpoint_label = "Particulate Matter",
        };
        matter_bridge_register_sensor(&pm_reg);
        
        // Register Spotify device for Matter control
        matter_bridge_device_registration_t spotify_reg = {
            .device_kind = MATTER_BRIDGE_DEVICE_KIND_SPOTIFY,
            .endpoint_label = "Spotify Player",
            .device_handle = &spotify,
        };
        matter_bridge_register_device(&spotify_reg);
        ESP_LOGI(TAG, "Matter bridge: Sensors and Spotify device registered");
    } else {
        ESP_LOGW(TAG, "Matter bridge init failed (%s)", esp_err_to_name(matter_init_err));
    }
#endif

    // Initialize sensor integration (samples all sensors at 1Hz)
    // This creates the I2C bus on GPIO 44/43 (RXD/TXD)
    ESP_LOGI(TAG, "Initializing sensor integration...");
    esp_err_t sensor_init_err = sensor_integration_init();
    if (sensor_init_err == ESP_OK) {
        ESP_LOGI(TAG, "Sensor integration initialized successfully");
        
        // Now scan the I2C bus after it's been created
        ESP_LOGI(TAG, "\n=== Scanning Sensor Bus (after init) ===");
        // Note: We can't easily scan here without access to the bus handle
        // The scan will happen during sensor initialization internally
        
        // Register sensor_manager observer for Matter bridge
#if CONFIG_NAPHOME_MATTER_BRIDGE_ENABLE
        sensor_manager_set_observer(matter_bridge_sensor_observer, NULL);
        ESP_LOGI(TAG, "Matter bridge registered as sensor_manager observer");
#endif
        
        // Wait for MQTT connection before starting sensor publishing
        // This prevents race condition where sensor_manager tries to publish before MQTT is connected
        if (mqtt_err == ESP_OK) {
            ESP_LOGI(TAG, "Waiting for MQTT connection before starting sensor publishing...");
            aws_iot_client_t *client = NULL;
            int wait_count = 0;
            const int max_wait_seconds = 30; // Wait up to 30 seconds
            
            while (wait_count < max_wait_seconds * 10) { // Check every 100ms
                client = aws_iot_service_get_client();
                if (client && aws_iot_client_is_connected(client)) {
                    ESP_LOGI(TAG, "MQTT connected, starting sensor publishing");
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                wait_count++;
            }
            
            if (wait_count >= max_wait_seconds * 10) {
                ESP_LOGW(TAG, "MQTT connection timeout after %d seconds, starting sensors anyway (will retry on publish)", max_wait_seconds);
            }
        }
        
        // Start sensor integration (begins 1Hz sampling)
        esp_err_t sensor_start_err = sensor_integration_start();
        if (sensor_start_err == ESP_OK) {
            ESP_LOGI(TAG, "Sensor integration started (1Hz sampling)");
        } else {
            ESP_LOGE(TAG, "Sensor integration start failed (%s)", esp_err_to_name(sensor_start_err));
        }
    } else {
        ESP_LOGE(TAG, "Sensor integration init failed (%s) - system may be unstable", esp_err_to_name(sensor_init_err));
        ESP_LOGE(TAG, "Free heap: %u bytes", (unsigned int)esp_get_free_heap_size());
    }
    
    // Scan Audio Bus separately (I2C_NUM_1 on GPIO 1/2) - this is safe as it's a different port
    if (CONFIG_KVA_CODEC_I2C_SDA >= 0 && CONFIG_KVA_CODEC_I2C_SCL >= 0) {
        ESP_LOGI(TAG, "\n=== Scanning Audio Bus (I2C_NUM_1) ===");
        scan_i2c_bus(I2C_NUM_1, CONFIG_KVA_CODEC_I2C_SDA, CONFIG_KVA_CODEC_I2C_SCL, "Audio Bus");
    }

    // Enable Gemini AI streaming with AFE pipeline: I2S -> AEC -> BSS/NS -> VAD -> Gemini Batch STT -> Gemini LLM -> Gemini TTS
    // Enable WakeNet9l in parallel for local control
    voice_pipeline_config_t pipeline_cfg = {
        .use_gemini = true,  // Use Gemini AI exclusively
        .audio = &audio,
        .router = &router,
        .spotify = &spotify,
        .aws_bridge = (aws_bridge.metrics_mutex != NULL) ? &aws_bridge : NULL, // AWS IoT bridge if initialized
        .leds = led_handle,
        .sample_rate_hz = CONFIG_KVA_SAMPLE_RATE,
        .capture_ms = CONFIG_KVA_CAPTURE_MS,
        .tts_voice = CONFIG_KVA_TTS_VOICE,
        .use_realtime_streaming = false,  // Gemini batch capture mode (no continuous streaming)
        .skip_wake_word = true,           // Skip traditional wake word service
        .enable_wakenet_local = true,     // Enable WakeNet9l in parallel for local control
        .wakenet_model = "wn9_hiesp",     // WakeNet9l model: "hi esp"
        .wakenet_threshold = 60,           // Detection threshold (0-100)
    };
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "=== GEMINI STT-LLM-TTS WITH FUNCTION CALLING ENABLED ===");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "Using Gemini AI exclusively for STT-LLM-TTS");
    ESP_LOGI(TAG, "Pipeline: I2S -> AEC -> BSS/NS -> VAD -> Gemini Batch STT -> Gemini LLM (with tools) -> Gemini TTS");
    ESP_LOGI(TAG, "Gemini can now control: LEDs, Audio, and query: Health, Sensors, Temperature");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    
    voice_pipeline_handle_t pipeline = voice_pipeline_create(&pipeline_cfg);
    if (!pipeline) {
        ESP_LOGE(TAG, "pipeline init failed");
        return;
    }
    ESP_ERROR_CHECK(voice_pipeline_start(pipeline));
    
    // Set WakeNet local control callback (parallel to Gemini streaming)
    if (pipeline_cfg.enable_wakenet_local) {
        voice_pipeline_set_wake_callback(pipeline, wakenet_local_control_cb, pipeline);
        ESP_LOGI(TAG, "WakeNet9l enabled in parallel for local control: model=%s", pipeline_cfg.wakenet_model);
    }

    // Skip traditional wake word service when using realtime streaming + WakeNet
    // WakeNet is handled directly in AFE pipeline
    wake_word_service_t *wake_service = NULL;
    if (!pipeline_cfg.use_realtime_streaming || (!pipeline_cfg.skip_wake_word && !pipeline_cfg.enable_wakenet_local)) {
        int wake_cooldown_ms = CONFIG_KVA_CAPTURE_MS + 500;
        if (wake_cooldown_ms < 1500) {
            wake_cooldown_ms = 1500;
        }
        wake_word_service_config_t wake_cfg = {
            .audio = &audio,
            .aws_bridge = (aws_bridge.metrics_mutex != NULL) ? &aws_bridge : NULL, // AWS IoT bridge if initialized
            .sensitivity = CONFIG_KVA_WAKE_WORD_SENSITIVITY,
            .simulated_interval_ms = CONFIG_KVA_SIMULATED_WAKE_INTERVAL_MS,
            .cooldown_ms = wake_cooldown_ms,
        };
        wake_service = wake_word_service_start(&wake_cfg, wake_word_callback, pipeline);
        if (!wake_service) {
            ESP_LOGE(TAG, "wake service init failed");
            return;
        }
    } else {
        if (pipeline_cfg.enable_wakenet_local) {
            ESP_LOGI(TAG, "Traditional wake word service skipped - using WakeNet9l in AFE pipeline for local control");
        } else {
            ESP_LOGI(TAG, "Wake word detection skipped - using continuous realtime streaming");
        }
    }

    button_service_t *button_service = NULL;
    if (CONFIG_KVA_BUTTON1_GPIO >= 0) {
        const button_service_button_t buttons[] = {
            {
                .id = 1,
                .gpio = CONFIG_KVA_BUTTON1_GPIO,
                .active_low = CONFIG_KVA_BUTTON1_ACTIVE_LOW,
            },
        };
        button_service_config_t button_cfg = {
            .buttons = buttons,
            .button_count = 1,
            .callback = button_callback,
            .callback_ctx = pipeline,
            .debounce_ms = CONFIG_KVA_BUTTON_DEBOUNCE_MS,
        };
        button_service = button_service_start(&button_cfg);
        if (!button_service) {
            ESP_LOGW(TAG, "Button service failed to start (GPIO %d)", CONFIG_KVA_BUTTON1_GPIO);
        }
    }

    if (pipeline_cfg.use_realtime_streaming) {
        if (pipeline_cfg.enable_wakenet_local) {
            ESP_LOGI(TAG, "Assistant ready: Gemini streaming + WakeNet9l local control");
            ESP_LOGI(TAG, "  Pipeline: I2S -> AEC -> BSS/NS -> VAD -> Gemini (continuous)");
            ESP_LOGI(TAG, "  Pipeline: I2S -> AEC -> BSS/NS -> WakeNet -> Local Control (triggered)");
            ESP_LOGI(TAG, "  Say '%s' for local control, or speak for Gemini transcription", pipeline_cfg.wakenet_model);
        } else {
            ESP_LOGI(TAG, "Assistant ready with Gemini continuous streaming (AFE-enabled, no wake word)");
        }
    } else {
        ESP_LOGI(TAG, "Assistant ready. Say \"Naptick\" or wait for simulated wake events or button presses.");
    }
    
    // Initialize serial command parser for dashboard control
    serial_command_parser_init(&spotify);
    ESP_LOGI(TAG, "Serial command parser initialized for Spotify control");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
