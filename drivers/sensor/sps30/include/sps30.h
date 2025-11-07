/**
 * @file sps30.h
 * @brief SPS30 PM2.5 Air Quality Sensor Driver
 * 
 * Driver for Sensirion SPS30 particulate matter sensor connected via UART.
 * Provides clean interface for reading PM1.0, PM2.5, PM4.0, and PM10 values.
 * 
 * Communication Protocol: UART with SHDLC (Sensirion HDLC) framing
 * Baud Rate: 115200, 8N1
 */

#ifndef SPS30_H
#define SPS30_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"

/**
 * @brief SPS30 device handle
 */
typedef struct {
    uart_port_t uart_port;        ///< UART port number
    bool initialized;              ///< Initialization status
    bool measuring;                ///< Measurement state
} sps30_handle_t;

/**
 * @brief SPS30 sensor data structure
 * 
 * Contains mass concentration values in µg/m³
 */
typedef struct {
    float pm1_0;      ///< PM1.0 mass concentration (µg/m³)
    float pm2_5;      ///< PM2.5 mass concentration (µg/m³)
    float pm4_0;      ///< PM4.0 mass concentration (µg/m³)
    float pm10;       ///< PM10 mass concentration (µg/m³)
    bool valid;       ///< Data validity flag
} sps30_data_t;

/**
 * @brief Initialize SPS30 sensor
 * 
 * Configures UART communication and initializes the sensor.
 * 
 * @param handle Pointer to SPS30 handle (will be initialized)
 * @param uart_port UART port number (e.g., UART_NUM_1)
 * @param tx_pin GPIO pin for UART TX
 * @param rx_pin GPIO pin for UART RX
 * @return true if initialization successful, false otherwise
 */
bool sps30_init(sps30_handle_t *handle, uart_port_t uart_port, int tx_pin, int rx_pin);

/**
 * @brief Deinitialize SPS30 sensor
 * 
 * Stops measurement and releases UART resources.
 * 
 * @param handle SPS30 handle
 */
void sps30_deinit(sps30_handle_t *handle);

/**
 * @brief Start measurement
 * 
 * Starts the fan and begins continuous measurement.
 * Measurement must be started before reading data.
 * 
 * @param handle SPS30 handle
 * @return true if successful, false otherwise
 */
bool sps30_start_measurement(sps30_handle_t *handle);

/**
 * @brief Stop measurement
 * 
 * Stops the fan and measurement.
 * 
 * @param handle SPS30 handle
 * @return true if successful, false otherwise
 */
bool sps30_stop_measurement(sps30_handle_t *handle);

/**
 * @brief Read PM values from sensor
 * 
 * Reads PM1.0, PM2.5, PM4.0, and PM10 mass concentration values.
 * Measurement must be started before calling this function.
 * 
 * @param handle SPS30 handle
 * @param data Pointer to data structure (will be populated)
 * @return true if read successful, false otherwise
 */
bool sps30_read(sps30_handle_t *handle, sps30_data_t *data);

/**
 * @brief Read PM2.5 value only
 * 
 * Convenience function to read only PM2.5 value.
 * 
 * @param handle SPS30 handle
 * @param pm2_5 Pointer to PM2.5 value (µg/m³)
 * @return true if read successful, false otherwise
 */
bool sps30_read_pm25(sps30_handle_t *handle, float *pm2_5);

/**
 * @brief Check if sensor is initialized
 * 
 * @param handle SPS30 handle
 * @return true if initialized, false otherwise
 */
bool sps30_is_initialized(const sps30_handle_t *handle);

/**
 * @brief Check if sensor is measuring
 * 
 * @param handle SPS30 handle
 * @return true if measuring, false otherwise
 */
bool sps30_is_measuring(const sps30_handle_t *handle);

#endif // SPS30_H
