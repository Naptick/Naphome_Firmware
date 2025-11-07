/**
 * @file scd41.c
 * @brief SCD41 CO2 Sensor Driver Implementation
 */

#include "scd41.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "scd41";

// SCD41 I2C address
#define SCD41_I2C_ADDR_DEFAULT 0x62

// SCD41 commands (16-bit commands sent MSB first)
#define SCD41_CMD_START_PERIODIC_MEASUREMENT 0x21B1
#define SCD41_CMD_READ_MEASUREMENT 0xEC05
#define SCD41_CMD_STOP_PERIODIC_MEASUREMENT 0x3F86
#define SCD41_CMD_SELF_TEST 0x3639

// Timing constants (in milliseconds)
#define SCD41_INIT_DELAY_MS 1000
#define SCD41_STOP_MEASUREMENT_DELAY_MS 500
#define SCD41_MEASUREMENT_INTERVAL_MS 5000
#define SCD41_SELF_TEST_DELAY_MS 10000

// Data validation ranges
#define SCD41_CO2_MIN_PPM 400
#define SCD41_CO2_MAX_PPM 5000
#define SCD41_TEMP_MIN_C -10.0f
#define SCD41_TEMP_MAX_C 60.0f
#define SCD41_HUM_MIN_RH 0.0f
#define SCD41_HUM_MAX_RH 100.0f

/**
 * @brief Calculate CRC-8 checksum for SCD41 data
 * 
 * Polynomial: 0x31 (x^8 + x^5 + x^4 + 1)
 * Initialization: 0xFF
 */
static uint8_t scd41_calculate_crc(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    
    return crc;
}

/**
 * @brief Send command to SCD41
 */
static esp_err_t scd41_send_command(scd41_handle_t *handle, uint16_t command)
{
    uint8_t cmd_buffer[2];
    cmd_buffer[0] = (command >> 8) & 0xFF;  // MSB
    cmd_buffer[1] = command & 0xFF;          // LSB
    
    return i2c_master_transmit(handle->i2c_bus, handle->device_address, 
                               cmd_buffer, 2, -1);
}

/**
 * @brief Initialize SCD41 sensor
 */
bool scd41_init(scd41_handle_t *handle, i2c_master_bus_handle_t i2c_bus, uint8_t device_addr)
{
    if (handle == NULL || i2c_bus == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    memset(handle, 0, sizeof(scd41_handle_t));
    handle->i2c_bus = i2c_bus;
    handle->device_address = (device_addr != 0) ? device_addr : SCD41_I2C_ADDR_DEFAULT;
    handle->measurement_started = false;

    // Wait for sensor to be ready after power-up
    vTaskDelay(pdMS_TO_TICKS(SCD41_INIT_DELAY_MS));

    handle->initialized = true;
    ESP_LOGI(TAG, "SCD41 initialized at address 0x%02X", handle->device_address);
    
    return true;
}

/**
 * @brief Deinitialize SCD41 sensor
 */
void scd41_deinit(scd41_handle_t *handle)
{
    if (handle != NULL) {
        // Stop periodic measurement if running
        if (handle->measurement_started) {
            scd41_stop_periodic_measurement(handle);
        }
        handle->initialized = false;
        ESP_LOGI(TAG, "SCD41 deinitialized");
    }
}

/**
 * @brief Start periodic measurement
 */
bool scd41_start_periodic_measurement(scd41_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        ESP_LOGE(TAG, "Invalid parameters or not initialized");
        return false;
    }

    if (handle->measurement_started) {
        ESP_LOGW(TAG, "Periodic measurement already started");
        return true;
    }

    esp_err_t ret = scd41_send_command(handle, SCD41_CMD_START_PERIODIC_MEASUREMENT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start periodic measurement: %s", esp_err_to_name(ret));
        return false;
    }

    handle->measurement_started = true;
    ESP_LOGI(TAG, "Periodic measurement started (5s interval)");
    
    return true;
}

/**
 * @brief Stop periodic measurement
 */
bool scd41_stop_periodic_measurement(scd41_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        ESP_LOGE(TAG, "Invalid parameters or not initialized");
        return false;
    }

    if (!handle->measurement_started) {
        ESP_LOGW(TAG, "Periodic measurement not started");
        return true;
    }

    esp_err_t ret = scd41_send_command(handle, SCD41_CMD_STOP_PERIODIC_MEASUREMENT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop periodic measurement: %s", esp_err_to_name(ret));
        return false;
    }

    // Wait for sensor to stop measurement
    vTaskDelay(pdMS_TO_TICKS(SCD41_STOP_MEASUREMENT_DELAY_MS));

    handle->measurement_started = false;
    ESP_LOGI(TAG, "Periodic measurement stopped");
    
    return true;
}

/**
 * @brief Read CO2, temperature, and humidity from sensor
 */
