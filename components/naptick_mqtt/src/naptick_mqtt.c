/**
 * @file naptick_mqtt.c
 * @brief Naptick MQTT client implementation
 */

#include "naptick_mqtt.h"
#include "naptick_config.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "naptick_mqtt";

#ifndef CONFIG_NAPTICK_MQTT_BROKER_URI
#define CONFIG_NAPTICK_MQTT_BROKER_URI "wss://api-uat.naptick.com:443/mqtt"
#endif

#ifndef CONFIG_NAPTICK_MQTT_KEEPALIVE_SEC
#define CONFIG_NAPTICK_MQTT_KEEPALIVE_SEC 120
#endif

#ifndef CONFIG_NAPTICK_MQTT_RECONNECT_TIMEOUT_MS
#define CONFIG_NAPTICK_MQTT_RECONNECT_TIMEOUT_MS 5000
#endif

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_connected = false;
static naptick_mqtt_message_cb_t s_message_cb = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    naptick_system_t *sys = naptick_config_get_system();
    const char *device_id = naptick_config_get_device_id();

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker");
            s_connected = true;

            /* Subscribe to device topics */
            char command_topic[64];
            char config_topic[64];
            snprintf(command_topic, sizeof(command_topic), "devices/%s/command", device_id);
            snprintf(config_topic, sizeof(config_topic), "devices/%s/config", device_id);

            esp_mqtt_client_subscribe(s_mqtt_client, command_topic, 1);
            esp_mqtt_client_subscribe(s_mqtt_client, config_topic, 1);

            ESP_LOGI(TAG, "Subscribed to: %s", command_topic);
            ESP_LOGI(TAG, "Subscribed to: %s", config_topic);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            s_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Subscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT data received:");
            ESP_LOGI(TAG, "  Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "  Data: %.*s", event->data_len, event->data);

            if (s_message_cb) {
                s_message_cb(event->topic, event->topic_len,
                            event->data, event->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "  Transport error: %s",
                         strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;

        default:
            ESP_LOGD(TAG, "MQTT event: %d", (int)event_id);
            break;
    }
}

esp_err_t naptick_mqtt_start(naptick_mqtt_message_cb_t message_cb)
{
    if (s_mqtt_client) {
        ESP_LOGW(TAG, "MQTT client already started");
        return ESP_OK;
    }

    naptick_system_t *sys = naptick_config_get_system();
    const char *device_id = naptick_config_get_device_id();

    if (strlen(sys->config.token) == 0) {
        ESP_LOGE(TAG, "Token not set, cannot start MQTT");
        return ESP_ERR_INVALID_STATE;
    }

    s_message_cb = message_cb;

    ESP_LOGI(TAG, "Starting MQTT client...");
    ESP_LOGI(TAG, "  Broker: %s", CONFIG_NAPTICK_MQTT_BROKER_URI);
    ESP_LOGI(TAG, "  Username: %s", device_id);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = CONFIG_NAPTICK_MQTT_BROKER_URI,
            },
        },
        .credentials = {
            .username = device_id,
            .authentication = {
                .password = sys->config.token,
            },
        },
        .session = {
            .keepalive = CONFIG_NAPTICK_MQTT_KEEPALIVE_SEC,
        },
        .network = {
            .reconnect_timeout_ms = CONFIG_NAPTICK_MQTT_RECONNECT_TIMEOUT_MS,
            .timeout_ms = 40000,
        },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        return err;
    }

    ESP_LOGI(TAG, "MQTT client started");
    return ESP_OK;
}

esp_err_t naptick_mqtt_stop(void)
{
    if (!s_mqtt_client) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping MQTT client...");

    esp_mqtt_client_stop(s_mqtt_client);
    esp_mqtt_client_destroy(s_mqtt_client);
    s_mqtt_client = NULL;
    s_connected = false;
    s_message_cb = NULL;

    ESP_LOGI(TAG, "MQTT client stopped");
    return ESP_OK;
}

bool naptick_mqtt_is_connected(void)
{
    return s_connected;
}

esp_err_t naptick_mqtt_publish(const char *topic, const char *data, int data_len, int qos)
{
    if (!s_mqtt_client || !s_connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish");
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, data, data_len, qos, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published to %s, msg_id=%d", topic, msg_id);
    return ESP_OK;
}
