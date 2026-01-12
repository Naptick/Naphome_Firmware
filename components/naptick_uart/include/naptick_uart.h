/**
 * @file naptick_uart.h
 * @brief Naptick UART sensor data ingestion
 */

#ifndef NAPTICK_UART_H
#define NAPTICK_UART_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sensor data structure
 */
typedef struct {
    float temperature;
    float humidity;
    float co2;
    float voc_index;
    float pm25;
    int ambient_light_lux;
    int noise_db;
    bool motion_detected;
    bool valid;
} naptick_sensor_data_t;

/**
 * @brief Initialize UART for sensor data
 *
 * @return ESP_OK on success
 */
esp_err_t naptick_uart_init(void);

/**
 * @brief Start the UART receive task
 *
 * @return ESP_OK on success
 */
esp_err_t naptick_uart_start(void);

/**
 * @brief Stop the UART receive task
 *
 * @return ESP_OK on success
 */
esp_err_t naptick_uart_stop(void);

/**
 * @brief Get the latest sensor data
 *
 * @return Pointer to sensor data structure
 */
const naptick_sensor_data_t* naptick_uart_get_sensor_data(void);

#ifdef __cplusplus
}
#endif

#endif /* NAPTICK_UART_H */
