/**
 * @file naptick_mqtt.h
 * @brief Naptick MQTT client over WebSocket Secure
 */

#ifndef NAPTICK_MQTT_H
#define NAPTICK_MQTT_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback for MQTT message received
 *
 * @param topic Topic name
 * @param topic_len Length of topic
 * @param data Message data
 * @param data_len Length of data
 */
typedef void (*naptick_mqtt_message_cb_t)(const char *topic, int topic_len,
                                          const char *data, int data_len);

/**
 * @brief Start the MQTT client
 *
 * Connects to the Naptick MQTT broker using WebSocket Secure.
 * Uses device ID as username and token as password from naptick_config.
 *
 * @param message_cb Optional callback for received messages
 * @return ESP_OK on success
 */
esp_err_t naptick_mqtt_start(naptick_mqtt_message_cb_t message_cb);

/**
 * @brief Stop the MQTT client
 *
 * @return ESP_OK on success
 */
esp_err_t naptick_mqtt_stop(void);

/**
 * @brief Check if MQTT is connected
 *
 * @return true if connected
 */
bool naptick_mqtt_is_connected(void);

/**
 * @brief Publish a message
 *
 * @param topic Topic to publish to
 * @param data Message data
 * @param data_len Length of data
 * @param qos QoS level (0, 1, or 2)
 * @return ESP_OK on success
 */
esp_err_t naptick_mqtt_publish(const char *topic, const char *data, int data_len, int qos);

#ifdef __cplusplus
}
#endif

#endif /* NAPTICK_MQTT_H */
