#include "sgp40.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "../../i2c_master_compat.h"  // For i2c_master_write_to_device, i2c_master_write_read_device
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sgp40";

#define SGP40_CMD_MEASURE_RAW        0x260F
#define SGP40_CMD_SELF_TEST          0x280E

#define SGP40_CRC_POLY               0x31
#define SGP40_CRC_INIT               0xFF

__attribute__((weak)) esp_err_t sgp40_i2c_transfer(i2c_port_t port,
                                                   uint8_t addr,
                                                   const uint8_t *write_buf,
                                                   size_t write_size,
                                                   uint8_t *read_buf,
                                                   size_t read_size,
                                                   TickType_t ticks_to_wait)
{
    // This weak function is overridden in sensor_integration.c
    // It should not be called directly - use the device handle instead
    return ESP_ERR_NOT_SUPPORTED;
}

static uint8_t sgp40_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = SGP40_CRC_INIT;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ SGP40_CRC_POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

esp_err_t sgp40_init(sgp40_t *dev, const sgp40_config_t *config)
{
    ESP_RETURN_ON_FALSE(dev && config, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    memcpy(&dev->config, config, sizeof(sgp40_config_t));
    dev->i2c_dev = NULL;  // Will be set by sensor_integration.c

    // I2C bus should already be initialized by sensor_integration.c using new API
    // Device handle will be created and stored by sensor_integration.c

    dev->initialized = true;
    return ESP_OK;
}

static esp_err_t sgp40_write_command_with_args(sgp40_t *dev, uint16_t command,
                                               const uint16_t *args, size_t arg_words)
{
    uint8_t buf[2 + (3 * arg_words)];
    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFF);
    for (size_t i = 0; i < arg_words; ++i) {
        uint8_t msb = (uint8_t)(args[i] >> 8);
        uint8_t lsb = (uint8_t)(args[i] & 0xFF);
        buf[2 + i * 3] = msb;
        buf[2 + i * 3 + 1] = lsb;
        buf[2 + i * 3 + 2] = sgp40_crc8(&buf[2 + i * 3], 2);
    }
    if (dev->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(dev->i2c_dev, buf, sizeof(buf), pdMS_TO_TICKS(SGP40_I2C_TIMEOUT_MS));
}

esp_err_t sgp40_measure_raw(sgp40_t *dev, uint16_t humidity_ticks, uint16_t temperature_ticks, sgp40_raw_data_t *data)
{
    ESP_RETURN_ON_FALSE(dev && data, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    uint16_t args[2] = { humidity_ticks, temperature_ticks };
    ESP_RETURN_ON_ERROR(sgp40_write_command_with_args(dev, SGP40_CMD_MEASURE_RAW, args, 2), TAG, "send measure command");

    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t read_buf[3] = {0};
    if (dev->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_receive(dev->i2c_dev, read_buf, sizeof(read_buf), pdMS_TO_TICKS(SGP40_I2C_TIMEOUT_MS));
    ESP_RETURN_ON_ERROR(err, TAG, "read raw data failed");

    if (sgp40_crc8(read_buf, 2) != read_buf[2]) {
        return ESP_ERR_INVALID_CRC;
    }

    data->voc_ticks = ((uint16_t)read_buf[0] << 8) | read_buf[1];
    return ESP_OK;
}

esp_err_t sgp40_perform_self_test(sgp40_t *dev, bool *passed)
{
    ESP_RETURN_ON_FALSE(dev && passed, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    ESP_RETURN_ON_ERROR(sgp40_write_command_with_args(dev, SGP40_CMD_SELF_TEST, NULL, 0), TAG, "send self test");
    vTaskDelay(pdMS_TO_TICKS(250));

    uint8_t response[3] = {0};
    if (dev->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(i2c_master_receive(dev->i2c_dev, response, sizeof(response), pdMS_TO_TICKS(SGP40_I2C_TIMEOUT_MS)), TAG, "read self test result");

    if (sgp40_crc8(response, 2) != response[2]) {
        return ESP_ERR_INVALID_CRC;
    }

    *passed = (((uint16_t)response[0] << 8) | response[1]) == 0xD400;
    return ESP_OK;
}

esp_err_t sgp40_deinit(sgp40_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    if (!dev->initialized) {
        return ESP_OK;
    }
    // Remove device from bus if it was added
    if (dev->i2c_dev != NULL) {
        i2c_master_bus_rm_device(dev->i2c_dev);
        dev->i2c_dev = NULL;
    }
    dev->initialized = false;
    return ESP_OK;
}
