/**
 * @file sensor_manager.c
 */

#include "sensor_manager.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "somnus_mqtt.h"

#define SENSOR_MANAGER_TAG "sensor_manager"

#ifndef CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS
#define CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS 10000
#endif

#define SENSOR_MANAGER_MAX_SENSORS 8
#define SENSOR_MANAGER_TASK_STACK 4096
#define SENSOR_MANAGER_TASK_PRIO 5

typedef struct {
    const char *name;
    sensor_manager_sample_cb_t callback;
} sensor_entry_t;

static sensor_entry_t s_sensors[SENSOR_MANAGER_MAX_SENSORS];
static size_t s_sensor_count;
static uint32_t s_publish_interval_ms = CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS;
static bool s_initialized;
static bool s_should_run;
static bool s_running;
static TaskHandle_t s_task_handle;
static sensor_manager_observer_cb_t s_observer_cb;
static void *s_observer_ctx;

static void sensor_manager_collect_and_publish(void);
static void sensor_manager_task(void *arg);

esp_err_t sensor_manager_init(const sensor_manager_config_t *config)
{
    if (config && config->publish_interval_ms > 0) {
        s_publish_interval_ms = config->publish_interval_ms;
    } else {
        s_publish_interval_ms = CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS;
    }

    s_initialized = true;
    s_observer_cb = NULL;
    s_observer_ctx = NULL;
    return ESP_OK;
}

esp_err_t sensor_manager_register(const sensor_manager_sensor_t *sensor)
{
    if (!s_initialized) {
        sensor_manager_init(NULL);
    }

    ESP_RETURN_ON_FALSE(sensor, ESP_ERR_INVALID_ARG, SENSOR_MANAGER_TAG, "sensor pointer is NULL");
    ESP_RETURN_ON_FALSE(sensor->name && sensor->name[0] != '\0',
                        ESP_ERR_INVALID_ARG,
                        SENSOR_MANAGER_TAG,
                        "sensor name is invalid");
    ESP_RETURN_ON_FALSE(sensor->sample_cb,
                        ESP_ERR_INVALID_ARG,
                        SENSOR_MANAGER_TAG,
                        "sensor callback is NULL");
    ESP_RETURN_ON_FALSE(!s_running,
                        ESP_ERR_INVALID_STATE,
                        SENSOR_MANAGER_TAG,
                        "cannot register sensors while manager is running");
    ESP_RETURN_ON_FALSE(s_sensor_count < SENSOR_MANAGER_MAX_SENSORS,
                        ESP_ERR_NO_MEM,
                        SENSOR_MANAGER_TAG,
                        "sensor registry full");

    s_sensors[s_sensor_count++] = (sensor_entry_t){
        .name = sensor->name,
        .callback = sensor->sample_cb,
    };
    return ESP_OK;
}

esp_err_t sensor_manager_set_observer(sensor_manager_observer_cb_t observer, void *user_ctx)
{
    s_observer_cb = observer;
    s_observer_ctx = user_ctx;
    return ESP_OK;
}

esp_err_t sensor_manager_start(void)
{
    if (!s_initialized) {
        sensor_manager_init(NULL);
    }

    ESP_RETURN_ON_FALSE(s_sensor_count > 0,
                        ESP_ERR_INVALID_STATE,
                        SENSOR_MANAGER_TAG,
                        "no sensors registered");

    if (s_running) {
        return ESP_OK;
    }

    s_should_run = true;
    BaseType_t created = xTaskCreate(sensor_manager_task,
                                     "sensor_manager",
                                     SENSOR_MANAGER_TASK_STACK,
                                     NULL,
                                     SENSOR_MANAGER_TASK_PRIO,
                                     &s_task_handle);
    ESP_RETURN_ON_FALSE(created == pdPASS,
                        ESP_ERR_NO_MEM,
                        SENSOR_MANAGER_TAG,
                        "failed to create sensor manager task");

    return ESP_OK;
}

esp_err_t sensor_manager_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    s_should_run = false;
    while (s_running) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

bool sensor_manager_is_running(void)
{
    return s_running;
}

static void sensor_manager_task(void *arg)
{
    (void)arg;
    const TickType_t delay_ticks = pdMS_TO_TICKS(s_publish_interval_ms);
    TickType_t last_wake = xTaskGetTickCount();
    s_running = true;

    while (s_should_run) {
        sensor_manager_collect_and_publish();
        vTaskDelayUntil(&last_wake, delay_ticks);
    }

    s_running = false;
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static void sensor_manager_collect_and_publish(void)
{
    if (s_sensor_count == 0) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(SENSOR_MANAGER_TAG, "Failed to allocate JSON root");
        return;
    }

    const char *device_id = somnus_mqtt_get_device_id();
    if (device_id) {
        cJSON_AddStringToObject(root, "deviceId", device_id);
    }
    cJSON_AddNumberToObject(root, "timestamp_ms", (double)esp_log_timestamp());

    bool has_data = false;

    for (size_t i = 0; i < s_sensor_count; ++i) {
        const sensor_entry_t *entry = &s_sensors[i];
        cJSON *sensor_obj = cJSON_CreateObject();
        if (!sensor_obj) {
            ESP_LOGW(SENSOR_MANAGER_TAG, "Failed to allocate JSON object for sensor '%s'", entry->name);
            continue;
        }

        bool ok = entry->callback(sensor_obj);
        if (ok && sensor_obj->child) {
            cJSON_AddItemToObject(root, entry->name, sensor_obj);
            has_data = true;
            if (s_observer_cb) {
                s_observer_cb(entry->name, sensor_obj, s_observer_ctx);
            }
        } else {
            cJSON_Delete(sensor_obj);
        }
    }

    if (!has_data) {
        cJSON_Delete(root);
        return;
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!payload) {
        ESP_LOGW(SENSOR_MANAGER_TAG, "Failed to serialise telemetry payload");
        return;
    }

    esp_err_t err = somnus_mqtt_publish_telemetry(payload);
    if (err != ESP_OK) {
        ESP_LOGW(SENSOR_MANAGER_TAG,
                 "Telemetry publish failed (%s)",
                 esp_err_to_name(err));
    }
    free(payload);
}

