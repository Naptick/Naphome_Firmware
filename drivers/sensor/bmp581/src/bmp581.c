/**
 * @file bmp581.c
 * @brief BMP581 Barometric Pressure Sensor Driver Implementation
 */

#include "bmp581.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "bmp581";

// BMP581 I2C addresses
#define BMP581_I2C_ADDR_DEFAULT 0x46
#define BMP581_I2C_ADDR_ALT     0x47

// BMP581 register addresses
#define BMP581_REG_CHIP_ID      0x01
#define BMP581_REG_CMD          0x7E
#define BMP581_REG_PRESS_DATA   0x1D  // Pressure data (3 bytes)
#define BMP581_REG_TEMP_DATA    0x20  // Temperature data (3 bytes)
#define BMP581_REG_OSR_CONFIG   0x36  // Oversampling and power control
#define BMP581_REG_ODR_CONFIG   0x37  // Output data rate configuration

// BMP581 commands
#define BMP581_CMD_SOFT_RESET   0xB6
#define BMP581_CMD_NVM_READ     0xA0

// BMP581 chip ID
#define BMP581_CHIP_ID          0x50

// BMP581 power modes
#define BMP581_MODE_STANDBY     0x00
#define BMP581_MODE_NORMAL      0x01
#define BMP581_MODE_FORCED      0x02

// Timing
#define BMP581_RESET_DELAY_MS   10
#define BMP581_MEASURE_DELAY_MS 20

/**
 * @brief Write single byte to BMP581 register
 */
static esp_err_t bmp581_write_register(bmp581_handle_t *handle, uint8_t reg, uint8_t value)
{
    uint8_t tx_data[2] = {reg, value};
    return i2c_master_transmit(handle->i2c_bus, handle->device_address, tx_data, 2, -1);
}

/**
 * @brief Read bytes from BMP581 register
 */
static esp_err_t bmp581_read_register(bmp581_handle_t *handle, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(handle->i2c_bus, handle->device_address, &reg, 1, data, len, -1);
}

/**
 * @brief Verify chip ID
 */
static bool bmp581_verify_chip_id(bmp581_handle_t *handle)
{
    uint8_t chip_id = 0;
    esp_err_t ret = bmp581_read_register(handle, BMP581_REG_CHIP_ID, &chip_id, 1);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %s", esp_err_to_name(ret));
        return false;
    }
    
    if (chip_id != BMP581_CHIP_ID) {
        ESP_LOGE(TAG, "Invalid chip ID: 0x%02X (expected 0x%02X)", chip_id, BMP581_CHIP_ID);
        return false;
    }
    
    ESP_LOGI(TAG, "BMP581 chip ID verified: 0x%02X", chip_id);
    return true;
}

/**
 * @brief Initialize BMP581 sensor
 */
bool bmp581_init(bmp581_handle_t *handle, i2c_master_bus_handle_t i2c_bus, uint8_t device_addr)
{
    if (handle == NULL || i2c_bus == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    memset(handle, 0, sizeof(bmp581_handle_t));
    handle->i2c_bus = i2c_bus;
    handle->device_address = (device_addr != 0) ? device_addr : BMP581_I2C_ADDR_DEFAULT;

    // Perform soft reset
    esp_err_t ret = bmp581_write_register(handle, BMP581_REG_CMD, BMP581_CMD_SOFT_RESET);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send soft reset: %s", esp_err_to_name(ret));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(BMP581_RESET_DELAY_MS));

    // Verify chip ID
    if (!bmp581_verify_chip_id(handle)) {
        return false;
    }

    // Configure sensor for normal mode with standard oversampling
    // OSR configuration: 8x oversampling for pressure and temperature
    ret = bmp581_write_register(handle, BMP581_REG_OSR_CONFIG, 0x03);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure OSR: %s", esp_err_to_name(ret));
        return false;
    }

    handle->initialized = true;
    ESP_LOGI(TAG, "BMP581 initialized at address 0x%02X", handle->device_address);
    
    return true;
}

/**
 * @brief Deinitialize BMP581 sensor
 */
void bmp581_deinit(bmp581_handle_t *handle)
{
    if (handle != NULL) {
        handle->initialized = false;
        ESP_LOGI(TAG, "BMP581 deinitialized");
    }
}

/**
 * @brief Read pressure and temperature from sensor
 */
bool bmp581_read(bmp581_handle_t *handle, bmp581_data_t *data)
{
    if (handle == NULL || data == NULL || !handle->initialized) {
        ESP_LOGE(TAG, "Invalid parameters or not initialized");
        return false;
    }

    uint8_t pressure_data[3] = {0};
    uint8_t temp_data[3] = {0};

    // Read pressure data (3 bytes)
    esp_err_t ret = bmp581_read_register(handle, BMP581_REG_PRESS_DATA, pressure_data, 3);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read pressure data: %s", esp_err_to_name(ret));
        data->valid = false;
        return false;
    }

    // Read temperature data (3 bytes)
    ret = bmp581_read_register(handle, BMP581_REG_TEMP_DATA, temp_data, 3);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature data: %s", esp_err_to_name(ret));
        data->valid = false;
        return false;
    }

    // Parse pressure data (24-bit)
    uint32_t pressure_raw = ((uint32_t)pressure_data[2] << 16) | 
                            ((uint32_t)pressure_data[1] << 8) | 
                            pressure_data[0];
    
    // Parse temperature data (24-bit)
    uint32_t temp_raw = ((uint32_t)temp_data[2] << 16) | 
                        ((uint32_t)temp_data[1] << 8) | 
                        temp_data[0];

    // Convert pressure to Pascals
    // BMP581 outputs 24-bit data, typical conversion factor
    data->pressure_pa = (float)pressure_raw / 64.0f;

    // Convert temperature to Celsius
    // BMP581 temperature conversion (typical for Bosch sensors)
    data->temperature_c = (float)temp_raw / 65536.0f;

    // Validate ranges
    // Pressure: 300-1250 hPa (30000-125000 Pa)
    // Temperature: -40 to +85°C
    if (data->pressure_pa < 30000.0f || data->pressure_pa > 125000.0f ||
        data->temperature_c < -40.0f || data->temperature_c > 85.0f) {
        ESP_LOGW(TAG, "Sensor reading out of range: P=%.2f Pa, T=%.2f°C", 
                 data->pressure_pa, data->temperature_c);
        data->valid = false;
        return false;
    }

    data->valid = true;
    return true;
}

/**
 * @brief Read pressure only
 */
bool bmp581_read_pressure(bmp581_handle_t *handle, float *pressure_pa)
{
    bmp581_data_t data;
    if (bmp581_read(handle, &data)) {
        *pressure_pa = data.pressure_pa;
        return true;
    }
    return false;
}

/**
 * @brief Read pressure in hPa (hectopascals/millibars)
 */
bool bmp581_read_pressure_hpa(bmp581_handle_t *handle, float *pressure_hpa)
{
    float pressure_pa;
    if (bmp581_read_pressure(handle, &pressure_pa)) {
        *pressure_hpa = pressure_pa / 100.0f;  // Convert Pa to hPa
        return true;
    }
    return false;
}

/**
 * @brief Read temperature only
 */
bool bmp581_read_temperature(bmp581_handle_t *handle, float *temperature_c)
{
    bmp581_data_t data;
    if (bmp581_read(handle, &data)) {
        *temperature_c = data.temperature_c;
        return true;
    }
    return false;
}

/**
 * @brief Check if sensor is initialized
 */
bool bmp581_is_initialized(const bmp581_handle_t *handle)
{
    return (handle != NULL && handle->initialized);
}
