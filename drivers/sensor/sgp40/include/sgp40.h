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

#define SGP40_DEFAULT_ADDR         0x59
#define SGP40_I2C_TIMEOUT_MS       1000
#define SGP40_RAW_DATA_WORDS       3

typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t sda_io_num;
    gpio_num_t scl_io_num;
    uint32_t i2c_clk_speed_hz;
} sgp40_config_t;

typedef struct {
    sgp40_config_t config;
    bool initialized;
    i2c_master_dev_handle_t i2c_dev;  // Device handle for new I2C API
} sgp40_t;

typedef struct {
    uint16_t voc_ticks;
} sgp40_raw_data_t;

esp_err_t sgp40_init(sgp40_t *dev, const sgp40_config_t *config);
esp_err_t sgp40_measure_raw(sgp40_t *dev, uint16_t humidity_ticks, uint16_t temperature_ticks, sgp40_raw_data_t *data);
esp_err_t sgp40_perform_self_test(sgp40_t *dev, bool *passed);
esp_err_t sgp40_deinit(sgp40_t *dev);

#ifdef __cplusplus
}
#endif
