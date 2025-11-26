/**
 * @file aws_iot.h
 * @brief High-level helper for connecting Naphome devices to AWS IoT Core.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "aws_iot_mqtt_client_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration needed to establish a mutual TLS connection to AWS IoT Core.
 *
 * The certificate fields are expected to be PEM-formatted, null-terminated strings
 * that live for the lifetime of the client instance (e.g. embedded with `extern const char`).
 */
typedef struct {
    const char *endpoint;        /**< AWS IoT endpoint, e.g. "xxxxxxxxxx-ats.iot.us-west-2.amazonaws.com" */
    uint16_t port;               /**< MQTT port, typically 8883 for TLS. */
    const char *client_id;       /**< Client ID / Thing name used when connecting. */
    const char *root_ca;         /**< Root CA certificate (PEM). */
    size_t root_ca_len;          /**< Length of root CA string including terminator. */
    const char *client_cert;     /**< Device certificate (PEM). */
    size_t client_cert_len;      /**< Length of device certificate string including terminator. */
    const char *client_key;      /**< Device private key (PEM). */
    size_t client_key_len;       /**< Length of private key string including terminator. */
    uint32_t keepalive_sec;      /**< MQTT keepalive interval in seconds. */
    bool clean_session;          /**< Whether to start a clean session on connect. */
    bool auto_reconnect;         /**< Enable automatic reconnect handling by the SDK. */
} aws_iot_config_t;

/**
 * @brief Disconnect callback signature.
 */
typedef void (*aws_iot_disconnect_cb_t)(AWS_IoT_Client *client, void *ctx);

/**
 * @brief AWS IoT client wrapper.
 */
typedef struct {
    AWS_IoT_Client client;
    IoT_Client_Init_Params init_params;
    IoT_Client_Connect_Params connect_params;
    bool initialized;
    bool connected;
    aws_iot_disconnect_cb_t disconnect_cb;
    void *disconnect_ctx;
} aws_iot_client_t;

/**
 * @brief Helper macro to initialise a configuration struct with sensible defaults.
 */
#define AWS_IOT_CONFIG_DEFAULT()                     \
    {                                                \
        .port = 8883,                                \
        .root_ca_len = 0,                            \
        .client_cert_len = 0,                        \
        .client_key_len = 0,                         \
        .keepalive_sec = 60,                         \
        .clean_session = true,                       \
        .auto_reconnect = true,                      \
    }

/**
 * @brief Populate an AWS IoT configuration using menuconfig settings and embedded credentials.
 *
 * The endpoint and client ID are read from the corresponding `CONFIG_NAPHOME_AWS_IOT_*` options,
 * while the certificates are sourced from the PEM files embedded via the component's CMakeLists.
 *
 * @param[out] config Destination configuration.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t aws_iot_config_load_from_kconfig(aws_iot_config_t *config);

/**
 * @brief Initialise an AWS IoT client using the default configuration populated from menuconfig.
 *
 * This is a convenience wrapper that calls @ref aws_iot_config_load_from_kconfig internally and
 * forwards the result to @ref aws_iot_client_init.
 *
 * @param[in,out] client Client instance to initialise.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t aws_iot_client_init_from_settings(aws_iot_client_t *client);

esp_err_t aws_iot_client_init(aws_iot_client_t *client, const aws_iot_config_t *config);
esp_err_t aws_iot_client_connect(aws_iot_client_t *client);
esp_err_t aws_iot_client_disconnect(aws_iot_client_t *client);
esp_err_t aws_iot_client_yield(aws_iot_client_t *client, uint32_t timeout_ms);
esp_err_t aws_iot_client_publish(aws_iot_client_t *client,
                                 const char *topic,
                                 QoS qos,
                                 const void *payload,
                                 size_t payload_len,
                                 bool retain);
esp_err_t aws_iot_client_subscribe(aws_iot_client_t *client,
                                   const char *topic,
                                   QoS qos,
                                   pApplicationHandler_t handler,
                                   void *handler_ctx);
bool aws_iot_client_is_connected(const aws_iot_client_t *client);
void aws_iot_client_set_disconnect_callback(aws_iot_client_t *client,
                                            aws_iot_disconnect_cb_t cb,
                                            void *ctx);
AWS_IoT_Client *aws_iot_client_get_mqtt_client(aws_iot_client_t *client);

#ifdef __cplusplus
}
#endif

