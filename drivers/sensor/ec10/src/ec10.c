#include "ec10.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ec10";

#define EC10_HEADER_HIGH        0x42
#define EC10_HEADER_LOW         0x4D

__attribute__((weak)) int ec10_uart_read_bytes(uart_port_t uart_num, uint8_t *buf, uint32_t length, TickType_t ticks_to_wait)
{
    return uart_read_bytes(uart_num, buf, length, ticks_to_wait);
}

static uint16_t ec10_checksum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return (uint16_t)sum;
}

esp_err_t ec10_init(ec10_t *dev, const ec10_config_t *config)
{
    ESP_RETURN_ON_FALSE(dev && config, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    memcpy(&dev->config, config, sizeof(ec10_config_t));

    uart_config_t uart_conf = {
        .baud_rate = config->baud_rate > 0 ? config->baud_rate : 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(config->uart_port,
                                            config->rx_buffer_size ? config->rx_buffer_size : 128,
                                            0, 0, NULL, 0), TAG, "driver install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(config->uart_port, &uart_conf), TAG, "param config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(config->uart_port, config->tx_io_num, config->rx_io_num,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "set pin failed");

    uart_flush_input(config->uart_port);
    dev->initialized = true;
    return ESP_OK;
}

static esp_err_t ec10_parse_frame(const uint8_t *frame, size_t len, ec10_measurement_t *measurement)
{
    ESP_RETURN_ON_FALSE(len >= EC10_FRAME_LENGTH, ESP_ERR_INVALID_SIZE, TAG, "frame too short");
    ESP_RETURN_ON_FALSE(frame[0] == EC10_HEADER_HIGH && frame[1] == EC10_HEADER_LOW, ESP_ERR_INVALID_RESPONSE, TAG, "invalid header");

    uint16_t frame_length = ((uint16_t)frame[2] << 8) | frame[3];
    if (frame_length + 4 != EC10_FRAME_LENGTH) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t expected_checksum = ((uint16_t)frame[EC10_FRAME_LENGTH - 2] << 8) | frame[EC10_FRAME_LENGTH - 1];
    uint16_t calculated_checksum = ec10_checksum(frame, EC10_FRAME_LENGTH - 2);
    if (expected_checksum != calculated_checksum) {
        return ESP_ERR_INVALID_CRC;
    }

    measurement->pm1_0_ug_m3 = ((uint16_t)frame[4] << 8) | frame[5];
    measurement->pm2_5_ug_m3 = ((uint16_t)frame[6] << 8) | frame[7];
    measurement->pm10_ug_m3 = ((uint16_t)frame[8] << 8) | frame[9];
    return ESP_OK;
}

esp_err_t ec10_read_measurement(ec10_t *dev, ec10_measurement_t *measurement, TickType_t ticks_to_wait)
{
    ESP_RETURN_ON_FALSE(dev && measurement, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    uint8_t buffer[EC10_FRAME_LENGTH] = {0};
    int read = ec10_uart_read_bytes(dev->config.uart_port, buffer, sizeof(buffer), ticks_to_wait);
    if (read < 0) {
        return ESP_FAIL;
    }
    if ((size_t)read < EC10_FRAME_LENGTH) {
        ESP_LOGW(TAG, "partial frame read: %d", read);
        return ESP_ERR_INVALID_SIZE;
    }

    return ec10_parse_frame(buffer, sizeof(buffer), measurement);
}

esp_err_t ec10_deinit(ec10_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    if (!dev->initialized) {
        return ESP_OK;
    }
    uart_driver_delete(dev->config.uart_port);
    dev->initialized = false;
    return ESP_OK;
}
