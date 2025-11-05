/**
 * @file sht45.c
 * @brief SHT45 Temperature and Humidity Sensor Driver Implementation
 */

#include "sht45.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>

static const char *TAG = "sht45";

// SHT45 I2C address
#define SHT45_I2C_ADDR_DEFAULT 0x44

// SHT45 commands
#define SHT45_CMD_MEASURE_T_RH_HPM 0xFD  // High precision measurement
#define SHT45_CMD_SOFT_RESET 0x94

// Timing
#define SHT45_MEASURE_DELAY_MS 15

// CRC parameters for SHT45 (CRC-8 with polynomial 0x31)
#define SHT45_CRC_POLYNOMIAL 0x31
#define SHT45_CRC_INIT 0xFF

/**
 * @brief Calculate CRC-8 checksum for SHT45 data
 * 
 * @param data Pointer to data bytes
 * @param len Number of bytes
 * @return uint8_t CRC-8 checksum
 */
static uint8_t sht45_calculate_crc(const uint8_t *data, size_t len)
{
    uint8_t crc = SHT45_CRC_INIT;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ SHT45_CRC_POLYNOMIAL;
            } else {
                crc = (crc << 1);
            }
        }
    }
    
    return crc;
}

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

    // Configure I2C device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = handle->device_address,
        .scl_speed_hz = 100000,  // 100 kHz for SHT45
    };

    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &handle->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        return false;
    }

    // Perform soft reset
    uint8_t cmd = SHT45_CMD_SOFT_RESET;
    ret = i2c_master_transmit(handle->i2c_dev, &cmd, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Soft reset failed: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(handle->i2c_dev);
        return false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));

    handle->initialized = true;
    ESP_LOGI(TAG, "SHT45 initialized at address 0x%02X", handle->device_address);
    
    return true;
}

/**
 * @brief Deinitialize SHT45 sensor
 */
void sht45_deinit(sht45_handle_t *handle)
{
    if (handle != NULL && handle->initialized) {
        if (handle->i2c_dev != NULL) {
            i2c_master_bus_rm_device(handle->i2c_dev);
            handle->i2c_dev = NULL;
        }
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

    // Send measurement command
    esp_err_t ret = i2c_master_transmit(handle->i2c_dev, &cmd, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send measurement command: %s", esp_err_to_name(ret));
        data->valid = false;
        return false;
    }

    // Wait for measurement to complete
    vTaskDelay(pdMS_TO_TICKS(SHT45_MEASURE_DELAY_MS));

    // Read measurement data (6 bytes: T_msb, T_lsb, T_crc, RH_msb, RH_lsb, RH_crc)
    ret = i2c_master_receive(handle->i2c_dev, rx_data, 6, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read measurement data: %s", esp_err_to_name(ret));
        data->valid = false;
        return false;
    }

    // Validate temperature CRC
    uint8_t temp_crc = sht45_calculate_crc(&rx_data[0], 2);
    if (temp_crc != rx_data[2]) {
        ESP_LOGE(TAG, "Temperature CRC mismatch: expected 0x%02X, got 0x%02X", 
                 temp_crc, rx_data[2]);
        data->valid = false;
        return false;
    }

    // Validate humidity CRC
    uint8_t hum_crc = sht45_calculate_crc(&rx_data[3], 2);
    if (hum_crc != rx_data[5]) {
        ESP_LOGE(TAG, "Humidity CRC mismatch: expected 0x%02X, got 0x%02X", 
                 hum_crc, rx_data[5]);
        data->valid = false;
        return false;
    }

    // Parse temperature (first 2 bytes) - Formula from datasheet
    uint16_t temp_raw = (rx_data[0] << 8) | rx_data[1];
    data->temperature_c = -45.0f + (175.0f * temp_raw / 65535.0f);

    // Parse humidity (bytes 3-4) - Formula from datasheet
    uint16_t hum_raw = (rx_data[3] << 8) | rx_data[4];
    data->humidity_rh = -6.0f + (125.0f * hum_raw / 65535.0f);

    // Clamp humidity to valid range (datasheet specifies 0-100%)
    if (data->humidity_rh < 0.0f) {
        data->humidity_rh = 0.0f;
    } else if (data->humidity_rh > 100.0f) {
        data->humidity_rh = 100.0f;
    }

    // Validate temperature range (datasheet: -40°C to +125°C)
    if (data->temperature_c < -40.0f || data->temperature_c > 125.0f) {
        ESP_LOGW(TAG, "Temperature out of sensor range: %.2f°C", data->temperature_c);
        data->valid = false;
        return false;
    }

    data->valid = true;
    ESP_LOGD(TAG, "Read successful: T=%.2f°C, RH=%.2f%%", 
             data->temperature_c, data->humidity_rh);
    
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
