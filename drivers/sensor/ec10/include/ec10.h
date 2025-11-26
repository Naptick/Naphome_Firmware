#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EC10_FRAME_LENGTH          32
#define EC10_UART_TIMEOUT_MS       100

typedef struct {
    uart_port_t uart_port;
    gpio_num_t tx_io_num;
    gpio_num_t rx_io_num;
    int baud_rate;
    uint8_t rx_buffer_size;
} ec10_config_t;

typedef struct {
    uint16_t pm1_0_ug_m3;
    uint16_t pm2_5_ug_m3;
    uint16_t pm10_ug_m3;
} ec10_measurement_t;

typedef struct {
    ec10_config_t config;
    bool initialized;
} ec10_t;

esp_err_t ec10_init(ec10_t *dev, const ec10_config_t *config);
esp_err_t ec10_read_measurement(ec10_t *dev, ec10_measurement_t *measurement, TickType_t ticks_to_wait);
esp_err_t ec10_deinit(ec10_t *dev);

#ifdef __cplusplus
}
#endif
