#include "device_state.h"
#include "led_controller.h"
#include "sensor_manager.h"
#include "sensor_integration.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "cJSON.h"
#include "esp_log.h"
#include <inttypes.h>
#include <string.h>

#ifdef KVA_HAVE_CSPOT
#include "spotify_player.h"
#endif

static const char *TAG = "device_state";

// Forward declarations for global state (defined in naphome_voice_assistant_main.c)
// Access global state directly via extern declarations
extern led_controller_t *s_led_controller_handle;
extern bool s_lights_enabled;
extern bool s_aws_connected;
extern bool s_muted;
extern bool s_audio_playing;

// Context structure for caching (but we'll read from globals when needed)
typedef struct {
    led_controller_t *led_handle;
    bool lights_enabled;
    bool aws_connected;
    bool muted;
    bool audio_playing;
} device_state_context_t;

static device_state_context_t s_ctx = {0};

void device_state_set_context(void *led_handle, bool lights_enabled, bool aws_connected, bool muted, bool audio_playing)
{
    // Cache initial values, but we'll read from globals when generating state
    s_ctx.led_handle = (led_controller_t *)led_handle;
    s_ctx.lights_enabled = lights_enabled;
    s_ctx.aws_connected = aws_connected;
    s_ctx.muted = muted;
    s_ctx.audio_playing = audio_playing;
    ESP_LOGI(TAG, "Device state context initialized: LEDs=%s, AWS=%s, Audio=%s, Muted=%s",
             lights_enabled ? "ON" : "OFF",
             aws_connected ? "connected" : "disconnected",
             audio_playing ? "playing" : "idle",
             muted ? "yes" : "no");
}

// Get sensor data from sensor_manager
static void add_sensor_data(cJSON *root)
{
    cJSON *sensors = cJSON_CreateObject();
    
    // Get actual sensor data
    sensor_integration_data_t sensor_data = sensor_integration_get_data();
    
    cJSON_AddBoolToObject(sensors, "sht45_available", sensor_data.sht45_available);
    cJSON_AddBoolToObject(sensors, "sgp40_available", sensor_data.sgp40_available);
    cJSON_AddBoolToObject(sensors, "scd40_available", sensor_data.scd40_available);
    cJSON_AddBoolToObject(sensors, "vcnl4040_available", sensor_data.vcnl4040_available);
    cJSON_AddBoolToObject(sensors, "ec10_available", sensor_data.ec10_available);
    
    if (sensor_data.sht45_available) {
        cJSON_AddNumberToObject(sensors, "temperature_c", sensor_data.temperature_c);
        cJSON_AddNumberToObject(sensors, "humidity_rh", sensor_data.humidity_rh);
    } else {
        cJSON_AddNumberToObject(sensors, "temperature_c", 0.0);
        cJSON_AddNumberToObject(sensors, "humidity_rh", 0.0);
    }
    
    if (sensor_data.sgp40_available) {
        cJSON_AddNumberToObject(sensors, "voc_index", sensor_data.voc_index);
    } else {
        cJSON_AddNumberToObject(sensors, "voc_index", 0);
    }
    
    if (sensor_data.scd40_available) {
        cJSON_AddNumberToObject(sensors, "co2_ppm", sensor_data.co2_ppm);
        cJSON_AddNumberToObject(sensors, "temperature_co2_c", sensor_data.temperature_co2_c);
        cJSON_AddNumberToObject(sensors, "humidity_co2_rh", sensor_data.humidity_co2_rh);
    } else {
        cJSON_AddNumberToObject(sensors, "co2_ppm", 0);
        cJSON_AddNumberToObject(sensors, "temperature_co2_c", 0.0);
        cJSON_AddNumberToObject(sensors, "humidity_co2_rh", 0.0);
    }
    
    if (sensor_data.vcnl4040_available) {
        cJSON_AddNumberToObject(sensors, "ambient_lux", sensor_data.ambient_lux);
        cJSON_AddNumberToObject(sensors, "proximity", sensor_data.proximity);
    } else {
        cJSON_AddNumberToObject(sensors, "ambient_lux", 0.0);
        cJSON_AddNumberToObject(sensors, "proximity", 0);
    }
    
    if (sensor_data.ec10_available) {
        // EC10 stores PM2.5 in ec_ms_per_cm field (as per sensor_integration.h comment)
        cJSON_AddNumberToObject(sensors, "pm2_5_ug_m3", sensor_data.ec_ms_per_cm);
        cJSON_AddNumberToObject(sensors, "pm10_ug_m3", 0.0);  // PM10 not available from EC10
    } else {
        cJSON_AddNumberToObject(sensors, "pm2_5_ug_m3", 0.0);
        cJSON_AddNumberToObject(sensors, "pm10_ug_m3", 0.0);
    }
    
    cJSON_AddItemToObject(root, "sensors", sensors);
}

