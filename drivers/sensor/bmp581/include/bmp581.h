/**
 * @file bmp581.h
 * @brief BMP581 Barometric Pressure Sensor Driver
 * 
 * Driver for Bosch BMP581 sensor connected via I2C.
 * Provides clean interface for reading pressure and temperature.
 */

#ifndef BMP581_H
#define BMP581_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

/**
 * @brief BMP581 device handle
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus;  ///< I2C bus handle
    uint8_t device_address;            ///< I2C device address (default 0x46 or 0x47)
    bool initialized;                   ///< Initialization status
} bmp581_handle_t;

/**
 * @brief BMP581 sensor data structure
 */
typedef struct {
    float pressure_pa;     ///< Pressure in Pascals
    float temperature_c;   ///< Temperature in Celsius
    bool valid;            ///< Data validity flag
} bmp581_data_t;

/**
 * @brief Initialize BMP581 sensor
 * 
 * @param handle Pointer to BMP581 handle (will be initialized)
 * @param i2c_bus I2C bus handle (must be initialized)
 * @param device_addr I2C device address (default: 0x46, alternative: 0x47)
 * @return true if initialization successful, false otherwise
 */
bool bmp581_init(bmp581_handle_t *handle, i2c_master_bus_handle_t i2c_bus, uint8_t device_addr);

/**
 * @brief Deinitialize BMP581 sensor
 * 
 * @param handle BMP581 handle
 */
void bmp581_deinit(bmp581_handle_t *handle);

/**
 * @brief Read pressure and temperature from sensor
 * 
 * @param handle BMP581 handle
 * @param data Pointer to data structure (will be populated)
 * @return true if read successful, false otherwise
 */
bool bmp581_read(bmp581_handle_t *handle, bmp581_data_t *data);

/**
 * @brief Read pressure only
 * 
 * @param handle BMP581 handle
 * @param pressure_pa Pointer to pressure value (Pascals)
 * @return true if read successful, false otherwise
 */
bool bmp581_read_pressure(bmp581_handle_t *handle, float *pressure_pa);

/**
 * @brief Read pressure in hPa (hectopascals/millibars)
 * 
 * @param handle BMP581 handle
 * @param pressure_hpa Pointer to pressure value (hPa)
 * @return true if read successful, false otherwise
 */
bool bmp581_read_pressure_hpa(bmp581_handle_t *handle, float *pressure_hpa);

/**
 * @brief Read temperature only
 * 
 * @param handle BMP581 handle
 * @param temperature_c Pointer to temperature value (Celsius)
 * @return true if read successful, false otherwise
 */
bool bmp581_read_temperature(bmp581_handle_t *handle, float *temperature_c);

/**
 * @brief Check if sensor is initialized
 * 
 * @param handle BMP581 handle
 * @return true if initialized, false otherwise
 */
bool bmp581_is_initialized(const bmp581_handle_t *handle);

#endif // BMP581_H
