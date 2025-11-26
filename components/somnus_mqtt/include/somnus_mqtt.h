/**
 * @file somnus_mqtt.h
 * @brief Somnus-compatible AWS IoT MQTT surface.
 */

#pragma once

#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback invoked for Somnus action payloads received from MQTT.
 *
 * The callback receives the raw JSON payload string exactly as delivered over
 * MQTT. The string lifetime ends after the callback returns; copy it if needed.
 */
typedef void (*somnus_mqtt_action_cb_t)(const char *payload, void *ctx);

typedef struct {
    somnus_mqtt_action_cb_t action_cb; /**< Optional handler for action payloads. */
    void *action_ctx;                  /**< Context passed to @p action_cb. */
} somnus_mqtt_config_t;

/**
 * @brief Start the Somnus MQTT service (idempotent).
 *
 * Establishes the AWS IoT connection using Somnus certificate discovery,
 * subscribes to the command topic, and begins publishing Somnus-formatted logs.
 */
esp_err_t somnus_mqtt_start(const somnus_mqtt_config_t *config);

/**
 * @brief Stop the Somnus MQTT service and release allocated resources.
 */
esp_err_t somnus_mqtt_stop(void);

/**
 * @brief Publish a Somnus log payload.
 *
 * @param level   Log severity string (e.g. "INFO", "WARN").
 * @param message Log message body.
 */
esp_err_t somnus_mqtt_publish_log(const char *level, const char *message);

/**
 * @brief Publish a prebuilt JSON payload to the Somnus log topic.
 */
esp_err_t somnus_mqtt_publish_raw_log(const char *json_payload);

/**
 * @brief Publish a telemetry payload to the Somnus telemetry topic.
 *
 * @param json_payload Null-terminated JSON payload string to transmit.
 */
esp_err_t somnus_mqtt_publish_telemetry(const char *json_payload);

/**
 * @brief Retrieve the Somnus device identifier used for MQTT.
 */
const char *somnus_mqtt_get_device_id(void);

#ifdef __cplusplus
}
#endif

