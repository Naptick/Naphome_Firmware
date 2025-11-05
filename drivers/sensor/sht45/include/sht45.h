/**
 * @file sht45.h
 * @brief SHT45 Temperature and Humidity Sensor Driver
 * 
 * Driver for Sensirion SHT45 sensor connected via I2C.
 * Provides clean interface for reading temperature and humidity.
 */

#ifndef SHT45_H
#define SHT45_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

/**
 * @brief SHT45 device handle
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus;  ///< I2C bus handle
    uint8_t device_address;           ///< I2C device address (default 0x44)
    bool initialized;                  ///< Initialization status
} sht45_handle_t;

/**
 * @brief SHT45 sensor data structure
 */
typedef struct {
    float temperature_c;   ///< Temperature in Celsius
    float humidity_rh;    ///< Relative humidity in %
    bool valid;            ///< Data validity flag
} sht45_data_t;

/**
 * @brief Initialize SHT45 sensor
 * 
 * @param handle Pointer to SHT45 handle (will be initialized)
 * @param i2c_bus I2C bus handle (must be initialized)
 * @param device_addr I2C device address (default: 0x44)
 * @return true if initialization successful, false otherwise
 */
bool sht45_init(sht45_handle_t *handle, i2c_master_bus_handle_t i2c_bus, uint8_t device_addr);

/**
 * @brief Deinitialize SHT45 sensor
 * 
 * @param handle SHT45 handle
 */
void sht45_deinit(sht45_handle_t *handle);

/**
 * @brief Read temperature and humidity from sensor
 * 
 * @param handle SHT45 handle
 * @param data Pointer to data structure (will be populated)
 * @return true if read successful, false otherwise
 */
bool sht45_read(sht45_handle_t *handle, sht45_data_t *data);

/**
 * @brief Read temperature only
 * 
 * @param handle SHT45 handle
 * @param temperature_c Pointer to temperature value (Celsius)
 * @return true if read successful, false otherwise
 */
bool sht45_read_temperature(sht45_handle_t *handle, float *temperature_c);

/**
 * @brief Read humidity only
 * 
 * @param handle SHT45 handle
 * @param humidity_rh Pointer to humidity value (%)
 * @return true if read successful, false otherwise
 */
bool sht45_read_humidity(sht45_handle_t *handle, float *humidity_rh);

/**
 * @brief Check if sensor is initialized
 * 
 * @param handle SHT45 handle
 * @return true if initialized, false otherwise
 */
bool sht45_is_initialized(const sht45_handle_t *handle);

#endif // SHT45_H
