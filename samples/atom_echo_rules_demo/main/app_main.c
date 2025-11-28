/**
 * Atom Echo Rules Demo
 *
 * This sample builds on the original rule-store demo by wiring in Wi-Fi
 * onboarding, AWS IoT (Somnus MQTT), Spotify Connect via cspot, and Google's
 * Gemini APIs. The cloud features intentionally log their behaviour so the
 * firmware can be exercised even if credentials are not provided.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cJSON.h"
#include "device_state.h"
#include "display_matrix.h"
#include "status_display.h"
#include "led_controller.h"
#include "naphome_image.h"
#include "naphome_face_image.h"
#include "face_led_simulator.h"
#include "sensor_reader.h"
#include "somnus_action_handler.h"
#include "driver/spi_common.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gemini_client.h"
#include "gemini_secrets.h"
#include "i2c_scanner.h"
#include "kva_config_defaults.h"
#include "nvs_flash.h"
#include "rule_store.h"
#include "rule_update_channel.h"
#include "scene_controller.h"
#include "somnus_ble.h"
#include "somnus_mqtt.h"
#include "spotify_client.h"
#include "spotify_player.h"
#include "wifi_manager.h"
#include "webserver.h"
#include "ota_updater.h"
#include "audio_player.h"
#include "driver/i2c.h"

#define TAG "atom_echo_demo"

static rule_update_channel_t *s_rule_channel;
static bool s_wifi_connected;
static char s_wifi_ssid[33];
static bool s_aws_started;
static bool s_spotify_ready;
static bool s_gemini_ready;
static bool s_spotify_player_started;
static bool s_gemini_api_missing_logged;
static spotify_client_t s_spotify_client;
static bool s_lights_available;
static void *s_led_handle;
static display_matrix_t *s_display = NULL;
static status_display_t s_status_display;
static face_led_simulator_t s_face_led_simulator;

// Voice/audio state tracking is now handled directly by status_display_set_* functions
// which update the status_display_t structure from the Gemini pipeline
static webserver_t *s_webserver = NULL;
static ota_updater_t *s_ota_updater = NULL;
static bool s_mdns_initialized = false;

void aws_led_set_connected(bool connected);

// Forward declarations
static void maybe_start_cloud_services(void);
static void log_device_snapshot(void);
static void status_display_update_task(void *pvParameters);
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

static void mqtt_action_handler(const char *payload, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "Somnus MQTT payload: %s", payload);
    
    // Try SomnusDevice-style action handler first
    if (s_led_handle) {
        esp_err_t err = somnus_action_handler_process(payload, (scene_controller_t *)s_led_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Action processed successfully");
            return;
        }
        // If action handler doesn't recognize it, fall through to rule channel
    }
    
    // Fall back to rule channel for rule-based actions
    if (s_rule_channel) {
        rule_update_channel_handle_mqtt(s_rule_channel, payload);
    } else {
        ESP_LOGW(TAG, "MQTT action received but no handler available");
    }
}

static esp_err_t mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "rules",
        .max_files = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK) {
        size_t total = 0, used = 0;
        if (esp_spiffs_info(conf.partition_label, &total, &used) == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS mounted: total=%d used=%d", (int)total, (int)used);
        }
    } else {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(err));
    }
    return err;
}

static void rule_store_observer(const rule_store_snapshot_t *snapshot,
                                rule_store_source_t source,
                                void *ctx)
{
    (void)source;
    rule_update_channel_t *channel = (rule_update_channel_t *)ctx;
    if (!channel || !snapshot) {
        return;
    }
    rule_update_channel_apply_snapshot(channel, snapshot);
}

static void log_device_snapshot(void)
{
    char *json = device_state_to_json();
    if (json) {
        ESP_LOGI(TAG, "Device state: %s", json);
        free(json);
    } else {
        ESP_LOGW(TAG, "Failed to serialise device state");
    }
}

static void init_mdns(void)
{
    if (s_mdns_initialized) {
        ESP_LOGD(TAG, "mDNS already initialized, skipping");
        return;
    }
    
    // Verify we have an IP address before initializing mDNS
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        ESP_LOGW(TAG, "WIFI_STA_DEF netif not found, cannot initialize mDNS");
        return;
    }
    
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        ESP_LOGW(TAG, "No IP address yet, deferring mDNS initialization");
        return;
    }
    
    ESP_LOGI(TAG, "Initializing mDNS with IP: " IPSTR, IP2STR(&ip_info.ip));
    
    // Set netif hostname FIRST (required for mDNS to work properly)
    esp_err_t netif_err = esp_netif_set_hostname(netif, "nap");
    if (netif_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set netif hostname: %s", esp_err_to_name(netif_err));
    } else {
        ESP_LOGI(TAG, "Netif hostname set to: nap");
    }
    
    // Check if mDNS was already initialized (e.g., by spotify_player)
    esp_err_t err = mdns_init();
    if (err == ESP_ERR_INVALID_STATE) {
        // mDNS already initialized, just update hostname
        ESP_LOGI(TAG, "mDNS already initialized, updating hostname");
        s_mdns_initialized = true;  // Update flag to reflect actual state
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS initialization failed: %s", esp_err_to_name(err));
        return;
    } else {
        ESP_LOGI(TAG, "mDNS initialized successfully");
    }
    
    // Set mDNS hostname
    err = mdns_hostname_set("nap");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS hostname set failed: %s", esp_err_to_name(err));
        if (err != ESP_ERR_INVALID_STATE) {
            mdns_free();
        }
        return;
    }
    ESP_LOGI(TAG, "mDNS hostname set to: nap");
    
    // Set default instance
    err = mdns_instance_name_set("M5 Atom Echo");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS instance name set failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "mDNS instance name set to: M5 Atom Echo");
    }
    
    // Add HTTP service with TXT records
    mdns_txt_item_t txt[] = {
        {"path", "/"},
    };
    err = mdns_service_add(NULL, "_http", "_tcp", 80, txt, 1);
    if (err == ESP_ERR_INVALID_STATE) {
        // Service might already exist, try to remove and re-add
        mdns_service_remove("_http", "_tcp");
        err = mdns_service_add(NULL, "_http", "_tcp", 80, txt, 1);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS service add failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "mDNS HTTP service added on port 80");
    }
    
    ESP_LOGI(TAG, "mDNS fully initialized: nap.local (http://nap.local/)");
    s_mdns_initialized = true;
}

static void handle_wifi_connected(const char *ssid)
{
    s_wifi_connected = true;
    if (ssid && ssid[0]) {
        strlcpy(s_wifi_ssid, ssid, sizeof(s_wifi_ssid));
    }
    device_state_set_wifi(true, s_wifi_ssid);
    ESP_LOGI(TAG, "Wi-Fi connected%s%s",
             s_wifi_ssid[0] ? " to " : "",
             s_wifi_ssid[0] ? s_wifi_ssid : "");
    device_state_set_context(s_led_handle,
                             s_lights_available,
                             s_aws_started,
                             false,
                             s_spotify_player_started);
    
    // mDNS will be initialized in IP event handler when IP is obtained
    // Don't initialize here as we need an IP address first
    
    // Update status display - DISABLED to keep background image visible
    // if (s_display) {
    //     bool ble_running = somnus_ble_is_running();
    //     status_display_update(&s_status_display,
    //                         s_wifi_connected,
    //                         s_aws_started,
    //                         ble_running,
    //                         s_spotify_ready,
    //                         s_gemini_ready,
    //                         true);
    // }
    
    maybe_start_cloud_services();
}

static void handle_wifi_disconnected(void)
{
    if (!s_wifi_connected) {
        return;
    }
    s_wifi_connected = false;
    s_wifi_ssid[0] = '\0';
    s_aws_started = false;
    device_state_set_wifi(false, NULL);
    device_state_set_aws(false);
    ESP_LOGW(TAG, "Wi-Fi disconnected");
    device_state_set_context(s_led_handle,
                             s_lights_available,
                             s_aws_started,
                             false,
                             s_spotify_player_started);
    
    // Update status display - DISABLED to keep background image visible
    // if (s_display) {
    //     bool ble_running = somnus_ble_is_running();
    //     status_display_update(&s_status_display,
    //                         s_wifi_connected,
    //                         s_aws_started,
    //                         ble_running,
    //                         s_spotify_ready,
    //                         s_gemini_ready,
    //                         true);
    // }
}

// I2C scanning callback - currently disabled to reduce stack usage
/*
static void i2c_collect_cb(const i2c_bus_config_t *bus,
                           uint8_t address,
                           const char *friendly_name,
                           void *ctx)
{
    cJSON *root = (cJSON *)ctx;
    const char *label = (bus && bus->label) ? bus->label : "bus";
    cJSON *array = cJSON_GetObjectItem(root, label);
    if (!array) {
        array = cJSON_CreateArray();
        cJSON_AddItemToObject(root, label, array);
    }
    char entry[80];
    if (friendly_name) {
        snprintf(entry, sizeof(entry), "0x%02X (%s)", address, friendly_name);
    } else {
        snprintf(entry, sizeof(entry), "0x%02X", address);
    }
    cJSON_AddItemToArray(array, cJSON_CreateString(entry));
}
*/

