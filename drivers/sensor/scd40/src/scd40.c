#include "scd40.h"
#include "../../i2c_master_compat.h"  // For i2c_master_write_to_device, i2c_master_write_read_device

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "scd40";

#define SCD40_CMD_START_PERIODIC    0x21B1
#define SCD40_CMD_STOP_PERIODIC     0x3F86
#define SCD40_CMD_READ_MEASUREMENT  0xEC05
#define SCD40_CMD_FRC               0x362F

#define SCD40_CRC_POLY              0x31
#define SCD40_CRC_INIT              0xFF

__attribute__((weak)) esp_err_t scd40_i2c_transfer(i2c_port_t port,
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

static uint8_t scd40_calc_crc(const uint8_t *data, size_t len)
{
    uint8_t crc = SCD40_CRC_INIT;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ SCD40_CRC_POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static esp_err_t scd40_send_command(scd40_t *dev, uint16_t command)
{
    ESP_RETURN_ON_FALSE(dev && dev->initialized, ESP_ERR_INVALID_STATE, TAG, "device not initialized");
    if (dev->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = {
        (uint8_t)(command >> 8),
        (uint8_t)(command & 0xFF)
    };
    return i2c_master_transmit(dev->i2c_dev, buf, sizeof(buf), pdMS_TO_TICKS(SCD40_I2C_TIMEOUT_MS));
}

static esp_err_t scd40_write_with_arg(scd40_t *dev, uint16_t command, uint16_t arg)
{
    uint8_t buf[5] = {
        (uint8_t)(command >> 8),
        (uint8_t)(command & 0xFF),
        (uint8_t)(arg >> 8),
        (uint8_t)(arg & 0xFF),
        0
    };
    buf[4] = scd40_calc_crc(&buf[2], 2);
    if (dev->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(dev->i2c_dev, buf, sizeof(buf), pdMS_TO_TICKS(SCD40_I2C_TIMEOUT_MS));
}

static esp_err_t scd40_read_measurement_raw(scd40_t *dev, uint16_t *co2, uint16_t *temp, uint16_t *hum)
{
    uint8_t command[2] = {
        (uint8_t)(SCD40_CMD_READ_MEASUREMENT >> 8),
        (uint8_t)(SCD40_CMD_READ_MEASUREMENT & 0xFF)
    };
    uint8_t response[9] = {0};
    if (dev->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_transmit_receive(dev->i2c_dev, command, sizeof(command),
                                                 response, sizeof(response), pdMS_TO_TICKS(SCD40_I2C_TIMEOUT_MS));
    ESP_RETURN_ON_ERROR(err, TAG, "i2c read failed");

    for (size_t i = 0; i < 3; ++i) {
        uint8_t crc = scd40_calc_crc(&response[i * 3], 2);
        if (crc != response[i * 3 + 2]) {
            ESP_LOGE(TAG, "crc mismatch on block %d", (int)i);
            return ESP_ERR_INVALID_CRC;
        }
    }

    *co2 = ((uint16_t)response[0] << 8) | response[1];
    *temp = ((uint16_t)response[3] << 8) | response[4];
    *hum = ((uint16_t)response[6] << 8) | response[7];
    return ESP_OK;
}

static void scd40_measurement_convert(uint16_t raw_temp, uint16_t raw_hum,
                                      float *temperature_c, float *humidity_rh)
{
    if (temperature_c) {
        *temperature_c = -45.0f + (175.0f * (float)raw_temp) / 65535.0f;
    }
    if (humidity_rh) {
        *humidity_rh = 100.0f * (float)raw_hum / 65535.0f;
    }
}

esp_err_t scd40_init(scd40_t *dev, const scd40_config_t *config)
{
    ESP_RETURN_ON_FALSE(dev && config, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    memcpy(&dev->config, config, sizeof(scd40_config_t));
    dev->i2c_dev = NULL;  // Will be set by sensor_integration.c

    // I2C bus should already be initialized by sensor_integration.c using new API
    // Device handle will be created and stored by sensor_integration.c

    dev->initialized = true;
    dev->periodic_measurement = false;

    if (config->rst_io_num >= 0) {
        uint64_t pin_mask = 1ULL << config->rst_io_num;
        gpio_config_t rst_conf = {
            .pin_bit_mask = pin_mask,
            .mode = GPIO_MODE_OUTPUT,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&rst_conf);
        gpio_set_level(config->rst_io_num, 1);
    }

    return ESP_OK;
}

esp_err_t scd40_start_periodic_measurement(scd40_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "dev is null");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "device not initialized");

    ESP_RETURN_ON_ERROR(scd40_send_command(dev, SCD40_CMD_START_PERIODIC), TAG, "start measurement failed");
    dev->periodic_measurement = true;
    return ESP_OK;
}

esp_err_t scd40_stop_periodic_measurement(scd40_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "dev is null");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "device not initialized");

    ESP_RETURN_ON_ERROR(scd40_send_command(dev, SCD40_CMD_STOP_PERIODIC), TAG, "stop measurement failed");
    dev->periodic_measurement = false;
    return ESP_OK;
}

esp_err_t scd40_read_measurement(scd40_t *dev, scd40_measurement_t *measurement)
{
    ESP_RETURN_ON_FALSE(dev && measurement, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "device not initialized");

    uint16_t raw_co2 = 0;
    uint16_t raw_temp = 0;
    uint16_t raw_hum = 0;
    ESP_RETURN_ON_ERROR(scd40_read_measurement_raw(dev, &raw_co2, &raw_temp, &raw_hum), TAG, "read measurement failed");

    measurement->co2_ppm = raw_co2;
    scd40_measurement_convert(raw_temp, raw_hum, &measurement->temperature_c, &measurement->humidity_rh);
    return ESP_OK;
}

esp_err_t scd40_perform_forced_recalibration(scd40_t *dev, uint16_t target_co2_ppm, uint16_t *frc_result)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "dev is null");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "device not initialized");

    ESP_RETURN_ON_ERROR(scd40_write_with_arg(dev, SCD40_CMD_FRC, target_co2_ppm), TAG, "write frc command failed");

    uint8_t response[3] = {0};
    uint8_t cmd[2] = { (uint8_t)(SCD40_CMD_FRC >> 8), (uint8_t)(SCD40_CMD_FRC & 0xFF) };
    if (dev->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(dev->i2c_dev, cmd, sizeof(cmd), response, sizeof(response),
                                                     pdMS_TO_TICKS(SCD40_I2C_TIMEOUT_MS)), TAG, "read frc result failed");

    if (scd40_calc_crc(response, 2) != response[2]) {
        return ESP_ERR_INVALID_CRC;
    }

    if (frc_result) {
        *frc_result = ((uint16_t)response[0] << 8) | response[1];
    }
    return ESP_OK;
}

esp_err_t scd40_deinit(scd40_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "dev is null");
    if (!dev->initialized) {
        return ESP_OK;
    }
    // Remove device from bus if it was added
    if (dev->i2c_dev != NULL) {
        i2c_master_bus_rm_device(dev->i2c_dev);
        dev->i2c_dev = NULL;
    }
    dev->initialized = false;
    dev->periodic_measurement = false;
    return ESP_OK;
}
