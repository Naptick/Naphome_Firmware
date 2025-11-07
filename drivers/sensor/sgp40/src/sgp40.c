/**
 * @file sgp40.c
 * @brief SGP40 VOC Sensor Driver Implementation
 */

#include "sgp40.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>

static const char *TAG = "sgp40";

// SGP40 I2C address
#define SGP40_I2C_ADDR_DEFAULT 0x59

// SGP40 commands (MSB first)
#define SGP40_CMD_MEASURE_RAW 0x260F
#define SGP40_CMD_SOFT_RESET 0x0006
#define SGP40_CMD_HEATER_OFF 0x3615
#define SGP40_CMD_SELF_TEST 0x280E

// Timing
#define SGP40_MEASURE_DELAY_MS 30
#define SGP40_SELF_TEST_DELAY_MS 250
#define SGP40_RESET_DELAY_MS 10

// CRC parameters for SGP40
#define SGP40_CRC_POLYNOMIAL 0x31
#define SGP40_CRC_INIT 0xFF

/**
 * @brief Calculate CRC-8 checksum for SGP40 data
 * 
 * @param data Data bytes to calculate CRC for
 * @param len Number of bytes
 * @return CRC-8 checksum
 */
static uint8_t sgp40_calculate_crc(const uint8_t *data, size_t len)
{
    uint8_t crc = SGP40_CRC_INIT;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ SGP40_CRC_POLYNOMIAL;
            } else {
                crc = crc << 1;
            }
        }
    }
    
    return crc;
}

/**
 * @brief Convert temperature to SGP40 format
 * 
 * @param temperature_c Temperature in Celsius
 * @return Temperature in SGP40 format
 */
static uint16_t sgp40_convert_temperature(float temperature_c)
{
    // Temperature = -45 + 175 * (value / 65535)
    // value = (temperature + 45) * 65535 / 175
    return (uint16_t)((temperature_c + 45.0f) * 65535.0f / 175.0f);
}

/**
 * @brief Convert humidity to SGP40 format
 * 
 * @param humidity_rh Relative humidity in %
 * @return Humidity in SGP40 format
 */
static uint16_t sgp40_convert_humidity(float humidity_rh)
{
    // Humidity = 100 * (value / 65535)
    // value = humidity * 65535 / 100
    return (uint16_t)(humidity_rh * 65535.0f / 100.0f);
}

/**
 * @brief Initialize SGP40 sensor
 */