static bool ble_connect_wifi_cb(const char *ssid,
                                const char *password,
                                const char *token,
                                bool is_production,
                                void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "BLE onboarding request: SSID=\"%s\" (production=%s, token len=%d)",
             ssid,
             is_production ? "true" : "false",
             token ? (int)strlen(token) : 0);

    esp_err_t err = wifi_manager_connect(ssid, password, pdMS_TO_TICKS(20000));
    if (err != ESP_OK) {
        somnus_ble_notify("Wi-Fi connection failed");
        return false;
    }

    somnus_ble_notify("Wi-Fi connected. Starting cloud services…");
    handle_wifi_connected(ssid);
    return true;
}

static void maybe_start_cloud_services(void)
{
    if (!wifi_manager_is_connected()) {
        return;
    }

    if (!s_aws_started) {
        somnus_mqtt_config_t mqtt_cfg = {
            .action_cb = mqtt_action_handler,
            .action_ctx = NULL,
        };
        aws_led_set_connected(false);
        esp_err_t err = somnus_mqtt_start(&mqtt_cfg);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Somnus MQTT service started");
            s_aws_started = true;
            device_state_set_aws(true);
            
            // Update status display
            if (s_display) {
                bool ble_running = somnus_ble_is_running();
                bool ble_advertising = somnus_ble_is_advertising();
                bool ble_connected = somnus_ble_is_connected();
                status_display_update(&s_status_display,
                                    s_wifi_connected,
                                    s_aws_started,
                                    ble_running,      // ble_initialized
                                    ble_advertising, // ble_advertising
                                    ble_connected,   // ble_connected
                                    s_spotify_ready,
                                    s_gemini_ready,
                                    true,            // system_ok
                                    false,           // listening
                                    false,           // vad_active
                                    false);          // speaking
            }
        } else {
            ESP_LOGW(TAG, "Somnus MQTT start failed (%s) – will retry after next Wi-Fi event", esp_err_to_name(err));
        }
    }

    if (!s_spotify_ready) {
        const char *device_name = "Atom Echo";
        spotify_client_config_t cfg = {
            .device_name = device_name,
            .volume_step = 8,
        };
        esp_err_t init_err = spotify_client_init(&s_spotify_client, &cfg);
        if (init_err == ESP_OK) {
            s_spotify_ready = true;
            device_state_set_spotify(true);
            
            // Update status display
            if (s_display) {
                bool ble_running = somnus_ble_is_running();
                bool ble_advertising = somnus_ble_is_advertising();
                bool ble_connected = somnus_ble_is_connected();
                status_display_update(&s_status_display,
                                    s_wifi_connected,
                                    s_aws_started,
                                    ble_running,      // ble_initialized
                                    ble_advertising, // ble_advertising
                                    ble_connected,   // ble_connected
                                    s_spotify_ready,
                                    s_gemini_ready,
                                    true,            // system_ok
                                    false,           // listening
                                    false,           // vad_active
                                    false);          // speaking
            }
#if CONFIG_KVA_SPOTIFY_USE_CSPOT
            // Temporarily disabled: Spotify Connect HTTP server thread creation fails
            // causing system crash. Need to investigate thread/stack resource limits.
            ESP_LOGW(TAG, "Spotify Connect player disabled (thread creation issue)");
            s_spotify_player_started = false;
            /*
            if (!s_spotify_player_started) {
                spotify_player_config_t player_cfg = {
                    .device_name = device_name,
                    .credentials_path = "/spiffs/spotify_blob.json",
                    .zeroconf_port = 8080,
                };
                esp_err_t player_err = spotify_player_start(&player_cfg);
                if (player_err == ESP_OK) {
                    s_spotify_player_started = true;
                    somnus_ble_notify("Spotify Connect ready");
                    ESP_LOGI(TAG, "Spotify Connect player started");
                } else {
                    ESP_LOGW(TAG, "Spotify Connect start failed (%s)", esp_err_to_name(player_err));
                }
            }
            */
#endif
        } else {
            ESP_LOGE(TAG, "Spotify client init failed (%s)", esp_err_to_name(init_err));
            device_state_set_spotify(false);
        }
    }

    // Defer Gemini initialization to reduce stack usage during boot
    // Will be initialized in a separate task after boot completes
    if (!s_gemini_ready) {
        const char *api_key = NULL;
#ifdef GEMINI_API_KEY
        api_key = GEMINI_API_KEY;
#else
        api_key = getenv("GEMINI_API_KEY");
#endif
        if (!api_key || api_key[0] == '\0') {
            if (!s_gemini_api_missing_logged) {
                ESP_LOGW(TAG, "Gemini API key not configured; skipping Gemini setup");
                s_gemini_api_missing_logged = true;
            }
            device_state_set_gemini(false, "Gemini API key not configured");
        } else {
            ESP_LOGI(TAG, "Gemini initialization deferred to reduce stack usage");
            // Will be initialized later in a separate task
        }
    }

    device_state_set_context(s_led_handle,
                             s_lights_available,
                             s_aws_started,
                             false,
                             s_spotify_player_started);

    log_device_snapshot();
}

