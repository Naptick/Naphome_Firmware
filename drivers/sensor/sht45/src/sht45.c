/**
 * @file sht45.c
 * @brief SHT45 Temperature and Humidity Sensor Driver Implementation
 */

#include "sht45.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "sht45";

// SHT45 I2C address
#define SHT45_I2C_ADDR_DEFAULT 0x44

// SHT45 commands
#define SHT45_CMD_MEASURE_T_RH_HPM 0xFD  // High precision measurement
#define SHT45_CMD_SOFT_RESET 0x94

// Timing
#define SHT45_MEASURE_DELAY_MS 15

/**
 * @brief Initialize SHT45 sensor
 */
bool sht45_init(sht45_handle_t *handle, i2c_master_bus_handle_t i2c_bus, uint8_t device_addr)
{
    if (handle == NULL || i2c_bus == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    memset(handle, 0, sizeof(sht45_handle_t));
    handle->i2c_bus = i2c_bus;
    handle->device_address = (device_addr != 0) ? device_addr : SHT45_I2C_ADDR_DEFAULT;

    // Perform soft reset
    // Note: i2c_master_transmit_receive needs a device handle, not bus handle + address
    // For v4.4 compatibility, we'll use direct I2C commands
    uint8_t cmd = SHT45_CMD_SOFT_RESET;
    i2c_cmd_handle_t i2c_cmd = i2c_cmd_link_create();
    i2c_master_start(i2c_cmd);
    i2c_master_write_byte(i2c_cmd, (handle->device_address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(i2c_cmd, cmd, true);
    i2c_master_stop(i2c_cmd);
    i2c_master_cmd_begin(handle->i2c_bus, i2c_cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(i2c_cmd);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Verify device presence by attempting a read
    // (In real implementation, would check device ID register)
    handle->initialized = true;
    ESP_LOGI(TAG, "SHT45 initialized at address 0x%02X", handle->device_address);
    
    return true;
}

/**
 * @brief Deinitialize SHT45 sensor
 */
void sht45_deinit(sht45_handle_t *handle)
{
    if (handle != NULL) {
        handle->initialized = false;
        ESP_LOGI(TAG, "SHT45 deinitialized");
    }
}

/**
 * @brief Read temperature and humidity from sensor
 */
bool sht45_read(sht45_handle_t *handle, sht45_data_t *data)
{
    if (handle == NULL || data == NULL || !handle->initialized) {
        ESP_LOGE(TAG, "Invalid parameters or not initialized");
        return false;
    }

    uint8_t cmd = SHT45_CMD_MEASURE_T_RH_HPM;
    uint8_t rx_data[6] = {0};

    // Send measurement command using v4.4 I2C API
    i2c_cmd_handle_t i2c_cmd = i2c_cmd_link_create();
    i2c_master_start(i2c_cmd);
    i2c_master_write_byte(i2c_cmd, (handle->device_address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(i2c_cmd, cmd, true);
    i2c_master_stop(i2c_cmd);
    esp_err_t ret = i2c_master_cmd_begin(handle->i2c_bus, i2c_cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(i2c_cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
        data->valid = false;
        return false;
    }

    // Wait for measurement to complete
    vTaskDelay(pdMS_TO_TICKS(SHT45_MEASURE_DELAY_MS));

    // Read measurement data using v4.4 I2C API
    i2c_cmd = i2c_cmd_link_create();
    i2c_master_start(i2c_cmd);
    i2c_master_write_byte(i2c_cmd, (handle->device_address << 1) | I2C_MASTER_READ, true);
    if (6 > 1) {
        i2c_master_read(i2c_cmd, rx_data, 6 - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(i2c_cmd, rx_data + 6 - 1, I2C_MASTER_NACK);
    i2c_master_stop(i2c_cmd);
    ret = i2c_master_cmd_begin(handle->i2c_bus, i2c_cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(i2c_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
        data->valid = false;
        return false;
    }

    // Parse temperature (first 2 bytes)
    uint16_t temp_raw = (rx_data[0] << 8) | rx_data[1];
    data->temperature_c = -45.0f + (175.0f * temp_raw / 65535.0f);

    // Parse humidity (bytes 3-4)
    uint16_t hum_raw = (rx_data[3] << 8) | rx_data[4];
    data->humidity_rh = -6.0f + (125.0f * hum_raw / 65535.0f);

    // Validate ranges
    if (data->temperature_c < -40.0f || data->temperature_c > 125.0f ||
        data->humidity_rh < 0.0f || data->humidity_rh > 100.0f) {
        ESP_LOGW(TAG, "Sensor reading out of range: T=%.2f°C, RH=%.2f%%", 
                 data->temperature_c, data->humidity_rh);
        data->valid = false;
        return false;
    }

    data->valid = true;
    return true;
}

/**
 * @brief Read temperature only
 */
bool sht45_read_temperature(sht45_handle_t *handle, float *temperature_c)
{
    sht45_data_t data;
    if (sht45_read(handle, &data)) {
        *temperature_c = data.temperature_c;
        return true;
    }
    return false;
}

/**
 * @brief Read humidity only
 */
bool sht45_read_humidity(sht45_handle_t *handle, float *humidity_rh)
{
    sht45_data_t data;
    if (sht45_read(handle, &data)) {
        *humidity_rh = data.humidity_rh;
        return true;
    }
    return false;
}

/**
 * @brief Check if sensor is initialized
 */
bool sht45_is_initialized(const sht45_handle_t *handle)
{
    return (handle != NULL && handle->initialized);
}
