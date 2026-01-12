/**
 * @file naptick_http.c
 * @brief Naptick HTTP client implementation
 */

#include "naptick_http.h"
#include "naptick_config.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "naptick_http";

#ifndef CONFIG_NAPTICK_HTTP_REGISTRATION_URL
#define CONFIG_NAPTICK_HTTP_REGISTRATION_URL "https://api-uat.naptick.com/user/user-service/app-registration"
#endif

#ifndef CONFIG_NAPTICK_HTTP_SENSOR_UPLOAD_URL
#define CONFIG_NAPTICK_HTTP_SENSOR_UPLOAD_URL "https://api-uat.naptick.com/sensor-service/sensor-service/stream"
#endif

#ifndef CONFIG_NAPTICK_HTTP_TIMEOUT_MS
#define CONFIG_NAPTICK_HTTP_TIMEOUT_MS 30000
#endif

static char s_response_buffer[1024];
static int s_response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                int copy_len = evt->data_len;
                if (s_response_len + copy_len < sizeof(s_response_buffer) - 1) {
                    memcpy(s_response_buffer + s_response_len, evt->data, copy_len);
                    s_response_len += copy_len;
                    s_response_buffer[s_response_len] = '\0';
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t naptick_http_register_device(void)
{
    naptick_system_t *sys = naptick_config_get_system();

    if (strlen(sys->config.token) == 0) {
        ESP_LOGE(TAG, "Token not set, cannot register device");
        return ESP_ERR_INVALID_STATE;
    }

    const char *device_id = naptick_config_get_device_id();

    /* Build JSON body */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "deviceId", device_id);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body) {
        ESP_LOGE(TAG, "Failed to create JSON body");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Registering device: %s", device_id);
    ESP_LOGI(TAG, "URL: %s", CONFIG_NAPTICK_HTTP_REGISTRATION_URL);

    /* Build authorization header */
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", sys->config.token);

    /* Configure HTTP client */
    esp_http_client_config_t config = {
        .url = CONFIG_NAPTICK_HTTP_REGISTRATION_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CONFIG_NAPTICK_HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(body);
        return ESP_FAIL;
    }

    /* Set headers */
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_header(client, "Authorization", auth_header);

    /* Set body */
    esp_http_client_set_post_field(client, body, strlen(body));

    /* Clear response buffer */
    s_response_len = 0;
    memset(s_response_buffer, 0, sizeof(s_response_buffer));

    /* Perform request */
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    free(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Registration response: status=%d", status_code);

    if (status_code == 200 || status_code == 201) {
        ESP_LOGI(TAG, "Device registration successful");
        return ESP_OK;
    } else if (status_code == 401) {
        ESP_LOGE(TAG, "Token invalid or expired (401), triggering factory reset");
        naptick_config_reset();
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Registration failed with status %d", status_code);
        if (s_response_len > 0) {
            ESP_LOGE(TAG, "Response: %s", s_response_buffer);
        }
        return ESP_FAIL;
    }
}

esp_err_t naptick_http_upload_sensors(const char *json_payload)
{
    if (!json_payload) {
        return ESP_ERR_INVALID_ARG;
    }

    naptick_system_t *sys = naptick_config_get_system();

    if (strlen(sys->config.token) == 0) {
        ESP_LOGE(TAG, "Token not set, cannot upload sensors");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Uploading sensor data...");

    /* Build authorization header */
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", sys->config.token);

    /* Configure HTTP client */
    esp_http_client_config_t config = {
        .url = CONFIG_NAPTICK_HTTP_SENSOR_UPLOAD_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CONFIG_NAPTICK_HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    /* Set headers */
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_header(client, "Authorization", auth_header);

    /* Set body */
    esp_http_client_set_post_field(client, json_payload, strlen(json_payload));

    /* Clear response buffer */
    s_response_len = 0;
    memset(s_response_buffer, 0, sizeof(s_response_buffer));

    /* Perform request */
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    if (status_code == 200 || status_code == 201) {
        ESP_LOGI(TAG, "Sensor upload successful");
        return ESP_OK;
    } else if (status_code == 401) {
        ESP_LOGE(TAG, "Token invalid (401), triggering factory reset");
        naptick_config_reset();
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Sensor upload failed with status %d", status_code);
        return ESP_FAIL;
    }
}

esp_err_t naptick_http_build_sensor_payload(
    float temperature,
    float humidity,
    float co2,
    float voc_index,
    float pm25,
    int light_lux,
    int noise_db,
    bool motion,
    char *out,
    size_t out_len)
{
    if (!out || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *device_id = naptick_config_get_device_id();

    /* Get current timestamp in ISO8601 format */
    time_t now;
    struct tm timeinfo;
    char timestamp[30];
    time(&now);
    gmtime_r(&now, &timeinfo);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

    /* Build JSON */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "deviceId", device_id);
    cJSON_AddStringToObject(root, "timestamp", timestamp);

    cJSON *sensors = cJSON_AddObjectToObject(root, "sensors");
    cJSON_AddNumberToObject(sensors, "temperature", temperature);
    cJSON_AddNumberToObject(sensors, "humidity", humidity);
    cJSON_AddNumberToObject(sensors, "co2", co2);
    cJSON_AddNumberToObject(sensors, "vocIndex", voc_index);
    cJSON_AddNumberToObject(sensors, "pm25", pm25);
    cJSON_AddNumberToObject(sensors, "ambientLightLux", light_lux);
    cJSON_AddNumberToObject(sensors, "noiseDb", noise_db);
    cJSON_AddBoolToObject(sensors, "motionDetected", motion);

    cJSON *device = cJSON_AddObjectToObject(root, "device");
    cJSON_AddStringToObject(device, "powerSource", "plugged");
    cJSON_AddStringToObject(device, "firmwareVersion", "1.0.0");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }

    size_t json_len = strlen(json_str);
    if (json_len >= out_len) {
        free(json_str);
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(out, json_str);
    free(json_str);

    return ESP_OK;
}