char *device_state_to_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON root");
        return NULL;
    }
    
    // Device info
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", "Naphome Voice Assistant");
    cJSON_AddStringToObject(device, "type", "ESP32-S3");
    cJSON_AddNumberToObject(device, "free_heap_bytes", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(device, "min_free_heap_bytes", (double)esp_get_minimum_free_heap_size());
    cJSON_AddItemToObject(root, "device", device);
    
    // WiFi status
    cJSON *wifi = cJSON_CreateObject();
    wifi_ap_record_t ap_info;
    esp_err_t wifi_ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (wifi_ret == ESP_OK) {
        cJSON_AddBoolToObject(wifi, "connected", true);
        cJSON_AddStringToObject(wifi, "ssid", (const char *)ap_info.ssid);
        cJSON_AddNumberToObject(wifi, "rssi", ap_info.rssi);
    } else {
        cJSON_AddBoolToObject(wifi, "connected", false);
        cJSON_AddStringToObject(wifi, "ssid", "");
        cJSON_AddNumberToObject(wifi, "rssi", 0);
    }
    cJSON_AddItemToObject(root, "wifi", wifi);
    
    // LED status - read from global state
    cJSON *leds = cJSON_CreateObject();
    led_controller_t *led_handle = s_led_controller_handle ? s_led_controller_handle : s_ctx.led_handle;
    if (led_handle) {
        cJSON_AddBoolToObject(leds, "enabled", s_lights_enabled);
        cJSON_AddNumberToObject(leds, "count", led_handle->led_count);
        cJSON_AddNumberToObject(leds, "brightness", led_handle->brightness);
        const char *state_str = "idle";
        switch (led_handle->state) {
            case LED_CONTROLLER_STATE_IDLE: state_str = "idle"; break;
            case LED_CONTROLLER_STATE_LISTENING: state_str = "listening"; break;
            case LED_CONTROLLER_STATE_THINKING: state_str = "thinking"; break;
            case LED_CONTROLLER_STATE_SPEAKING: state_str = "speaking"; break;
            case LED_CONTROLLER_STATE_ERROR: state_str = "error"; break;
        }
        cJSON_AddStringToObject(leds, "state", state_str);
    } else {
        cJSON_AddBoolToObject(leds, "enabled", false);
    }
    cJSON_AddItemToObject(root, "leds", leds);
    
    // Audio status - read from global state
    cJSON *audio = cJSON_CreateObject();
    cJSON_AddBoolToObject(audio, "playing", s_audio_playing);
    cJSON_AddBoolToObject(audio, "muted", s_muted);
    cJSON_AddItemToObject(root, "audio", audio);
    
    // AWS IoT status - read from global state
    cJSON *aws = cJSON_CreateObject();
    cJSON_AddBoolToObject(aws, "connected", s_aws_connected);
    cJSON_AddItemToObject(root, "aws", aws);
    
    // Spotify status
    cJSON *spotify = cJSON_CreateObject();
#ifdef KVA_HAVE_CSPOT
    cJSON_AddBoolToObject(spotify, "cspot_enabled", true);
    extern bool spotify_player_is_ready(void);
    cJSON_AddBoolToObject(spotify, "ready", spotify_player_is_ready());
#else
    cJSON_AddBoolToObject(spotify, "cspot_enabled", false);
    cJSON_AddBoolToObject(spotify, "ready", false);
#endif
    cJSON_AddItemToObject(root, "spotify", spotify);
    
    // Sensors
    add_sensor_data(root);
    
    // System health
    cJSON *health = cJSON_CreateObject();
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free = esp_get_minimum_free_heap_size();
    const char *health_status = (free_heap > 50000 && min_free > 10000) ? "healthy" : "low_memory";
    cJSON_AddStringToObject(health, "status", health_status);
    cJSON_AddNumberToObject(health, "free_heap_bytes", free_heap);
    cJSON_AddNumberToObject(health, "min_free_heap_bytes", min_free);
    cJSON_AddNumberToObject(health, "free_heap_percent", (free_heap * 100.0) / (512 * 1024));  // Approximate
    
    // Count active sensors
    sensor_integration_data_t sensor_data = sensor_integration_get_data();
    int sensor_count = 0;
    if (sensor_data.sht45_available) sensor_count++;
    if (sensor_data.sgp40_available) sensor_count++;
    if (sensor_data.scd40_available) sensor_count++;
    if (sensor_data.vcnl4040_available) sensor_count++;
    if (sensor_data.ec10_available) sensor_count++;
    cJSON_AddNumberToObject(health, "sensors_active", sensor_count);
    
    cJSON_AddItemToObject(root, "health", health);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

esp_err_t gemini_execute_function_call(const char *function_name, const char *arguments_json, char *response_text, size_t response_len)
{
    ESP_LOGI(TAG, "🔧 [Gemini Tools] Executing function: %s", function_name);
    ESP_LOGD(TAG, "🔧 [Gemini Tools] Arguments: %s", arguments_json);
    
    cJSON *args = cJSON_Parse(arguments_json);
    if (!args) {
        snprintf(response_text, response_len, "{\"error\": \"Failed to parse arguments\"}");
        return ESP_FAIL;
    }
    
    esp_err_t ret = ESP_OK;
    
    if (strcmp(function_name, "get_device_state") == 0) {
        char *state_json = device_state_to_json();
        if (state_json) {
            snprintf(response_text, response_len, "%s", state_json);
            free(state_json);
        } else {
            snprintf(response_text, response_len, "{\"error\": \"Failed to generate device state\"}");
            ret = ESP_FAIL;
        }
    }
    else if (strcmp(function_name, "get_health") == 0) {
        uint32_t free_heap = esp_get_free_heap_size();
        uint32_t min_free = esp_get_minimum_free_heap_size();
        const char *status = (free_heap > 50000 && min_free > 10000) ? "healthy" : "low_memory";
        
        // Get sensor status
        sensor_integration_data_t sensor_data = sensor_integration_get_data();
        int sensor_count = 0;
        if (sensor_data.sht45_available) sensor_count++;
        if (sensor_data.sgp40_available) sensor_count++;
        if (sensor_data.scd40_available) sensor_count++;
        if (sensor_data.vcnl4040_available) sensor_count++;
        if (sensor_data.ec10_available) sensor_count++;
        
        snprintf(response_text, response_len, 
                "{\"status\": \"%s\", \"free_heap_bytes\": %" PRIu32 ", \"min_free_heap_bytes\": %" PRIu32 ", \"free_heap_percent\": %.1f, "
                "\"wifi_connected\": %s, \"aws_connected\": %s, \"spotify_ready\": %s, \"sensors_active\": %d}",
                status, free_heap, min_free, (free_heap * 100.0) / (512 * 1024),
                "unknown",  // WiFi check would need separate call
                s_aws_connected ? "true" : "false",
#ifdef KVA_HAVE_CSPOT
                spotify_player_is_ready() ? "true" : "false",
#else
                "false",
#endif
                sensor_count);
    }
    else if (strcmp(function_name, "get_temperature") == 0) {
        sensor_integration_data_t sensor_data = sensor_integration_get_data();
        if (sensor_data.sht45_available) {
            snprintf(response_text, response_len, "{\"temperature_c\": %.2f, \"humidity_rh\": %.2f, \"source\": \"SHT45\"}",
                     sensor_data.temperature_c, sensor_data.humidity_rh);
        } else if (sensor_data.scd40_available) {
            snprintf(response_text, response_len, "{\"temperature_c\": %.2f, \"humidity_rh\": %.2f, \"source\": \"SCD40\"}",
                     sensor_data.temperature_co2_c, sensor_data.humidity_co2_rh);
        } else {
            snprintf(response_text, response_len, "{\"temperature_c\": 0.0, \"note\": \"No temperature sensor available\"}");
        }
    }
    else if (strcmp(function_name, "get_sensors") == 0) {
        sensor_integration_data_t sensor_data = sensor_integration_get_data();
        cJSON *sensors = cJSON_CreateObject();
        
        cJSON_AddBoolToObject(sensors, "sht45_available", sensor_data.sht45_available);
        cJSON_AddBoolToObject(sensors, "sgp40_available", sensor_data.sgp40_available);
        cJSON_AddBoolToObject(sensors, "scd40_available", sensor_data.scd40_available);
        cJSON_AddBoolToObject(sensors, "vcnl4040_available", sensor_data.vcnl4040_available);
        cJSON_AddBoolToObject(sensors, "ec10_available", sensor_data.ec10_available);
        
        if (sensor_data.sht45_available) {
            cJSON_AddNumberToObject(sensors, "temperature_c", sensor_data.temperature_c);
            cJSON_AddNumberToObject(sensors, "humidity_rh", sensor_data.humidity_rh);
        }
        if (sensor_data.sgp40_available) {
            cJSON_AddNumberToObject(sensors, "voc_index", sensor_data.voc_index);
        }
        if (sensor_data.scd40_available) {
            cJSON_AddNumberToObject(sensors, "co2_ppm", sensor_data.co2_ppm);
            cJSON_AddNumberToObject(sensors, "temperature_co2_c", sensor_data.temperature_co2_c);
            cJSON_AddNumberToObject(sensors, "humidity_co2_rh", sensor_data.humidity_co2_rh);
        }
        if (sensor_data.vcnl4040_available) {
            cJSON_AddNumberToObject(sensors, "ambient_lux", sensor_data.ambient_lux);
            cJSON_AddNumberToObject(sensors, "proximity", sensor_data.proximity);
        }
        if (sensor_data.ec10_available) {
            // EC10 stores PM2.5 in ec_ms_per_cm field
            cJSON_AddNumberToObject(sensors, "pm2_5_ug_m3", sensor_data.ec_ms_per_cm);
            cJSON_AddNumberToObject(sensors, "pm10_ug_m3", 0.0);
        }
        
        char *sensors_str = cJSON_PrintUnformatted(sensors);
        snprintf(response_text, response_len, "%s", sensors_str ? sensors_str : "{}");
        if (sensors_str) free(sensors_str);
        cJSON_Delete(sensors);
    }
    else if (strcmp(function_name, "set_leds") == 0) {
        cJSON *enabled = cJSON_GetObjectItem(args, "enabled");
        led_controller_t *led_handle = s_led_controller_handle ? s_led_controller_handle : s_ctx.led_handle;
        if (cJSON_IsBool(enabled) && led_handle) {
            bool enable = cJSON_IsTrue(enabled);
            
            // Update global state
            s_lights_enabled = enable;
            s_ctx.lights_enabled = enable;
            
            // Actually control LEDs
            if (enable) {
                led_controller_start_trippy_fade(led_handle);
                ESP_LOGI(TAG, "🔧 [Gemini Tools] set_leds: ON - Started trippy fade");
            } else {
                led_controller_stop_trippy_fade(led_handle);
                for (uint8_t i = 0; i < led_handle->led_count; ++i) {
                    led_controller_set_pixel_color(led_handle, i, 0, 0, 0);
                }
                ESP_LOGI(TAG, "🔧 [Gemini Tools] set_leds: OFF - All LEDs turned off");
            }
            
            snprintf(response_text, response_len, "{\"success\": true, \"message\": \"LEDs turned %s\"}", enable ? "on" : "off");
        } else {
            snprintf(response_text, response_len, "{\"error\": \"Invalid arguments or LEDs not available\"}");
            ret = ESP_FAIL;
        }
    }
    else if (strcmp(function_name, "set_led_color") == 0) {
        cJSON *red = cJSON_GetObjectItem(args, "red");
        cJSON *green = cJSON_GetObjectItem(args, "green");
        cJSON *blue = cJSON_GetObjectItem(args, "blue");
        led_controller_t *led_handle = s_led_controller_handle ? s_led_controller_handle : s_ctx.led_handle;
        if (cJSON_IsNumber(red) && cJSON_IsNumber(green) && cJSON_IsNumber(blue) && led_handle) {
            uint8_t r = (uint8_t)cJSON_GetNumberValue(red);
            uint8_t g = (uint8_t)cJSON_GetNumberValue(green);
            uint8_t b = (uint8_t)cJSON_GetNumberValue(blue);
            
            // Stop trippy fade and set solid color
            led_controller_stop_trippy_fade(led_handle);
            for (uint8_t i = 0; i < led_handle->led_count; ++i) {
                led_controller_set_pixel_color(led_handle, i, r, g, b);
            }
            s_lights_enabled = true;  // Ensure lights are on
            s_ctx.lights_enabled = true;
            
            ESP_LOGI(TAG, "🔧 [Gemini Tools] set_led_color: RGB(%d, %d, %d) - Applied to all LEDs", r, g, b);
            snprintf(response_text, response_len, "{\"success\": true, \"message\": \"LED color set to RGB(%d, %d, %d)\"}", r, g, b);
        } else {
            snprintf(response_text, response_len, "{\"error\": \"Invalid arguments or LEDs not available\"}");
            ret = ESP_FAIL;
        }
    }
    else if (strcmp(function_name, "set_audio_mute") == 0) {
        cJSON *muted = cJSON_GetObjectItem(args, "muted");
        if (cJSON_IsBool(muted)) {
            bool mute_val = cJSON_IsTrue(muted);
            s_muted = mute_val;
            s_ctx.muted = mute_val;
            ESP_LOGI(TAG, "🔧 [Gemini Tools] set_audio_mute: %s", mute_val ? "muted" : "unmuted");
            snprintf(response_text, response_len, "{\"success\": true, \"message\": \"Audio %s\"}", mute_val ? "muted" : "unmuted");
        } else {
            snprintf(response_text, response_len, "{\"error\": \"Invalid arguments\"}");
            ret = ESP_FAIL;
        }
    }
    else {
        snprintf(response_text, response_len, "{\"error\": \"Unknown function: %s\"}", function_name);
        ret = ESP_ERR_NOT_FOUND;
    }
    
    cJSON_Delete(args);
    ESP_LOGI(TAG, "🔧 [Gemini Tools] Function result: %s", response_text);
    return ret;
}
