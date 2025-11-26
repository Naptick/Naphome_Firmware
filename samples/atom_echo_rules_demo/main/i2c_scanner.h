#pragma once

#include <stddef.h>
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_port_t port;
    gpio_num_t sda;
    gpio_num_t scl;
    uint32_t frequency_hz;
    const char *label;
} i2c_bus_config_t;

typedef void (*i2c_scanner_result_cb_t)(const i2c_bus_config_t *bus,
                                        uint8_t address,
                                        const char *friendly_name,
                                        void *ctx);

esp_err_t i2c_scanner_scan_bus_with_callback(const i2c_bus_config_t *bus,
                                             i2c_scanner_result_cb_t cb,
                                             void *ctx);
esp_err_t i2c_scanner_scan_bus(const i2c_bus_config_t *bus);
esp_err_t i2c_scanner_scan_all(const i2c_bus_config_t *buses, size_t count);

#ifdef __cplusplus
}
#endif
