/**
 * @file scd41.h
 * @brief SCD41 CO2 Sensor Driver
 * 
 * Driver for Sensirion SCD41 CO2, temperature, and humidity sensor connected via I2C.
 * Provides clean interface for reading CO2 concentration, temperature, and humidity.
 */

#ifndef SCD41_H
#define SCD41_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

/**
 * @brief SCD41 device handle
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus;  ///< I2C bus handle
    uint8_t device_address;           ///< I2C device address (default 0x62)
    bool initialized;                  ///< Initialization status
    bool measurement_started;          ///< Periodic measurement status
} scd41_handle_t;

/**
 * @brief SCD41 sensor data structure
 */
typedef struct {
    uint16_t co2_ppm;         ///< CO2 concentration in ppm
    float temperature_c;      ///< Temperature in Celsius
    float humidity_rh;        ///< Relative humidity in %
    bool valid;               ///< Data validity flag
} scd41_data_t;

/**
 * @brief Initialize SCD41 sensor
 * 
 * @param handle Pointer to SCD41 handle (will be initialized)
 * @param i2c_bus I2C bus handle (must be initialized)
 * @param device_addr I2C device address (default: 0x62)
 * @return true if initialization successful, false otherwise
 */
bool scd41_init(scd41_handle_t *handle, i2c_master_bus_handle_t i2c_bus, uint8_t device_addr);

/**
 * @brief Deinitialize SCD41 sensor
 * 
 * @param handle SCD41 handle
 */
void scd41_deinit(scd41_handle_t *handle);

/**
 * @brief Start periodic measurement
 * 
 * Starts periodic measurement mode (one measurement every 5 seconds).
 * Must be called before reading sensor data.
 * 
 * @param handle SCD41 handle
 * @return true if command successful, false otherwise
 */
bool scd41_start_periodic_measurement(scd41_handle_t *handle);

/**
 * @brief Stop periodic measurement
 * 
 * Stops periodic measurement mode.
 * 
 * @param handle SCD41 handle
 * @return true if command successful, false otherwise
 */
bool scd41_stop_periodic_measurement(scd41_handle_t *handle);

/**
 * @brief Read CO2, temperature, and humidity from sensor
 * 
 * Reads all sensor values. Periodic measurement must be started first.
 * First measurement is available after approximately 5 seconds.
 * 
 * @param handle SCD41 handle
 * @param data Pointer to data structure (will be populated)
 * @return true if read successful, false otherwise
 */
bool scd41_read(scd41_handle_t *handle, scd41_data_t *data);

/**
 * @brief Read CO2 concentration only
 * 
 * @param handle SCD41 handle
 * @param co2_ppm Pointer to CO2 value (ppm)
 * @return true if read successful, false otherwise
 */
bool scd41_read_co2(scd41_handle_t *handle, uint16_t *co2_ppm);

/**
 * @brief Read temperature only
 * 
 * @param handle SCD41 handle
 * @param temperature_c Pointer to temperature value (Celsius)
 * @return true if read successful, false otherwise
 */
bool scd41_read_temperature(scd41_handle_t *handle, float *temperature_c);

/**
 * @brief Read humidity only
 * 
 * @param handle SCD41 handle
 * @param humidity_rh Pointer to humidity value (%)
 * @return true if read successful, false otherwise
 */
bool scd41_read_humidity(scd41_handle_t *handle, float *humidity_rh);

/**
 * @brief Check if sensor is initialized
 * 
 * @param handle SCD41 handle
 * @return true if initialized, false otherwise
 */
bool scd41_is_initialized(const scd41_handle_t *handle);

/**
 * @brief Perform sensor self-test
 * 
 * Executes sensor self-test. Takes approximately 10 seconds.
 * 
 * @param handle SCD41 handle
 * @return true if self-test passed, false otherwise
 */
bool scd41_self_test(scd41_handle_t *handle);

#endif // SCD41_H
