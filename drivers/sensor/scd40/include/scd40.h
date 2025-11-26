#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
// Use i2c_port_t from HAL to avoid including old driver/i2c.h (conflicts with driver_ng)
#include "hal/i2c_types.h"
#include "driver/gpio.h"
#include "../../i2c_master_compat.h"  // For i2c_master_dev_handle_t

#ifdef __cplusplus
extern "C" {
#endif

#define SCD40_DEFAULT_ADDR        0x62
#define SCD40_I2C_TIMEOUT_MS      1000
#define SCD40_MEASUREMENT_DELAY_MS 5000

typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t sda_io_num;
    gpio_num_t scl_io_num;
    uint32_t i2c_clk_speed_hz;
    gpio_num_t rst_io_num;
} scd40_config_t;

typedef struct {
    scd40_config_t config;
    bool initialized;
    bool periodic_measurement;
    i2c_master_dev_handle_t i2c_dev;  // Device handle for new I2C API
} scd40_t;

typedef struct {
    uint16_t co2_ppm;
    float temperature_c;
    float humidity_rh;
} scd40_measurement_t;

esp_err_t scd40_init(scd40_t *dev, const scd40_config_t *config);
esp_err_t scd40_start_periodic_measurement(scd40_t *dev);
esp_err_t scd40_stop_periodic_measurement(scd40_t *dev);
esp_err_t scd40_read_measurement(scd40_t *dev, scd40_measurement_t *measurement);
esp_err_t scd40_perform_forced_recalibration(scd40_t *dev, uint16_t target_co2_ppm, uint16_t *frc_result);
esp_err_t scd40_deinit(scd40_t *dev);

#ifdef __cplusplus
}
#endif