bool sgp40_init(sgp40_handle_t *handle, i2c_master_bus_handle_t i2c_bus, uint8_t device_addr)
{
    if (handle == NULL || i2c_bus == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    memset(handle, 0, sizeof(sgp40_handle_t));
    handle->i2c_bus = i2c_bus;
    handle->device_address = (device_addr != 0) ? device_addr : SGP40_I2C_ADDR_DEFAULT;

    // Perform soft reset
    uint8_t cmd[2] = {
        (SGP40_CMD_SOFT_RESET >> 8) & 0xFF,
        SGP40_CMD_SOFT_RESET & 0xFF
    };
    
    esp_err_t ret = i2c_master_transmit(handle->i2c_bus, handle->device_address, cmd, 2, -1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Soft reset command failed: %s", esp_err_to_name(ret));
    }
    
    vTaskDelay(pdMS_TO_TICKS(SGP40_RESET_DELAY_MS));

    // Mark as initialized
    handle->initialized = true;
    ESP_LOGI(TAG, "SGP40 initialized at address 0x%02X", handle->device_address);
    
    return true;
}

/**
 * @brief Deinitialize SGP40 sensor
 */
void sgp40_deinit(sgp40_handle_t *handle)
{
    if (handle != NULL) {
        // Turn off heater before deinit
        uint8_t cmd[2] = {
            (SGP40_CMD_HEATER_OFF >> 8) & 0xFF,
            SGP40_CMD_HEATER_OFF & 0xFF
        };
        i2c_master_transmit(handle->i2c_bus, handle->device_address, cmd, 2, -1);
        
        handle->initialized = false;
        ESP_LOGI(TAG, "SGP40 deinitialized");
    }
}

/**
 * @brief Read raw VOC signal with compensation
 */
bool sgp40_read_compensated(sgp40_handle_t *handle, sgp40_data_t *data,
                            float temperature_c, float humidity_rh)
{
    if (handle == NULL || data == NULL || !handle->initialized) {
        ESP_LOGE(TAG, "Invalid parameters or not initialized");
        return false;
    }

    // Convert compensation parameters
    uint16_t temp_raw = sgp40_convert_temperature(temperature_c);
    uint16_t hum_raw = sgp40_convert_humidity(humidity_rh);

    // Prepare command with compensation parameters
    uint8_t cmd[8];
    cmd[0] = (SGP40_CMD_MEASURE_RAW >> 8) & 0xFF;
    cmd[1] = SGP40_CMD_MEASURE_RAW & 0xFF;
    
    // Temperature parameter (2 bytes + CRC)
    cmd[2] = (temp_raw >> 8) & 0xFF;
    cmd[3] = temp_raw & 0xFF;
    cmd[4] = sgp40_calculate_crc(&cmd[2], 2);
    
    // Humidity parameter (2 bytes + CRC)
    cmd[5] = (hum_raw >> 8) & 0xFF;
    cmd[6] = hum_raw & 0xFF;
    cmd[7] = sgp40_calculate_crc(&cmd[5], 2);

    // Send measurement command
    esp_err_t ret = i2c_master_transmit(handle->i2c_bus, handle->device_address, cmd, 8, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C transmit failed: %s", esp_err_to_name(ret));
        data->valid = false;
        return false;
    }

    // Wait for measurement to complete
    vTaskDelay(pdMS_TO_TICKS(SGP40_MEASURE_DELAY_MS));

    // Read measurement result (2 bytes data + 1 byte CRC)
    uint8_t rx_data[3] = {0};
    ret = i2c_master_receive(handle->i2c_bus, handle->device_address, rx_data, 3, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C receive failed: %s", esp_err_to_name(ret));
        data->valid = false;
        return false;
    }

    // Verify CRC
    uint8_t crc = sgp40_calculate_crc(rx_data, 2);
    if (crc != rx_data[2]) {
        ESP_LOGE(TAG, "CRC mismatch: expected 0x%02X, got 0x%02X", crc, rx_data[2]);
        data->valid = false;
        return false;
    }

    // Parse VOC raw value
    data->voc_raw = (rx_data[0] << 8) | rx_data[1];
    data->voc_index = -1;  // VOC index requires additional algorithm
    data->valid = true;

    return true;
}

/**
 * @brief Read raw VOC signal from sensor
 */
bool sgp40_read(sgp40_handle_t *handle, sgp40_data_t *data)
{
    // Use default compensation values (25°C, 50% RH)
    return sgp40_read_compensated(handle, data, 25.0f, 50.0f);
}

/**
 * @brief Read raw VOC value only
 */
bool sgp40_read_raw(sgp40_handle_t *handle, uint16_t *voc_raw)
{
    sgp40_data_t data;
    if (sgp40_read(handle, &data)) {
        *voc_raw = data.voc_raw;
        return true;
    }
    return false;
}

/**
 * @brief Perform self-test on sensor
 */
bool sgp40_self_test(sgp40_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        ESP_LOGE(TAG, "Invalid parameters or not initialized");
        return false;
    }

    // Send self-test command
    uint8_t cmd[2] = {
        (SGP40_CMD_SELF_TEST >> 8) & 0xFF,
        SGP40_CMD_SELF_TEST & 0xFF
    };
    
    esp_err_t ret = i2c_master_transmit(handle->i2c_bus, handle->device_address, cmd, 2, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Self-test command failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Wait for self-test to complete
    vTaskDelay(pdMS_TO_TICKS(SGP40_SELF_TEST_DELAY_MS));

    // Read self-test result (2 bytes data + 1 byte CRC)
    uint8_t rx_data[3] = {0};
    ret = i2c_master_receive(handle->i2c_bus, handle->device_address, rx_data, 3, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Self-test result read failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Verify CRC
    uint8_t crc = sgp40_calculate_crc(rx_data, 2);
    if (crc != rx_data[2]) {
        ESP_LOGE(TAG, "Self-test CRC mismatch");
        return false;
    }

    // Parse result (0xD400 = pass)
    uint16_t result = (rx_data[0] << 8) | rx_data[1];
    bool passed = (result == 0xD400);
    
    if (passed) {
        ESP_LOGI(TAG, "Self-test passed");
    } else {
        ESP_LOGE(TAG, "Self-test failed: 0x%04X", result);
    }
    
    return passed;
}

/**
 * @brief Check if sensor is initialized
 */
bool sgp40_is_initialized(const sgp40_handle_t *handle)
{
    return (handle != NULL && handle->initialized);
}
