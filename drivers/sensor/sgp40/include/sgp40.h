/**
 * @file sgp40.h
 * @brief SGP40 VOC Sensor Driver
 * 
 * Driver for Sensirion SGP40 sensor connected via I2C.
 * Provides interface for reading VOC (Volatile Organic Compounds) measurements.
 */

#ifndef SGP40_H
#define SGP40_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

/**
 * @brief SGP40 device handle
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus;  ///< I2C bus handle
    uint8_t device_address;           ///< I2C device address (default 0x59)
    bool initialized;                  ///< Initialization status
} sgp40_handle_t;

/**
 * @brief SGP40 sensor data structure
 */
typedef struct {
    uint16_t voc_raw;      ///< Raw VOC signal (ADC value)
    int32_t voc_index;     ///< VOC index (0-500, requires VOC algorithm)
    bool valid;            ///< Data validity flag
} sgp40_data_t;

/**
 * @brief Initialize SGP40 sensor
 * 
 * @param handle Pointer to SGP40 handle (will be initialized)
 * @param i2c_bus I2C bus handle (must be initialized)
 * @param device_addr I2C device address (default: 0x59)
 * @return true if initialization successful, false otherwise
 */
bool sgp40_init(sgp40_handle_t *handle, i2c_master_bus_handle_t i2c_bus, uint8_t device_addr);

/**
 * @brief Deinitialize SGP40 sensor
 * 
 * @param handle SGP40 handle
 */
void sgp40_deinit(sgp40_handle_t *handle);

/**
 * @brief Read raw VOC signal from sensor
 * 
 * @param handle SGP40 handle
 * @param data Pointer to data structure (will be populated)
 * @return true if read successful, false otherwise
 */
bool sgp40_read(sgp40_handle_t *handle, sgp40_data_t *data);

/**
 * @brief Read raw VOC signal with temperature and humidity compensation
 * 
 * @param handle SGP40 handle
 * @param data Pointer to data structure (will be populated)
 * @param temperature_c Temperature in Celsius for compensation
 * @param humidity_rh Relative humidity in % for compensation
 * @return true if read successful, false otherwise
 */
bool sgp40_read_compensated(sgp40_handle_t *handle, sgp40_data_t *data, 
                            float temperature_c, float humidity_rh);

/**
 * @brief Read raw VOC value only
 * 
 * @param handle SGP40 handle
 * @param voc_raw Pointer to raw VOC value
 * @return true if read successful, false otherwise
 */
bool sgp40_read_raw(sgp40_handle_t *handle, uint16_t *voc_raw);

/**
 * @brief Perform self-test on sensor
 * 
 * @param handle SGP40 handle
 * @return true if self-test passed, false otherwise
 */
bool sgp40_self_test(sgp40_handle_t *handle);

/**
 * @brief Check if sensor is initialized
 * 
 * @param handle SGP40 handle
 * @return true if initialized, false otherwise
 */
bool sgp40_is_initialized(const sgp40_handle_t *handle);

#endif // SGP40_H
