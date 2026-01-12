/**
 * @file naptick_http.h
 * @brief Naptick HTTP client for device registration and sensor uploads
 */

#ifndef NAPTICK_HTTP_H
#define NAPTICK_HTTP_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the device with the Naptick backend
 *
 * Sends a POST request to the registration endpoint with the device ID.
 * Uses the token from naptick_config for authorization.
 *
 * @return ESP_OK on success (HTTP 200/201)
 * @return ESP_ERR_INVALID_STATE if token is not set
 * @return ESP_FAIL on HTTP error or non-success status
 */
esp_err_t naptick_http_register_device(void);

/**
 * @brief Upload sensor data to the Naptick backend
 *
 * @param json_payload JSON string containing sensor data
 * @return ESP_OK on success
 * @return ESP_FAIL on error
 */
esp_err_t naptick_http_upload_sensors(const char *json_payload);

/**
 * @brief Build a sensor upload JSON payload
 *
 * Creates a JSON payload with the device ID, timestamp, and sensor values.
 *
 * @param temperature Temperature in Celsius
 * @param humidity Humidity percentage
 * @param co2 CO2 in ppm
 * @param voc_index VOC index
 * @param pm25 PM2.5 in ug/m3
 * @param light_lux Ambient light in lux
 * @param noise_db Noise level in dB
 * @param motion Motion detected flag
 * @param out Output buffer for JSON string
 * @param out_len Length of output buffer
 * @return ESP_OK on success
 */
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
    size_t out_len
);

#ifdef __cplusplus
}
#endif

#endif /* NAPTICK_HTTP_H */
