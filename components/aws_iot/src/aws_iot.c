/**
 * @file aws_iot.c
 * @brief Implementation of the AWS IoT helper for Naphome.
 */

#include "aws_iot.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "aws_iot";

static void internal_disconnect_handler(AWS_IoT_Client *client, void *data);

static const char *iot_error_to_string(IoT_Error_t error)
{
    switch (error) {
    case SUCCESS: return "SUCCESS";
    case NULL_VALUE_ERROR: return "NULL_VALUE_ERROR";
    case MQTT_MAX_SUBSCRIPTIONS_REACHED_ERROR: return "MQTT_MAX_SUBSCRIPTIONS_REACHED_ERROR";
    case MQTT_CLIENT_NOT_IDLE_ERROR: return "MQTT_CLIENT_NOT_IDLE_ERROR";
    case NETWORK_DISCONNECTED_ERROR: return "NETWORK_DISCONNECTED_ERROR";
    case NETWORK_ATTEMPTING_RECONNECT: return "NETWORK_ATTEMPTING_RECONNECT";
    case NETWORK_MANUALLY_DISCONNECTED: return "NETWORK_MANUALLY_DISCONNECTED";
    case NETWORK_RECONNECT_TIMED_OUT_ERROR: return "NETWORK_RECONNECT_TIMED_OUT_ERROR";
    case NETWORK_X509_ROOT_CRT_PARSE_ERROR: return "NETWORK_X509_ROOT_CRT_PARSE_ERROR";
    case NETWORK_X509_DEVICE_CRT_PARSE_ERROR: return "NETWORK_X509_DEVICE_CRT_PARSE_ERROR";
    case NETWORK_PK_PRIVATE_KEY_PARSE_ERROR: return "NETWORK_PK_PRIVATE_KEY_PARSE_ERROR";
    case NETWORK_SSL_CONNECT_TIMEOUT_ERROR: return "NETWORK_SSL_CONNECT_TIMEOUT_ERROR";
    case NETWORK_SSL_READ_ERROR: return "NETWORK_SSL_READ_ERROR";
    case NETWORK_SSL_WRITE_ERROR: return "NETWORK_SSL_WRITE_ERROR";
    case NETWORK_SSL_NOTHING_TO_READ: return "NETWORK_SSL_NOTHING_TO_READ";
    case MQTT_CONNACK_UNACCEPTABLE_PROTOCOL_VERSION_ERROR: return "MQTT_CONNACK_UNACCEPTABLE_PROTOCOL_VERSION_ERROR";
    case MQTT_CONNACK_IDENTIFIER_REJECTED_ERROR: return "MQTT_CONNACK_IDENTIFIER_REJECTED_ERROR";
    case MQTT_CONNACK_SERVER_UNAVAILABLE_ERROR: return "MQTT_CONNACK_SERVER_UNAVAILABLE_ERROR";
    case MQTT_CONNACK_BAD_USERDATA_ERROR: return "MQTT_CONNACK_BAD_USERDATA_ERROR";
    case MQTT_CONNACK_NOT_AUTHORIZED_ERROR: return "MQTT_CONNACK_NOT_AUTHORIZED_ERROR";
    case MQTT_REQUEST_TIMEOUT_ERROR: return "MQTT_REQUEST_TIMEOUT_ERROR";
    case MQTT_UNEXPECTED_CLIENT_STATE_ERROR: return "MQTT_UNEXPECTED_CLIENT_STATE_ERROR";
    case MQTT_RX_MESSAGE_PACKET_TYPE_INVALID_ERROR: return "MQTT_RX_MESSAGE_PACKET_TYPE_INVALID_ERROR";
    case MQTT_RX_BUFFER_TOO_SHORT_ERROR: return "MQTT_RX_BUFFER_TOO_SHORT_ERROR";
    default: return "UNKNOWN_ERROR";
    }
}

static inline esp_err_t convert_iot_error(IoT_Error_t error)
{
    switch (error) {
    case SUCCESS:
        return ESP_OK;
    case NULL_VALUE_ERROR:
    case MQTT_MAX_SUBSCRIPTIONS_REACHED_ERROR:
    case MQTT_CLIENT_NOT_IDLE_ERROR:
        return ESP_ERR_INVALID_STATE;
    case NETWORK_DISCONNECTED_ERROR:
    case NETWORK_ATTEMPTING_RECONNECT:
    case NETWORK_MANUALLY_DISCONNECTED:
        return ESP_ERR_INVALID_STATE;
    case NETWORK_RECONNECT_TIMED_OUT_ERROR:
    case NETWORK_X509_ROOT_CRT_PARSE_ERROR:
    case NETWORK_X509_DEVICE_CRT_PARSE_ERROR:
    case NETWORK_PK_PRIVATE_KEY_PARSE_ERROR:
    case NETWORK_SSL_CONNECT_TIMEOUT_ERROR:
    case NETWORK_SSL_READ_ERROR:
    case NETWORK_SSL_WRITE_ERROR:
    case NETWORK_SSL_NOTHING_TO_READ:
        return ESP_ERR_INVALID_RESPONSE;
    case MQTT_CONNACK_UNACCEPTABLE_PROTOCOL_VERSION_ERROR:
    case MQTT_CONNACK_IDENTIFIER_REJECTED_ERROR:
    case MQTT_CONNACK_SERVER_UNAVAILABLE_ERROR:
    case MQTT_CONNACK_BAD_USERDATA_ERROR:
    case MQTT_CONNACK_NOT_AUTHORIZED_ERROR:
        return ESP_ERR_INVALID_STATE;
    case MQTT_REQUEST_TIMEOUT_ERROR:
    case MQTT_UNEXPECTED_CLIENT_STATE_ERROR:
    case MQTT_RX_MESSAGE_PACKET_TYPE_INVALID_ERROR:
    case MQTT_RX_BUFFER_TOO_SHORT_ERROR:
        return ESP_ERR_TIMEOUT;
    default:
        return ESP_FAIL;
    }
}

