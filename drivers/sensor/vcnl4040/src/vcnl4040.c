#include "vcnl4040.h"
#include "../../i2c_master_compat.h"  // For i2c_master_write_to_device, i2c_master_write_read_device

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "vcnl4040";

#define VCNL4040_REG_ALS_CONF        0x00
#define VCNL4040_REG_ALS_THDH        0x01
#define VCNL4040_REG_ALS_THDL        0x02
#define VCNL4040_REG_PS_CONF1_2      0x03
#define VCNL4040_REG_PS_CONF3        0x04
#define VCNL4040_REG_PS_MS           0x05
#define VCNL4040_REG_PS_CANC         0x06
#define VCNL4040_REG_PS_THDL         0x07
#define VCNL4040_REG_PS_THDH         0x08
#define VCNL4040_REG_PS_DATA         0x08
#define VCNL4040_REG_ALS_DATA        0x09

#define VCNL4040_ALS_IT_80MS         (0x01 << 6)
#define VCNL4040_ALS_PERS_1          (0x00 << 4)
#define VCNL4040_ALS_ACTIVE          (1 << 0)

#define VCNL4040_PS_PERS_1           (0x00 << 4)
#define VCNL4040_PS_ACTIVE           (1 << 0)
#define VCNL4040_LED_I_SHIFT         8

__attribute__((weak)) esp_err_t vcnl4040_i2c_transfer(i2c_port_t port,
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

static esp_err_t vcnl4040_write_reg(vcnl4040_t *dev, uint8_t reg, uint16_t value)
{
    if (dev->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[3] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF)
    };
    return i2c_master_transmit(dev->i2c_dev, buf, sizeof(buf), pdMS_TO_TICKS(VCNL4040_I2C_TIMEOUT_MS));
}

static esp_err_t vcnl4040_read_reg(vcnl4040_t *dev, uint8_t reg, uint16_t *value)
{
    if (dev->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg_addr = reg;
    uint8_t buf[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(dev->i2c_dev, &reg_addr, sizeof(reg_addr),
                                                 buf, sizeof(buf), pdMS_TO_TICKS(VCNL4040_I2C_TIMEOUT_MS));
    ESP_RETURN_ON_ERROR(err, TAG, "read register failed");
    *value = ((uint16_t)buf[0] << 8) | buf[1];
    return ESP_OK;
}

esp_err_t vcnl4040_init(vcnl4040_t *dev, const vcnl4040_config_t *config)
{
    ESP_RETURN_ON_FALSE(dev && config, ESP_ERR_INVALID_ARG, TAG, "invalid arguments");
    memcpy(&dev->config, config, sizeof(vcnl4040_config_t));
    dev->i2c_dev = NULL;  // Will be set by sensor_integration.c

    // I2C bus should already be initialized by sensor_integration.c using new API
    // Device handle will be created and stored by sensor_integration.c

    uint16_t als_conf = VCNL4040_ALS_IT_80MS | VCNL4040_ALS_PERS_1 | VCNL4040_ALS_ACTIVE;
    ESP_RETURN_ON_ERROR(vcnl4040_write_reg(dev, VCNL4040_REG_ALS_CONF, als_conf), TAG, "als config failed");

    uint16_t ps_conf1_2 = (config->prox_rate << 1) | VCNL4040_PS_PERS_1 | VCNL4040_PS_ACTIVE;
    ESP_RETURN_ON_ERROR(vcnl4040_write_reg(dev, VCNL4040_REG_PS_CONF1_2, ps_conf1_2), TAG, "ps config failed");

    ESP_RETURN_ON_ERROR(vcnl4040_set_led_current(dev, config->led_current_ma), TAG, "led current set failed");

    dev->initialized = true;
    return ESP_OK;
}

esp_err_t vcnl4040_read_ambient_lux(vcnl4040_t *dev, uint16_t *lux_raw)
{
    ESP_RETURN_ON_FALSE(dev && lux_raw, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    return vcnl4040_read_reg(dev, VCNL4040_REG_ALS_DATA, lux_raw);
}

esp_err_t vcnl4040_read_proximity(vcnl4040_t *dev, uint16_t *proximity_raw)
{
    ESP_RETURN_ON_FALSE(dev && proximity_raw, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    return vcnl4040_read_reg(dev, VCNL4040_REG_PS_DATA, proximity_raw);
}

esp_err_t vcnl4040_set_led_current(vcnl4040_t *dev, uint8_t led_current_ma)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "dev null");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (led_current_ma > 200) {
        led_current_ma = 200;
    }
    uint16_t value = ((uint16_t)led_current_ma << VCNL4040_LED_I_SHIFT);
    return vcnl4040_write_reg(dev, VCNL4040_REG_PS_MS, value);
}

esp_err_t vcnl4040_deinit(vcnl4040_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "dev null");
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
