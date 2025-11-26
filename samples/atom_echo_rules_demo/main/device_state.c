#include "device_state.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sensor_integration.h"

static const char *TAG = "device_state";

typedef struct {
    bool wifi_connected;
    char wifi_ssid[33];
    bool aws_connected;
    bool spotify_ready;
    bool gemini_ready;
    char gemini_summary[256];
    char i2c_summary[256];
    void *led_handle;
    bool lights_enabled;
    bool audio_muted;
    bool audio_playing;
} device_state_snapshot_t;

static device_state_snapshot_t s_state;

void device_state_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    ESP_LOGI(TAG, "Device state tracker initialised");
}

void device_state_set_context(void *led_handle,
                              bool lights_enabled,
                              bool aws_connected,
                              bool muted,
                              bool audio_playing)
{
    s_state.led_handle = led_handle;
    s_state.lights_enabled = lights_enabled;
    s_state.aws_connected = aws_connected;
    s_state.audio_muted = muted;
    s_state.audio_playing = audio_playing;
    ESP_LOGI(TAG,
             "Context updated: LEDs=%s AWS=%s Audio=%s Muted=%s",
             lights_enabled ? "ON" : "OFF",
             aws_connected ? "connected" : "disconnected",
             audio_playing ? "playing" : "idle",
             muted ? "yes" : "no");
}

void device_state_set_wifi(bool connected, const char *ssid)
{
    s_state.wifi_connected = connected;
    if (ssid) {
        strlcpy(s_state.wifi_ssid, ssid, sizeof(s_state.wifi_ssid));
    } else if (!connected) {
        s_state.wifi_ssid[0] = '\0';
    }
}

void device_state_set_aws(bool connected)
{
    s_state.aws_connected = connected;
}

void device_state_set_spotify(bool ready)
{
    s_state.spotify_ready = ready;
}

void device_state_set_gemini(bool ready, const char *summary)
{
    s_state.gemini_ready = ready;
    if (summary) {
        strlcpy(s_state.gemini_summary, summary, sizeof(s_state.gemini_summary));
    } else {
        s_state.gemini_summary[0] = '\0';
    }
}

void device_state_set_i2c_summary(const char *summary_json)
{
    if (summary_json) {
        strlcpy(s_state.i2c_summary, summary_json, sizeof(s_state.i2c_summary));
    } else {
        s_state.i2c_summary[0] = '\0';
    }
}

char *device_state_to_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddBoolToObject(root, "wifi_connected", s_state.wifi_connected);
    cJSON_AddStringToObject(root, "wifi_ssid", s_state.wifi_ssid);
    cJSON_AddBoolToObject(root, "aws_connected", s_state.aws_connected);
    cJSON_AddBoolToObject(root, "spotify_ready", s_state.spotify_ready);
    cJSON_AddBoolToObject(root, "gemini_ready", s_state.gemini_ready);
    cJSON_AddStringToObject(root, "gemini_summary", s_state.gemini_summary);
    cJSON_AddNumberToObject(root, "free_heap_bytes", esp_get_free_heap_size());

    cJSON *leds = cJSON_CreateObject();
    cJSON_AddBoolToObject(leds, "enabled", s_state.lights_enabled);
    cJSON_AddItemToObject(root, "leds", leds);

    cJSON *audio = cJSON_CreateObject();
    cJSON_AddBoolToObject(audio, "muted", s_state.audio_muted);
    cJSON_AddBoolToObject(audio, "playing", s_state.audio_playing);
    cJSON_AddItemToObject(root, "audio", audio);

    if (s_state.i2c_summary[0]) {
        cJSON *i2c = cJSON_Parse(s_state.i2c_summary);
        if (i2c) {
            cJSON_AddItemToObject(root, "i2c_scan", i2c);
        } else {
            cJSON_AddStringToObject(root, "i2c_scan_raw", s_state.i2c_summary);
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

esp_err_t gemini_execute_function_call(const char *function_name,
                                       const char *arguments_json,
                                       char *response_text,
                                       size_t response_len)
{
    (void)arguments_json;
    ESP_RETURN_ON_FALSE(function_name && response_text && response_len > 0,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid Gemini function call parameters");

    sensor_integration_data_t sensor_data = sensor_integration_get_data();

    if (strcmp(function_name, "get_device_state") == 0) {
        char *state = device_state_to_json();
        if (!state) {
            return ESP_ERR_NO_MEM;
        }
        strlcpy(response_text, state, response_len);
        free(state);
        return ESP_OK;
    }

    if (strcmp(function_name, "get_sensors") == 0) {
        cJSON *sensors = cJSON_CreateObject();
        if (!sensors) {
            return ESP_ERR_NO_MEM;
        }
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
            cJSON_AddNumberToObject(sensors, "pm2_5_ug_m3", sensor_data.ec_ms_per_cm);
        }
        char *json = cJSON_PrintUnformatted(sensors);
        cJSON_Delete(sensors);
        if (!json) {
            return ESP_ERR_NO_MEM;
        }
        strlcpy(response_text, json, response_len);
        free(json);
        return ESP_OK;
    }

    if (strcmp(function_name, "get_temperature") == 0) {
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            return ESP_ERR_NO_MEM;
        }
        if (sensor_data.sht45_available) {
            cJSON_AddStringToObject(obj, "source", "SHT45");
            cJSON_AddNumberToObject(obj, "temperature_c", sensor_data.temperature_c);
            cJSON_AddNumberToObject(obj, "humidity_rh", sensor_data.humidity_rh);
        } else if (sensor_data.scd40_available) {
            cJSON_AddStringToObject(obj, "source", "SCD40");
            cJSON_AddNumberToObject(obj, "temperature_c", sensor_data.temperature_co2_c);
            cJSON_AddNumberToObject(obj, "humidity_rh", sensor_data.humidity_co2_rh);
        } else {
            cJSON_AddStringToObject(obj, "source", "none");
            cJSON_AddNumberToObject(obj, "temperature_c", 0.0);
        }
        char *json = cJSON_PrintUnformatted(obj);
        cJSON_Delete(obj);
        if (!json) {
            return ESP_ERR_NO_MEM;
        }
        strlcpy(response_text, json, response_len);
        free(json);
        return ESP_OK;
    }

    if (strcmp(function_name, "get_health") == 0) {
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            return ESP_ERR_NO_MEM;
        }
        uint32_t free_heap = esp_get_free_heap_size();
        uint32_t min_free = esp_get_minimum_free_heap_size();
        cJSON_AddNumberToObject(obj, "free_heap_bytes", free_heap);
        cJSON_AddNumberToObject(obj, "min_free_heap_bytes", min_free);
        cJSON_AddNumberToObject(obj, "sensors_present",
                                (sensor_data.sht45_available ? 1 : 0) +
                                (sensor_data.sgp40_available ? 1 : 0) +
                                (sensor_data.scd40_available ? 1 : 0) +
                                (sensor_data.vcnl4040_available ? 1 : 0) +
                                (sensor_data.ec10_available ? 1 : 0));
        cJSON_AddBoolToObject(obj, "aws_connected", s_state.aws_connected);
        cJSON_AddBoolToObject(obj, "spotify_ready", s_state.spotify_ready);
        char *json = cJSON_PrintUnformatted(obj);
        cJSON_Delete(obj);
        if (!json) {
            return ESP_ERR_NO_MEM;
        }
        strlcpy(response_text, json, response_len);
        free(json);
        return ESP_OK;
    }

    snprintf(response_text,
             response_len,
             "{\"error\":\"Unknown function: %s\"}",
             function_name);
    return ESP_ERR_NOT_FOUND;
}
