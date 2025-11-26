#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // BME280 data
    bool bme280_available;
    float bme280_temp_c;
    float bme280_humidity_rh;
    float bme280_pressure_hpa;
    
    // BME680 data
    bool bme680_available;
    float bme680_temp_c;
    float bme680_humidity_rh;
    float bme680_pressure_hpa;
    uint32_t bme680_gas_resistance;
    
    // SHT41 data (using SHT45 driver)
    bool sht41_available;
    float sht41_temp_c;
    float sht41_humidity_rh;
    
    // SGP40 data
    bool sgp40_available;
    uint16_t sgp40_voc_index;
    
    // AS7341 data
    bool as7341_available;
    uint16_t as7341_channels[11];  // 8 spectral + clear + NIR + flicker
} sensor_readings_t;

/**
 * Initialize sensor reader
 */
esp_err_t sensor_reader_init(void);

/**
 * Read all sensors
 */
esp_err_t sensor_reader_read_all(sensor_readings_t *readings);

/**
 * Print sensor readings to console
 */
void sensor_reader_print(const sensor_readings_t *readings);

#ifdef __cplusplus
}
#endif
