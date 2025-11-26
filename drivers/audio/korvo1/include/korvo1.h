#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2s.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2s_port_t port;
    gpio_num_t din_io_num;
    gpio_num_t bclk_io_num;
    gpio_num_t ws_io_num;
    gpio_num_t mclk_io_num;
    int sample_rate_hz;
    int dma_buffer_count;
    int dma_buffer_len;
    i2s_channel_fmt_t channel_format;
} korvo1_config_t;

typedef struct {
    korvo1_config_t config;
    bool initialized;
    bool streaming;
} korvo1_t;

esp_err_t korvo1_init(korvo1_t *dev, const korvo1_config_t *config);
esp_err_t korvo1_start(korvo1_t *dev);
esp_err_t korvo1_stop(korvo1_t *dev);
esp_err_t korvo1_read(korvo1_t *dev, void *buffer, size_t bytes_to_read, size_t *bytes_read, TickType_t ticks_to_wait);
esp_err_t korvo1_deinit(korvo1_t *dev);

#ifdef __cplusplus
}
#endif