void app_main(void)
{
    // Early logging to catch crash location - this should be the FIRST thing that runs
    ESP_LOGI(TAG, "=== app_main() START ===");
    ESP_LOGI(TAG, "Free heap at start: %u bytes", (unsigned int)esp_get_free_heap_size());
    
    esp_err_t err = nvs_flash_init();
    ESP_LOGI(TAG, "NVS init: %s", esp_err_to_name(err));
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(mount_spiffs());
    device_state_init();
    device_state_set_wifi(false, NULL);
    device_state_set_aws(false);
    device_state_set_spotify(false);
    device_state_set_gemini(false, NULL);

    // Initialize BLE FIRST - before display/audio/I2C/WiFi to avoid resource conflicts
    // I2C driver conflicts could prevent BT controller from initializing properly
    // WiFi coexistence must be configured before WiFi starts, so BLE comes first
    ESP_LOGI(TAG, "Initializing BLE FIRST (before I2C/display/audio/WiFi)...");
    somnus_ble_config_t ble_cfg = {
        .connect_cb = ble_connect_wifi_cb,
        .connect_ctx = NULL,
    };
    esp_err_t ble_err = somnus_ble_start(&ble_cfg);
    bool ble_running = false;
    if (ble_err == ESP_OK) {
        ble_running = somnus_ble_is_running();
        ESP_LOGI(TAG, "BLE service started (advertising: %s)", ble_running ? "yes" : "no");
    } else {
        ESP_LOGE(TAG, "Failed to start Somnus BLE service (%s)", esp_err_to_name(ble_err));
        ESP_LOGE(TAG, "BLE initialization failure may cause WiFi/BLE coexistence issues");
    }

    display_matrix_t *display = NULL;
    display_matrix_config_t display_cfg = {
        .spi_host = SPI2_HOST,
        .sclk_gpio = CONFIG_ATOM_ECHO_LCD_SCLK_GPIO,
        .mosi_gpio = CONFIG_ATOM_ECHO_LCD_MOSI_GPIO,
        .cs_gpio = CONFIG_ATOM_ECHO_LCD_CS_GPIO,
        .dc_gpio = CONFIG_ATOM_ECHO_LCD_DC_GPIO,
        .reset_gpio = CONFIG_ATOM_ECHO_LCD_RST_GPIO,
        .backlight_gpio = CONFIG_ATOM_ECHO_LCD_BL_GPIO,
        .max_transfer_bytes = CONFIG_ATOM_ECHO_LCD_MAX_TRANSFER,
        .tile_rows = 10,
        .tile_cols = 10,
        .panel_width = 128,  // GC9107 on AtomS3R is 128x128 (per M5GFX)
        .panel_height = 128, // Note: M5GFX handles actual initialization
    };

    ESP_LOGI(TAG, "Display config: SCLK=%d, MOSI=%d, CS=%d, DC=%d, RST=%d, BL=%d",
             display_cfg.sclk_gpio, display_cfg.mosi_gpio, display_cfg.cs_gpio,
             display_cfg.dc_gpio, display_cfg.reset_gpio, display_cfg.backlight_gpio);
    
    if (display_cfg.sclk_gpio >= 0 && display_cfg.mosi_gpio >= 0 && display_cfg.cs_gpio >= 0 &&
        display_cfg.dc_gpio >= 0 && display_cfg.reset_gpio >= 0) {
        ESP_LOGI(TAG, "Initializing display...");
        err = display_matrix_init(&display, &display_cfg);
        if (err == ESP_OK) {
            s_display = display;
            ESP_LOGI(TAG, "Display initialized successfully");
            
            ESP_LOGI(TAG, "Display initialised as 10x10 tile grid");
            
            // Display naphome.png image (moved down 15px to make room for status icons)
            err = display_matrix_draw_bitmap(display, 0, 15, 
                                             NAPHOME_WIDTH, NAPHOME_HEIGHT, 
                                             naphome_data);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Naphome image displayed successfully");
            } else {
                ESP_LOGW(TAG, "Failed to display naphome image (%s)", esp_err_to_name(err));
            }
            
            // Initialize face LED simulator
            ESP_LOGI(TAG, "Initializing face LED simulator...");
            err = face_led_simulator_init(&s_face_led_simulator, display, naphome_face_data, naphome_data);
            bool face_led_ok = (err == ESP_OK);
            if (face_led_ok) {
                ESP_LOGI(TAG, "Face LED simulator initialized");
                // Register with action handler for synchronized updates
                somnus_action_handler_set_face_simulator(&s_face_led_simulator);
            } else {
                ESP_LOGW(TAG, "Face LED simulator init failed (%s)", esp_err_to_name(err));
            }
            
            // Initialize status display - will draw icons on top of Naphome background
            err = status_display_init(&s_status_display, display);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Status display initialized");
                // Draw status icons on top of Naphome background
                status_display_update(&s_status_display,
                                    false,  // wifi
                                    false,  // aws
                                    false,  // ble_initialized
                                    false,  // ble_advertising
                                    false,  // ble_connected
                                    false,  // spotify
                                    false,  // gemini
                                    true,   // system_ok
                                    false,  // listening
                                    false,  // vad_active
                                    false); // speaking
            } else {
                ESP_LOGW(TAG, "Status display init failed (%s)", esp_err_to_name(err));
                // Continue even if status display fails
            }
            
            // Initialize audio player BEFORE LED demo (so we can test audio immediately)
            ESP_LOGI(TAG, "=== INITIALIZING AUDIO (before LED demo) ===");
#if CONFIG_ATOM_ECHO_AUDIO_ENABLED
            if (CONFIG_ATOM_ECHO_AUDIO_I2S_BCLK >= 0 &&
                CONFIG_ATOM_ECHO_AUDIO_I2S_LRCLK >= 0 &&
                CONFIG_ATOM_ECHO_AUDIO_I2S_DATA >= 0) {
                ESP_LOGI(TAG, "Initializing audio player (ES8388/ES8311 codec)...");
                ESP_LOGI(TAG, "  I2C: SDA=GPIO%d, SCL=GPIO%d", 
                         CONFIG_ATOM_ECHO_AUDIO_I2C_SDA, CONFIG_ATOM_ECHO_AUDIO_I2C_SCL);
                ESP_LOGI(TAG, "  I2S: BCLK=GPIO%d, LRCLK=GPIO%d, DATA=GPIO%d, MCLK=%d",
                         CONFIG_ATOM_ECHO_AUDIO_I2S_BCLK, CONFIG_ATOM_ECHO_AUDIO_I2S_LRCLK,
                         CONFIG_ATOM_ECHO_AUDIO_I2S_DATA, CONFIG_ATOM_ECHO_AUDIO_I2S_MCLK);
                
                audio_player_config_t audio_cfg = {
                    .i2s_port = I2S_NUM_1,
                    .bclk_gpio = CONFIG_ATOM_ECHO_AUDIO_I2S_BCLK,
                    .lrclk_gpio = CONFIG_ATOM_ECHO_AUDIO_I2S_LRCLK,
                    .data_gpio = CONFIG_ATOM_ECHO_AUDIO_I2S_DATA,
                    .mclk_gpio = CONFIG_ATOM_ECHO_AUDIO_I2S_MCLK >= 0 ? (gpio_num_t)CONFIG_ATOM_ECHO_AUDIO_I2S_MCLK : (gpio_num_t)-1,
                    .i2c_scl_gpio = CONFIG_ATOM_ECHO_AUDIO_I2C_SCL,
                    .i2c_sda_gpio = CONFIG_ATOM_ECHO_AUDIO_I2C_SDA,
                    .default_sample_rate = CONFIG_ATOM_ECHO_AUDIO_SAMPLE_RATE,
                };
                
                ESP_LOGI(TAG, "NOTE: Echo Base uses ES8311 codec (per M5Stack config)");
                ESP_LOGI(TAG, "  I2C: GPIO38/39 (I2C_NUM_0), I2S: BCLK=GPIO8, LRCLK=GPIO6, DATA=GPIO5");
                ESP_LOGI(TAG, "  Sample rate: %d Hz (Echo Base requires 16kHz, not 44.1kHz)", CONFIG_ATOM_ECHO_AUDIO_SAMPLE_RATE);
                ESP_LOGI(TAG, "Audio player will handle I2C setup and codec detection");
                
                esp_err_t audio_err = audio_player_init(&audio_cfg);
                if (audio_err == ESP_OK) {
                    ESP_LOGI(TAG, "Audio player initialized successfully");
                    
                    // Play a simple beep to test audio (440Hz tone for 200ms)
                    ESP_LOGI(TAG, "Playing audio test beep...");
                    const int sample_rate = CONFIG_ATOM_ECHO_AUDIO_SAMPLE_RATE;
                    const float frequency = 440.0f;  // A4 note
                    const float duration = 0.2f;     // 200ms
                    const int num_samples = (int)(sample_rate * duration);
                    int16_t *tone_samples = malloc(num_samples * sizeof(int16_t));
                    if (tone_samples) {
                        for (int i = 0; i < num_samples; i++) {
                            float t = (float)i / sample_rate;
                            float value = sinf(2.0f * 3.14159f * frequency * t);
                            // Apply envelope (fade in/out to avoid clicks)
                            float envelope = 1.0f;
                            if (i < num_samples / 10) {
                                envelope = (float)i / (num_samples / 10.0f);  // Fade in
                            } else if (i > num_samples * 9 / 10) {
                                envelope = (float)(num_samples - i) / (num_samples / 10.0f);  // Fade out
                            }
                            tone_samples[i] = (int16_t)(value * envelope * 30000.0f);  // ~90% volume for better audibility
                        }
                        esp_err_t beep_err = audio_player_submit_pcm(tone_samples, num_samples, sample_rate, 1);
                        if (beep_err == ESP_OK) {
                            ESP_LOGI(TAG, "Audio test beep played successfully");
                            // Wait for beep to finish
                            vTaskDelay(pdMS_TO_TICKS((int)(duration * 1000) + 100));
                        } else {
                            ESP_LOGW(TAG, "Audio test beep failed: %s", esp_err_to_name(beep_err));
                        }
                        free(tone_samples);
                    }
                } else {
                    ESP_LOGE(TAG, "Audio player init failed: %s", esp_err_to_name(audio_err));
                    ESP_LOGE(TAG, "  Check I2C/I2S connections and ES8311 codec power");
                    ESP_LOGE(TAG, "  Expected ES8311 at I2C address 0x18 or 0x19");
                }
            } else {
                ESP_LOGW(TAG, "Audio player disabled (pins not configured)");
            }
#else
            ESP_LOGW(TAG, "Audio player disabled (CONFIG_ATOM_ECHO_AUDIO_ENABLED=n)");
#endif
            
            // LED demo disabled for faster boot and BLE initialization
            // Demo: Cycle through all LED patterns on init (after status icons are shown)
            #if 0
            if (face_led_ok) {
                ESP_LOGI(TAG, "Starting LED pattern demo loop...");
                struct {
                    face_led_pattern_t pattern;
                    uint8_t r, g, b;
                    const char *name;
                    int duration_ms;
                } demo_patterns[] = {
                    {FACE_LED_PATTERN_NONE, 255, 100, 100, "Solid Red", 2000},
                    {FACE_LED_PATTERN_BREATHING, 0, 255, 255, "Breathing Cyan", 3000},
                    {FACE_LED_PATTERN_GRADIENT_RED_BLUE, 255, 150, 150, "Red-Blue Gradient", 4000},
                    {FACE_LED_PATTERN_GRADIENT_RED_YELLOW, 220, 38, 38, "Red-Yellow Gradient", 4000},
                    {FACE_LED_PATTERN_GRADIENT_WHITE_BLUE, 14, 165, 233, "White-Blue Gradient", 4000},
                    {FACE_LED_PATTERN_GRADIENT_BLUE_GREEN, 6, 182, 212, "Blue-Green Gradient", 4000},
                    {FACE_LED_PATTERN_GRADIENT_TEAL_ORANGE, 244, 114, 182, "Teal-Orange Gradient", 4000},
                    {FACE_LED_PATTERN_PULSE_ORANGE, 255, 135, 0, "Orange Pulse", 5000},
                    {FACE_LED_PATTERN_PULSE_LILAC, 255, 100, 255, "Lilac Pulse", 5000},
                };
                
                for (int i = 0; i < sizeof(demo_patterns) / sizeof(demo_patterns[0]); i++) {
                    ESP_LOGI(TAG, "Demo pattern %d/%d: %s", i + 1, 
                             (int)(sizeof(demo_patterns) / sizeof(demo_patterns[0])), 
                             demo_patterns[i].name);
                    face_led_simulator_set_pattern(&s_face_led_simulator,
                                                   demo_patterns[i].pattern,
                                                   demo_patterns[i].r,
                                                   demo_patterns[i].g,
                                                   demo_patterns[i].b,
                                                   0.8f); // 80% intensity
                    vTaskDelay(pdMS_TO_TICKS(demo_patterns[i].duration_ms));
                }
                
                // Stop demo and return to background image
                face_led_simulator_stop_pattern(&s_face_led_simulator);
                ESP_LOGI(TAG, "LED pattern demo complete, returning to background image");
                
                // Redraw background image (moved down 15px)
                display_matrix_draw_bitmap(display, 0, 15, 
                                         NAPHOME_WIDTH, NAPHOME_HEIGHT, 
                                         naphome_data);
            }
            #endif
        } else {
            ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "Display pins not configured (some pins < 0), skipping display init");
    }

    // Audio initialization moved to before LED demo (see above)

    scene_controller_t *scene = NULL;
    float master_brightness = CONFIG_ATOM_ECHO_NEOPIXEL_BRIGHTNESS / 255.0f;
    if (master_brightness <= 0.0f) {
        master_brightness = 0.1f;
    }
    scene_controller_config_t scene_cfg = {
        .led_gpio = CONFIG_ATOM_ECHO_NEOPIXEL_GPIO,
        .led_pixel_count = CONFIG_ATOM_ECHO_NEOPIXEL_COUNT,
        .led_pixel_order = 0,
        .master_brightness = master_brightness,
    };
    if (scene_cfg.led_gpio >= 0 && scene_cfg.led_pixel_count > 0) {
        if (scene_controller_init(&scene, &scene_cfg) != ESP_OK) {
            ESP_LOGW(TAG, "Scene controller init failed");
            scene = NULL;
        }
    }
    s_lights_available = (scene != NULL);
    s_led_handle = scene;
    device_state_set_context(s_led_handle,
                             s_lights_available,
                             s_aws_started,
                             false,
                             false);

    rule_store_t *store = NULL;
    rule_store_config_t store_cfg = {
        .auto_flush = true,
        .spiffs_path = "/spiffs/rules.json",
    };
    ESP_ERROR_CHECK(rule_store_init(&store, &store_cfg));

    rule_update_channel_config_t rule_channel_cfg = {
        .store = store,
        .scene = scene,
        .display = display,
    };
    ESP_ERROR_CHECK(rule_update_channel_init(&s_rule_channel, &rule_channel_cfg));
    rule_store_set_observer(store, rule_store_observer, s_rule_channel);

    // Initialize OTA updater
    // GitHub token should be set via Kconfig (CONFIG_OTA_GITHUB_TOKEN) or environment variable
    // For security, never hardcode tokens in source code
    const char *github_token = NULL;
    // Check Kconfig first (string configs are always defined, but may be empty)
    if (strlen(CONFIG_OTA_GITHUB_TOKEN) > 0) {
        github_token = CONFIG_OTA_GITHUB_TOKEN;
        ESP_LOGI(TAG, "Using GitHub token from Kconfig");
    } else {
        // Fallback to environment variable if Kconfig not set or empty
        github_token = getenv("GITHUB_TOKEN");
        if (github_token && github_token[0] != '\0') {
            ESP_LOGI(TAG, "Using GitHub token from environment variable");
        } else {
            github_token = NULL;  // OTA will work without token but may hit rate limits
            ESP_LOGW(TAG, "GitHub token not configured - OTA may hit API rate limits");
        }
    }
    ota_updater_config_t ota_cfg = {
        .github_repo = "Naptick/Naphome_Firmware",
        .current_version = "0.1",
        .check_interval_seconds = 3600,  // Check every hour (0 = manual only)
        .auto_update = false,  // Set to true for automatic updates
        .github_token = github_token,  // GitHub PAT for API access (from config or env)
    };
    err = ota_updater_init(&s_ota_updater, &ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA updater initialized");
        // Optionally start periodic checking
        // ota_updater_start_periodic_check(s_ota_updater);
    } else {
        ESP_LOGW(TAG, "Failed to initialize OTA updater: %s", esp_err_to_name(err));
    }

    // Bring up Wi-Fi manager - it initializes the TCP/IP stack
    // Note: WiFi is started AFTER BLE to avoid coexistence initialization conflicts
    // BLE was initialized earlier (line 497) to ensure it's ready for onboarding
    // (no credentials is acceptable – BLE onboarding will handle it)
    err = wifi_manager_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi manager start failed (%s)", esp_err_to_name(err));
    } else {
        // Now that TCP/IP stack is initialized, register IP event handler for mDNS
        esp_event_handler_instance_t ip_event_handler_instance;
        esp_err_t ip_handler_err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                            ip_event_handler, NULL, &ip_event_handler_instance);
        if (ip_handler_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ip_handler_err));
        } else {
            ESP_LOGI(TAG, "IP event handler registered for mDNS initialization");
        }

        // Start HTTP webserver for debugging and control (TCP/IP stack is now initialized)
        webserver_config_t webserver_cfg = {
            .led_handle = scene,
            .rule_store = store,
            .device_state = NULL,  // device_state is a singleton, no handle needed
            .ota_updater = s_ota_updater,
            .port = 80,
        };
        err = webserver_start(&s_webserver, &webserver_cfg);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP webserver started on port %d", webserver_cfg.port);
            ESP_LOGI(TAG, "Access dashboard at http://nap.local/ (after WiFi connects)");
        } else {
            ESP_LOGW(TAG, "Failed to start HTTP webserver: %s", esp_err_to_name(err));
        }

        esp_err_t wait_err = wifi_manager_wait_for_connection(pdMS_TO_TICKS(10000));
        if (wait_err == ESP_OK) {
            wifi_ap_record_t ap_info = {0};
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                handle_wifi_connected((const char *)ap_info.ssid);
            } else {
                handle_wifi_connected(NULL);
            }
            // mDNS will be initialized by IP event handler when IP is obtained
        } else {
            ESP_LOGW(TAG, "Initial Wi-Fi connection not established (%s)", esp_err_to_name(wait_err));
            handle_wifi_disconnected();
        }
    }
    
    // If WiFi is already connected and we have an IP, initialize mDNS now
    // (IP event might have fired before handler was registered)
    if (wifi_manager_is_connected()) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                ESP_LOGI(TAG, "WiFi already connected with IP, initializing mDNS");
                init_mdns();
            }
        }
    }
    
    // Update status display with BLE initialized state (BLE was initialized earlier)
    if (s_display && ble_running) {
        bool ble_advertising = somnus_ble_is_advertising();
        bool ble_connected = somnus_ble_is_connected();
        status_display_update(&s_status_display,
                            s_wifi_connected,
                            s_aws_started,
                            ble_running,      // ble_initialized
                            ble_advertising, // ble_advertising
                            ble_connected,   // ble_connected
                            s_spotify_ready,
                            s_gemini_ready,
                            true,            // system_ok
                            false,           // listening
                            false,           // vad_active
                            false);          // speaking
    }

    // Attempt to start cloud services if Wi-Fi came up via static config
    maybe_start_cloud_services();
    
    // Create periodic task to update status display (for flashing animations)
    // Increased stack size to 4096 to prevent overflow during icon drawing
    if (s_display) {
        xTaskCreate(status_display_update_task,
                   "status_update",
                   4096,  // Increased from 2048 to prevent stack overflow
                   NULL,
                   5,
                   NULL);
    }

    // Defer I2C scanning to reduce initial stack usage - do it in a separate task
    // For now, skip I2C scanning during boot to prevent stack overflow
    ESP_LOGI(TAG, "I2C scanning deferred to reduce stack usage");
    // I2C scanning code commented out to reduce stack usage:
    // - bus_list array allocation
    // - cJSON object creation
    // - JSON string printing

    log_device_snapshot();
    somnus_ble_notify("Atom Echo rules demo ready");
    ESP_LOGI(TAG, "Atom Echo rules demo ready");
    
    // Defer sensor reading to reduce stack usage during boot
    // Will be done in a separate task after boot completes
    ESP_LOGI(TAG, "Sensor reading deferred to reduce stack usage");
}