bool scd41_read(scd41_handle_t *handle, scd41_data_t *data)
{
    if (handle == NULL || data == NULL || !handle->initialized) {
        ESP_LOGE(TAG, "Invalid parameters or not initialized");
        return false;
    }

    if (!handle->measurement_started) {
        ESP_LOGE(TAG, "Periodic measurement not started");
        data->valid = false;
        return false;
    }

    // Send read measurement command
    esp_err_t ret = scd41_send_command(handle, SCD41_CMD_READ_MEASUREMENT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send read command: %s", esp_err_to_name(ret));
        data->valid = false;
        return false;
    }

    // Small delay before reading (sensor needs time to prepare data)
    vTaskDelay(pdMS_TO_TICKS(1));

    // Read 9 bytes: CO2 (2 bytes + CRC), Temp (2 bytes + CRC), Humidity (2 bytes + CRC)
    uint8_t rx_data[9] = {0};
    ret = i2c_master_receive(handle->i2c_bus, handle->device_address, rx_data, 9, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
        data->valid = false;
        return false;
    }

    // Verify CRC for each data word
    uint8_t crc_co2 = scd41_calculate_crc(&rx_data[0], 2);
    uint8_t crc_temp = scd41_calculate_crc(&rx_data[3], 2);
    uint8_t crc_hum = scd41_calculate_crc(&rx_data[6], 2);

    if (crc_co2 != rx_data[2] || crc_temp != rx_data[5] || crc_hum != rx_data[8]) {
        ESP_LOGE(TAG, "CRC verification failed");
        data->valid = false;
        return false;
    }

    // Parse CO2 (ppm)
    data->co2_ppm = (rx_data[0] << 8) | rx_data[1];

    // Parse temperature (°C) - Formula: -45 + 175 * (value / 2^16)
    uint16_t temp_raw = (rx_data[3] << 8) | rx_data[4];
    data->temperature_c = -45.0f + (175.0f * temp_raw / 65535.0f);

    // Parse humidity (% RH) - Formula: 100 * (value / 2^16)
    uint16_t hum_raw = (rx_data[6] << 8) | rx_data[7];
    data->humidity_rh = 100.0f * hum_raw / 65535.0f;

    // Validate ranges
    if (data->co2_ppm < SCD41_CO2_MIN_PPM || data->co2_ppm > SCD41_CO2_MAX_PPM ||
        data->temperature_c < SCD41_TEMP_MIN_C || data->temperature_c > SCD41_TEMP_MAX_C ||
        data->humidity_rh < SCD41_HUM_MIN_RH || data->humidity_rh > SCD41_HUM_MAX_RH) {
        ESP_LOGW(TAG, "Sensor reading out of range: CO2=%u ppm, T=%.2f°C, RH=%.2f%%", 
                 data->co2_ppm, data->temperature_c, data->humidity_rh);
        data->valid = false;
        return false;
    }

    data->valid = true;
    return true;
}

/**
 * @brief Read CO2 concentration only
 */
bool scd41_read_co2(scd41_handle_t *handle, uint16_t *co2_ppm)
{
    scd41_data_t data;
    if (scd41_read(handle, &data)) {
        *co2_ppm = data.co2_ppm;
        return true;
    }
    return false;
}

/**
 * @brief Read temperature only
 */
bool scd41_read_temperature(scd41_handle_t *handle, float *temperature_c)
{
    scd41_data_t data;
    if (scd41_read(handle, &data)) {
        *temperature_c = data.temperature_c;
        return true;
    }
    return false;
}

/**
 * @brief Read humidity only
 */
bool scd41_read_humidity(scd41_handle_t *handle, float *humidity_rh)
{
    scd41_data_t data;
    if (scd41_read(handle, &data)) {
        *humidity_rh = data.humidity_rh;
        return true;
    }
    return false;
}

/**
 * @brief Check if sensor is initialized
 */
bool scd41_is_initialized(const scd41_handle_t *handle)
{
    return (handle != NULL && handle->initialized);
}

/**
 * @brief Perform sensor self-test
 */
bool scd41_self_test(scd41_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        ESP_LOGE(TAG, "Invalid parameters or not initialized");
        return false;
    }

    // Stop measurement if running
    bool was_running = handle->measurement_started;
    if (was_running) {
        scd41_stop_periodic_measurement(handle);
    }

    // Send self-test command
    esp_err_t ret = scd41_send_command(handle, SCD41_CMD_SELF_TEST);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send self-test command: %s", esp_err_to_name(ret));
        return false;
    }

    // Wait for self-test to complete
    ESP_LOGI(TAG, "Self-test in progress (10 seconds)...");
    vTaskDelay(pdMS_TO_TICKS(SCD41_SELF_TEST_DELAY_MS));

    // Read self-test result (3 bytes: result word + CRC)
    uint8_t result[3] = {0};
    ret = i2c_master_receive(handle->i2c_bus, handle->device_address, result, 3, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read self-test result: %s", esp_err_to_name(ret));
        return false;
    }

    // Verify CRC
    uint8_t crc = scd41_calculate_crc(&result[0], 2);
    if (crc != result[2]) {
        ESP_LOGE(TAG, "Self-test CRC verification failed");
        return false;
    }

    // Check result (0x0000 = pass, anything else = fail)
    uint16_t test_result = (result[0] << 8) | result[1];
    bool passed = (test_result == 0x0000);

    if (passed) {
        ESP_LOGI(TAG, "Self-test passed");
    } else {
        ESP_LOGE(TAG, "Self-test failed: 0x%04X", test_result);
    }

    // Restart measurement if it was running
    if (was_running) {
        scd41_start_periodic_measurement(handle);
    }

    return passed;
}
