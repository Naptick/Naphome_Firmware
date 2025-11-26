#include "aws_iot_bridge.h"

#include "aws_iot_bridge.h"

#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "somnus_mqtt.h"

static const char *TAG = "aws_iot_bridge";

static void aws_iot_bridge_metrics_init(aws_iot_bridge_metrics_t *metrics)
{
    if (!metrics) {
        return;
    }
    memset(metrics, 0, sizeof(*metrics));
    metrics->last_button_id = -1;
}

static bool aws_iot_bridge_lock(aws_iot_bridge_t *bridge)
{
    if (!bridge || !bridge->metrics_mutex) {
        return false;
    }
    if (xSemaphoreTake(bridge->metrics_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    return true;
}

static void aws_iot_bridge_unlock(aws_iot_bridge_t *bridge)
{
    if (bridge && bridge->metrics_mutex) {
        xSemaphoreGive(bridge->metrics_mutex);
    }
}

static bool aws_iot_bridge_snapshot_metrics(const aws_iot_bridge_t *bridge, aws_iot_bridge_metrics_t *out)
{
    if (!bridge || !out || !bridge->metrics_mutex) {
        return false;
    }
    if (xSemaphoreTake(bridge->metrics_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }
    *out = bridge->metrics;
    xSemaphoreGive(bridge->metrics_mutex);
    return true;
}

static void aws_iot_bridge_publish_metrics(aws_iot_bridge_t *bridge)
{
    aws_iot_bridge_metrics_t snapshot = {0};
    if (!aws_iot_bridge_snapshot_metrics(bridge, &snapshot)) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    const char *device_id = somnus_mqtt_get_device_id();
    if (device_id) {
        cJSON_AddStringToObject(root, "deviceId", device_id);
    }
    uint64_t timestamp_ms = esp_timer_get_time() / 1000ULL;
    cJSON_AddNumberToObject(root, "timestamp_ms", (double)timestamp_ms);
    cJSON *metrics = cJSON_CreateObject();
    if (!metrics) {
        cJSON_Delete(root);
        return;
    }
    cJSON_AddItemToObject(root, "assistant_metrics", metrics);
    cJSON_AddNumberToObject(metrics, "wake_events", snapshot.wake_events);
    cJSON_AddNumberToObject(metrics, "simulated_wake_events", snapshot.simulated_wake_events);
    cJSON_AddNumberToObject(metrics, "button_events", snapshot.button_events);
    cJSON_AddNumberToObject(metrics, "stt_success", snapshot.stt_success);
    cJSON_AddNumberToObject(metrics, "stt_failure", snapshot.stt_failure);
    cJSON_AddNumberToObject(metrics, "tts_success", snapshot.tts_success);
    cJSON_AddNumberToObject(metrics, "tts_failure", snapshot.tts_failure);
    cJSON_AddNumberToObject(metrics, "spotify_success", snapshot.spotify_success);
    cJSON_AddNumberToObject(metrics, "spotify_failure", snapshot.spotify_failure);
    cJSON_AddNumberToObject(metrics, "interactions", snapshot.interactions);
    cJSON_AddNumberToObject(metrics, "interaction_errors", snapshot.interaction_errors);
    if (snapshot.last_button_id >= 0) {
        cJSON_AddNumberToObject(metrics, "last_button_id", snapshot.last_button_id);
    }
    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        esp_err_t err = somnus_mqtt_publish_telemetry(json);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Telemetry publish failed (%s); logging locally", esp_err_to_name(err));
            ESP_LOGI(TAG, "[metrics] %s", json);
        } else {
            ESP_LOGI(TAG, "Published assistant metrics snapshot");
        }
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

static void telemetry_timer_cb(TimerHandle_t timer)
{
    aws_iot_bridge_t *bridge = (aws_iot_bridge_t *)pvTimerGetTimerID(timer);
    if (!bridge) {
        return;
    }
    aws_iot_bridge_publish_metrics(bridge);
}

esp_err_t aws_iot_bridge_init(aws_iot_bridge_t *bridge, const aws_iot_bridge_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(bridge && cfg, ESP_ERR_INVALID_ARG, TAG, "bad args");
    aws_iot_bridge_metrics_init(&bridge->metrics);
    bridge->metrics_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(bridge->metrics_mutex, ESP_ERR_NO_MEM, TAG, "metrics mutex");
    bridge->telemetry_period_ms = cfg->telemetry_period_ms;
    if (bridge->telemetry_period_ms > 0) {
        bridge->telemetry_timer = xTimerCreate("aws_telemetry",
                                               pdMS_TO_TICKS(bridge->telemetry_period_ms),
                                               pdTRUE,
                                               bridge,
                                               telemetry_timer_cb);
        if (!bridge->telemetry_timer) {
            vSemaphoreDelete(bridge->metrics_mutex);
            bridge->metrics_mutex = NULL;
            return ESP_ERR_NO_MEM;
        }
        xTimerStart(bridge->telemetry_timer, 0);
    }
    return ESP_OK;
}

static void aws_iot_bridge_record_interaction_locked(aws_iot_bridge_t *bridge, esp_err_t status)
{
    bridge->metrics.interactions++;
    if (status != ESP_OK) {
        bridge->metrics.interaction_errors++;
    }
}

static void aws_iot_bridge_record_result_locked(aws_iot_bridge_metrics_t *metrics,
                                                bool success,
                                                uint32_t *success_counter,
                                                uint32_t *failure_counter)
{
    if (success) {
        (*success_counter)++;
    } else {
        (*failure_counter)++;
    }
}

esp_err_t aws_iot_bridge_publish_interaction(aws_iot_bridge_t *bridge,
                                             const char *transcript,
                                             const intent_router_decision_t *decision,
                                             esp_err_t action_status)
{
    ESP_RETURN_ON_FALSE(bridge, ESP_ERR_INVALID_ARG, TAG, "bridge required");
    const char *intent = "none";
    if (decision) {
        switch (decision->action) {
            case INTENT_ROUTER_ACTION_SPOTIFY_PLAY:
                intent = "spotify_play";
                break;
            case INTENT_ROUTER_ACTION_SPOTIFY_PAUSE:
                intent = "spotify_pause";
                break;
            case INTENT_ROUTER_ACTION_SPOTIFY_RESUME:
                intent = "spotify_resume";
                break;
            case INTENT_ROUTER_ACTION_SPOTIFY_VOLUME_DELTA:
                intent = "spotify_volume_delta";
                break;
            default:
                break;
        }
    }
    ESP_LOGI(TAG,
             "[stub] Would publish interaction: transcript=\"%s\" intent=%s status=%s",
             transcript ? transcript : "(none)",
             intent,
             action_status == ESP_OK ? "ok" : esp_err_to_name(action_status));
    aws_iot_bridge_record_interaction(bridge, action_status);
    return ESP_OK;
}

void aws_iot_bridge_record_wake(aws_iot_bridge_t *bridge, bool simulated)
{
    if (!aws_iot_bridge_lock(bridge)) {
        return;
    }
    if (simulated) {
        bridge->metrics.simulated_wake_events++;
    } else {
        bridge->metrics.wake_events++;
    }
    aws_iot_bridge_unlock(bridge);
}

void aws_iot_bridge_record_button(aws_iot_bridge_t *bridge, int button_id)
{
    if (!aws_iot_bridge_lock(bridge)) {
        return;
    }
    bridge->metrics.button_events++;
    bridge->metrics.last_button_id = button_id;
    aws_iot_bridge_unlock(bridge);
}

void aws_iot_bridge_record_stt_result(aws_iot_bridge_t *bridge, esp_err_t status)
{
    if (!aws_iot_bridge_lock(bridge)) {
        return;
    }
    aws_iot_bridge_record_result_locked(&bridge->metrics,
                                        status == ESP_OK,
                                        &bridge->metrics.stt_success,
                                        &bridge->metrics.stt_failure);
    aws_iot_bridge_unlock(bridge);
}

void aws_iot_bridge_record_tts_result(aws_iot_bridge_t *bridge, esp_err_t status)
{
    if (!aws_iot_bridge_lock(bridge)) {
        return;
    }
    aws_iot_bridge_record_result_locked(&bridge->metrics,
                                        status == ESP_OK,
                                        &bridge->metrics.tts_success,
                                        &bridge->metrics.tts_failure);
    aws_iot_bridge_unlock(bridge);
}

void aws_iot_bridge_record_spotify_result(aws_iot_bridge_t *bridge, esp_err_t status)
{
    if (!aws_iot_bridge_lock(bridge)) {
        return;
    }
    aws_iot_bridge_record_result_locked(&bridge->metrics,
                                        status == ESP_OK,
                                        &bridge->metrics.spotify_success,
                                        &bridge->metrics.spotify_failure);
    aws_iot_bridge_unlock(bridge);
}

void aws_iot_bridge_record_interaction(aws_iot_bridge_t *bridge, esp_err_t status)
{
    if (!aws_iot_bridge_lock(bridge)) {
        return;
    }
    aws_iot_bridge_record_interaction_locked(bridge, status);
    aws_iot_bridge_unlock(bridge);
}
