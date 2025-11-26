#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sensor integration data structure
 */
typedef struct {
    float temperature_c;        ///< Temperature from SHT45 (°C)
    float humidity_rh;          ///< Humidity from SHT45 (%)
    uint16_t voc_index;         ///< VOC index from SGP40
    float co2_ppm;              ///< CO2 from SCD40 (ppm)
    float temperature_co2_c;    ///< Temperature from SCD40 (°C)
    float humidity_co2_rh;       ///< Humidity from SCD40 (%)
    uint16_t ambient_lux;       ///< Ambient light from VCNL4040 (lux)
    uint16_t proximity;         ///< Proximity from VCNL4040
    float ec_ms_per_cm;         ///< PM2.5 from EC10 (μg/m³) - stored in ec_ms_per_cm field
    bool sht45_available;       ///< SHT45 sensor available
    bool sgp40_available;       ///< SGP40 sensor available
    bool scd40_available;       ///< SCD40 sensor available
    bool vcnl4040_available;    ///< VCNL4040 sensor available
    bool ec10_available;        ///< EC10 sensor available
    uint32_t last_update_ms;    ///< Last update timestamp (ms)
} sensor_integration_data_t;

/**
 * @brief Initialize sensor integration system
 * @return ESP_OK on success
 */
esp_err_t sensor_integration_init(void);

/**
 * @brief Start sensor sampling at 1Hz
 * @return ESP_OK on success
 */
esp_err_t sensor_integration_start(void);

/**
 * @brief Stop sensor sampling
 * @return ESP_OK on success
 */
esp_err_t sensor_integration_stop(void);

/**
 * @brief Get current sensor data
 * @return Sensor data structure
 */
sensor_integration_data_t sensor_integration_get_data(void);

#ifdef __cplusplus
}
#endif
