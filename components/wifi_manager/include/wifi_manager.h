#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi connection state change callback
 *
 * @param connected true if WiFi is now connected, false if disconnected
 * @param user_ctx User context pointer passed during registration
 */
typedef void (*wifi_manager_state_cb_t)(bool connected, void *user_ctx);

/**
 * @brief Register a callback for WiFi connection state changes
 *
 * @param callback Function to call on state change
 * @param user_ctx User context pointer passed to callback
 */
void wifi_manager_set_state_callback(wifi_manager_state_cb_t callback, void *user_ctx);

/**
 * @brief Initialize and start the WiFi manager
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Stop the WiFi manager and release resources
 */
esp_err_t wifi_manager_stop(void);

/**
 * @brief Check if WiFi is currently connected
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Wait for WiFi connection with timeout
 */
esp_err_t wifi_manager_wait_for_connection(TickType_t ticks_to_wait);

/**
 * @brief Connect to a WiFi network at runtime
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password, TickType_t ticks_to_wait);

/**
 * @brief Perform WiFi scan and return results
 *
 * @param ap_records Array to store scan results
 * @param max_records Maximum number of records to store
 * @param num_found Output: number of APs found
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t max_records, uint16_t *num_found);

#ifdef __cplusplus
}
#endif

