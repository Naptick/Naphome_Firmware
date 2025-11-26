/**
 * @file somnus_ble.h
 * @brief Somnus-compatible BLE UART service for onboarding.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback invoked when the mobile app requests a Wi-Fi connection.
 *
 * This mirrors the behaviour of SomnusDevice's onboarding flow: the BLE
 * characteristic delivers the SSID, password, user token, and an indicator
 * for production mode. The callback should attempt to establish the connection
 * and return true on success.
 */
typedef bool (*somnus_ble_connect_wifi_cb_t)(const char *ssid,
                                             const char *password,
                                             const char *user_token,
                                             bool is_production,
                                             void *ctx);

/**
 * @brief Configuration for the Somnus BLE service.
 */
typedef struct {
    somnus_ble_connect_wifi_cb_t connect_cb; /**< Optional Wi-Fi connect handler. */
    void *connect_ctx;                       /**< Context pointer passed to @p connect_cb. */
} somnus_ble_config_t;

/**
 * @brief Start the Somnus BLE UART service.
 *
 * @param config Optional configuration (may be NULL).
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t somnus_ble_start(const somnus_ble_config_t *config);

/**
 * @brief Stop the Somnus BLE UART service and release resources.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t somnus_ble_stop(void);

/**
 * @brief Check if the Somnus BLE service is currently advertising/active.
 */
bool somnus_ble_is_running(void);

/**
 * @brief Send a notification payload to the connected mobile app.
 *
 * Messages longer than the BLE MTU are chunked into 20-byte fragments to
 * remain compatible with the reference Somnus implementation.
 *
 * @param message Null-terminated string to transmit.
 * @return ESP_OK if notification was queued for delivery, ESP_ERR_INVALID_STATE
 *         if no client is subscribed, or another esp_err_t on failure.
 */
esp_err_t somnus_ble_notify(const char *message);

#ifdef __cplusplus
}
#endif