static bool validate_config(const aws_iot_config_t *config)
{
    if (!config) {
        return false;
    }

    return config->endpoint && config->client_id && config->root_ca && config->client_cert && config->client_key;
}

esp_err_t aws_iot_client_init(aws_iot_client_t *client, const aws_iot_config_t *config)
{
    ESP_RETURN_ON_FALSE(client, ESP_ERR_INVALID_ARG, TAG, "client handle is NULL");
    ESP_RETURN_ON_FALSE(validate_config(config), ESP_ERR_INVALID_ARG, TAG, "configuration is invalid");

    memset(client, 0, sizeof(*client));

    client->init_params = iotClientInitParamsDefault;
    client->init_params.enableAutoReconnect = config->auto_reconnect;
    client->init_params.pHostURL = (char *)config->endpoint;
    client->init_params.port = config->port ? config->port : 8883;
    client->init_params.pRootCALocation = (char *)config->root_ca;
    client->init_params.pDeviceCertLocation = (char *)config->client_cert;
    client->init_params.pDevicePrivateKeyLocation = (char *)config->client_key;
    client->init_params.disconnectHandler = internal_disconnect_handler;
    client->init_params.disconnectHandlerData = client;
    client->init_params.isSSLHostnameVerify = true;

    client->connect_params = iotClientConnectParamsDefault;
    client->connect_params.keepAliveIntervalInSec = config->keepalive_sec ? config->keepalive_sec : 60;
    client->connect_params.isCleanSession = config->clean_session;
    client->connect_params.MQTTVersion = MQTT_3_1_1;
    client->connect_params.pClientID = (char *)config->client_id;
    client->connect_params.clientIDLen = (uint16_t)strlen(config->client_id);
    client->connect_params.isWillMsgPresent = false;

    IoT_Error_t rc = aws_iot_mqtt_init(&client->client, &client->init_params);
    if (SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_init failed (%d)", rc);
        return convert_iot_error(rc);
    }

    if (config->auto_reconnect) {
        rc = aws_iot_mqtt_autoreconnect_set_status(&client->client, true);
        if (SUCCESS != rc) {
            ESP_LOGW(TAG, "Auto-reconnect enable failed (%d)", rc);
        }
    }

    client->initialized = true;
    client->connected = false;
    return ESP_OK;
}

esp_err_t aws_iot_client_connect(aws_iot_client_t *client)
{
    ESP_RETURN_ON_FALSE(client, ESP_ERR_INVALID_ARG, TAG, "client handle is NULL");
    ESP_RETURN_ON_FALSE(client->initialized, ESP_ERR_INVALID_STATE, TAG, "client not initialised");

    ESP_LOGI(TAG, "Attempting AWS IoT connection to %s:%d (client_id=%s)",
             client->init_params.pHostURL ? client->init_params.pHostURL : "(null)",
             client->init_params.port,
             client->connect_params.pClientID ? client->connect_params.pClientID : "(null)");

    IoT_Error_t rc = aws_iot_mqtt_connect(&client->client, &client->connect_params);
    if (SUCCESS != rc && NETWORK_ALREADY_CONNECTED_ERROR != rc) {
        if (NETWORK_ATTEMPTING_RECONNECT == rc) {
            ESP_LOGW(TAG, "Auto-reconnect in progress");
            client->connected = true;
            return ESP_OK;
        }
        ESP_LOGE(TAG, "aws_iot_mqtt_connect failed: %s (%d)", iot_error_to_string(rc), rc);
        if (rc == NETWORK_SSL_CONNECT_TIMEOUT_ERROR || rc == NETWORK_RECONNECT_TIMED_OUT_ERROR) {
            ESP_LOGE(TAG, "SSL/TLS connection timeout - check endpoint, certificates, and network connectivity");
        } else if (rc == NETWORK_SSL_READ_ERROR || rc == NETWORK_SSL_WRITE_ERROR) {
            ESP_LOGE(TAG, "SSL/TLS read/write error - possible certificate mismatch, expired cert, or network issue");
            ESP_LOGE(TAG, "Verify: 1) Certificates match device in AWS IoT, 2) Certificates not expired, 3) Network allows port 8883");
        } else if (rc == NETWORK_X509_ROOT_CRT_PARSE_ERROR || rc == NETWORK_X509_DEVICE_CRT_PARSE_ERROR) {
            ESP_LOGE(TAG, "Certificate parse error - verify certificate format and content");
        } else if (rc == NETWORK_PK_PRIVATE_KEY_PARSE_ERROR) {
            ESP_LOGE(TAG, "Private key parse error - verify key format and content");
        } else if (rc == MQTT_CONNACK_NOT_AUTHORIZED_ERROR) {
            ESP_LOGE(TAG, "MQTT authorization failed - check client ID and IAM policy");
        }
        return convert_iot_error(rc);
    }

    client->connected = true;
    ESP_LOGI(TAG, "Connected to AWS IoT Core");
    return ESP_OK;
}

