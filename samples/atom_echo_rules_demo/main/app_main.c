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

#include "cJSON.h"
#include "device_state.h"
#include "display_matrix.h"
#include "status_display.h"
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

void aws_led_set_connected(bool connected);

// Forward declarations
static void maybe_start_cloud_services(void);
static void log_device_snapshot(void);

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
                status_display_update(&s_status_display,
                                    s_wifi_connected,
                                    s_aws_started,
                                    ble_running,
                                    s_spotify_ready,
                                    s_gemini_ready,
                                    true);
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
                status_display_update(&s_status_display,
                                    s_wifi_connected,
                                    s_aws_started,
                                    ble_running,
                                    s_spotify_ready,
                                    s_gemini_ready,
                                    true);
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
    // Early logging to catch crash location
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
            
            // Test with a bright color first to verify display works
            ESP_LOGI(TAG, "Testing display with white fill...");
            display_matrix_fill(display, 0xFFFF); // white background
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            ESP_LOGI(TAG, "Testing display with red fill...");
            display_matrix_fill(display, 0xF800); // red
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            ESP_LOGI(TAG, "Testing display with green fill...");
            display_matrix_fill(display, 0x07E0); // green
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            ESP_LOGI(TAG, "Testing display with blue fill...");
            display_matrix_fill(display, 0x001F); // blue
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            display_matrix_fill(display, 0x0000); // black background
            ESP_LOGI(TAG, "Display filled with black background");
            ESP_LOGI(TAG, "Display initialised as 10x10 tile grid");
            
            // Display naphome.png image
            ESP_LOGI(TAG, "Drawing NaphomeBackground image (%dx%d)...", NAPHOME_WIDTH, NAPHOME_HEIGHT);
            err = display_matrix_draw_bitmap(display, 0, 0, 
                                             NAPHOME_WIDTH, NAPHOME_HEIGHT, 
                                             naphome_data);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Naphome image displayed successfully");
            } else {
                ESP_LOGW(TAG, "Failed to display naphome image (%s)", esp_err_to_name(err));
            }
            
            // Initialize face LED simulator
            ESP_LOGI(TAG, "Initializing face LED simulator...");
            err = face_led_simulator_init(&s_face_led_simulator, display, naphome_face_data);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Face LED simulator initialized");
                // Register with action handler for synchronized updates
                somnus_action_handler_set_face_simulator(&s_face_led_simulator);
            } else {
                ESP_LOGW(TAG, "Face LED simulator init failed (%s)", esp_err_to_name(err));
            }
        } else {
            ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "Display pins not configured (some pins < 0), skipping display init");
    }
    
    // Temporarily disable status display to debug boot loop
    // Initialize status display - wrap in try/catch equivalent
    // err = status_display_init(&s_status_display, display);
    // if (err == ESP_OK) {
    //     ESP_LOGI(TAG, "Status display initialized");
    //     // Initial status update deferred to reduce stack usage
    // } else {
    //     ESP_LOGW(TAG, "Status display init failed (%s)", esp_err_to_name(err));
    //     // Continue even if status display fails
    // }
    ESP_LOGI(TAG, "Status display init temporarily disabled for debugging");

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

    // Bring up Wi-Fi manager (no credentials is acceptable – BLE onboarding will handle it)
    err = wifi_manager_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi manager start failed (%s)", esp_err_to_name(err));
    } else {
        esp_err_t wait_err = wifi_manager_wait_for_connection(pdMS_TO_TICKS(10000));
        if (wait_err == ESP_OK) {
            wifi_ap_record_t ap_info = {0};
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                handle_wifi_connected((const char *)ap_info.ssid);
            } else {
                handle_wifi_connected(NULL);
            }
        } else {
            ESP_LOGW(TAG, "Initial Wi-Fi connection not established (%s)", esp_err_to_name(wait_err));
            handle_wifi_disconnected();
        }
    }

    somnus_ble_config_t ble_cfg = {
        .connect_cb = ble_connect_wifi_cb,
        .connect_ctx = NULL,
    };
    esp_err_t ble_err = somnus_ble_start(&ble_cfg);
    bool ble_running = false;
    if (ble_err == ESP_OK) {
        ble_running = somnus_ble_is_running();
        somnus_ble_notify("Atom Echo ready for BLE onboarding");
        ESP_LOGI(TAG, "BLE service started (advertising: %s)", ble_running ? "yes" : "no");
        
        // Status display update deferred to reduce stack usage during init
    } else {
        ESP_LOGE(TAG, "Failed to start Somnus BLE service (%s)", esp_err_to_name(ble_err));
    }

    // Attempt to start cloud services if Wi-Fi came up via static config
    maybe_start_cloud_services();

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
    
    // Final status display update after all initialization - temporarily disabled
    // if (s_display) {
    //     bool ble_running = somnus_ble_is_running();
    //     status_display_update(&s_status_display,
    //                         s_wifi_connected,
    //                         s_aws_started,
    //                         ble_running,
    //                         s_spotify_ready,
    //                         s_gemini_ready,
    //                         true);
    //     ESP_LOGI(TAG, "Status display updated");
    // }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
