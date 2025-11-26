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

#define VCNL4040_DEFAULT_ADDR        0x60
#define VCNL4040_I2C_TIMEOUT_MS      1000

typedef enum {
    VCNL4040_PROX_RATE_1_95_SPS = 0,
    VCNL4040_PROX_RATE_3_9_SPS,
    VCNL4040_PROX_RATE_7_8_SPS,
    VCNL4040_PROX_RATE_16_3_SPS,
    VCNL4040_PROX_RATE_31_3_SPS,
    VCNL4040_PROX_RATE_62_5_SPS,
    VCNL4040_PROX_RATE_125_SPS,
    VCNL4040_PROX_RATE_250_SPS
} vcnl4040_prox_rate_t;

typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t sda_io_num;
    gpio_num_t scl_io_num;
    uint32_t i2c_clk_speed_hz;
    uint8_t led_current_ma;
    vcnl4040_prox_rate_t prox_rate;
} vcnl4040_config_t;

typedef struct {
    vcnl4040_config_t config;
    bool initialized;
    i2c_master_dev_handle_t i2c_dev;  // Device handle for new I2C API
} vcnl4040_t;

esp_err_t vcnl4040_init(vcnl4040_t *dev, const vcnl4040_config_t *config);
esp_err_t vcnl4040_read_ambient_lux(vcnl4040_t *dev, uint16_t *lux_raw);
esp_err_t vcnl4040_read_proximity(vcnl4040_t *dev, uint16_t *proximity_raw);
esp_err_t vcnl4040_set_led_current(vcnl4040_t *dev, uint8_t led_current_ma);
esp_err_t vcnl4040_deinit(vcnl4040_t *dev);

#ifdef __cplusplus
}
#endif