esp_err_t aws_iot_client_disconnect(aws_iot_client_t *client)
{
    ESP_RETURN_ON_FALSE(client, ESP_ERR_INVALID_ARG, TAG, "client handle is NULL");
    ESP_RETURN_ON_FALSE(client->initialized, ESP_ERR_INVALID_STATE, TAG, "client not initialised");

    IoT_Error_t rc = aws_iot_mqtt_disconnect(&client->client);
    if (SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_disconnect failed (%d)", rc);
        return convert_iot_error(rc);
    }

    client->connected = false;
    return ESP_OK;
}

esp_err_t aws_iot_client_yield(aws_iot_client_t *client, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(client, ESP_ERR_INVALID_ARG, TAG, "client handle is NULL");
    ESP_RETURN_ON_FALSE(client->initialized, ESP_ERR_INVALID_STATE, TAG, "client not initialised");

    IoT_Error_t rc = aws_iot_mqtt_yield(&client->client, timeout_ms);
    if (SUCCESS != rc && NETWORK_ATTEMPTING_RECONNECT != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_yield failed (%d)", rc);
        return convert_iot_error(rc);
    }

    return ESP_OK;
}

esp_err_t aws_iot_client_publish(aws_iot_client_t *client,
                                 const char *topic,
                                 QoS qos,
                                 const void *payload,
                                 size_t payload_len,
                                 bool retain)
{
    ESP_RETURN_ON_FALSE(client, ESP_ERR_INVALID_ARG, TAG, "client handle is NULL");
    ESP_RETURN_ON_FALSE(topic && payload, ESP_ERR_INVALID_ARG, TAG, "topic/payload invalid");
    ESP_RETURN_ON_FALSE(client->initialized, ESP_ERR_INVALID_STATE, TAG, "client not initialised");

    IoT_Publish_Message_Params params = {
        .qos = qos,
        .isRetained = retain,
        .payload = (void *)payload,
        .payloadLen = payload_len,
    };

    IoT_Error_t rc = aws_iot_mqtt_publish(&client->client, topic, (uint16_t)strlen(topic), &params);
    if (SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_publish failed (%d)", rc);
        return convert_iot_error(rc);
    }

    return ESP_OK;
}

esp_err_t aws_iot_client_subscribe(aws_iot_client_t *client,
                                   const char *topic,
                                   QoS qos,
                                   pApplicationHandler_t handler,
                                   void *handler_ctx)
{
    ESP_RETURN_ON_FALSE(client, ESP_ERR_INVALID_ARG, TAG, "client handle is NULL");
    ESP_RETURN_ON_FALSE(topic && handler, ESP_ERR_INVALID_ARG, TAG, "topic/handler invalid");
    ESP_RETURN_ON_FALSE(client->initialized, ESP_ERR_INVALID_STATE, TAG, "client not initialised");

    IoT_Error_t rc = aws_iot_mqtt_subscribe(&client->client, topic, (uint16_t)strlen(topic), qos, handler, handler_ctx);
    if (SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_subscribe failed (%d)", rc);
        return convert_iot_error(rc);
    }

    return ESP_OK;
}

bool aws_iot_client_is_connected(const aws_iot_client_t *client)
{
    if (!client) {
        return false;
    }

    return client->connected;
}

void aws_iot_client_set_disconnect_callback(aws_iot_client_t *client,
                                            aws_iot_disconnect_cb_t cb,
                                            void *ctx)
{
    if (!client) {
        return;
    }

    client->disconnect_cb = cb;
    client->disconnect_ctx = ctx;
}

AWS_IoT_Client *aws_iot_client_get_mqtt_client(aws_iot_client_t *client)
{
    if (!client) {
        return NULL;
    }

    return &client->client;
}

static void internal_disconnect_handler(AWS_IoT_Client *client, void *data)
{
    aws_iot_client_t *wrapper = (aws_iot_client_t *)data;
    if (!wrapper) {
        return;
    }

    wrapper->connected = false;

    ESP_LOGW(TAG, "AWS IoT disconnected");

    if (wrapper->disconnect_cb) {
        wrapper->disconnect_cb(client, wrapper->disconnect_ctx);
    }
}