// IP event handler to initialize mDNS when IP is obtained
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    (void)arg;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        // Initialize mDNS now that we have an IP
        init_mdns();
    }
}

// Periodic task to update status display (for flashing animations)
static void status_display_update_task(void *pvParameters)
{
    (void)pvParameters;
    
    // Update every 100ms for smooth flashing animation
    const TickType_t update_interval = pdMS_TO_TICKS(100);
    
    while (1) {
        if (s_display) {
            bool ble_running = somnus_ble_is_running();
            bool ble_advertising = somnus_ble_is_advertising();
            bool ble_connected = somnus_ble_is_connected();
            
            // Get listening/speaking/VAD state from status structure (updated by Gemini pipeline via status_display_set_*)
            status_display_update(&s_status_display,
                                s_wifi_connected,
                                s_aws_started,
                                ble_running,      // ble_initialized
                                ble_advertising, // ble_advertising
                                ble_connected,   // ble_connected
                                s_spotify_ready,
                                s_gemini_ready,
                                true,            // system_ok
                                s_status_display.listening,  // listening (from Gemini pipeline)
                                s_status_display.vad_active, // vad_active (from Gemini pipeline VAD)
                                s_status_display.speaking);  // speaking (from Gemini TTS/audio playback)
        }
        vTaskDelay(update_interval);
    }
    // Note: This task runs indefinitely - the while(1) loop above never exits
}
