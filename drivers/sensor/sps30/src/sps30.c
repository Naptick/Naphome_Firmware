/**
 * @file sps30.c
 * @brief SPS30 PM2.5 Air Quality Sensor Driver Implementation
 * 
 * Implements UART communication with Sensirion SPS30 using SHDLC protocol.
 */

#include "sps30.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sps30";

// UART Configuration
#define SPS30_UART_BAUD_RATE 115200
#define SPS30_UART_BUF_SIZE 256

// SHDLC Protocol Constants
#define SHDLC_START 0x7E
#define SHDLC_STOP 0x7E
#define SHDLC_ESCAPE 0x7D
#define SHDLC_ESCAPE_XOR 0x20

// SPS30 SHDLC Commands
#define SPS30_CMD_START_MEASUREMENT 0x00
#define SPS30_CMD_STOP_MEASUREMENT 0x01
#define SPS30_CMD_READ_MEASURED_VALUES 0x03
#define SPS30_CMD_DEVICE_RESET 0xD3

// Device Address
#define SPS30_ADDR 0x00

// Timing
#define SPS30_RESET_DELAY_MS 100
#define SPS30_START_DELAY_MS 1000
#define SPS30_READ_TIMEOUT_MS 1000

/**
 * @brief Calculate CRC for SHDLC frame
 * 
 * Uses CRC-8 with polynomial 0x31 (x^8 + x^5 + x^4 + 1)
 */
static uint8_t shdlc_calc_crc(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = crc << 1;
            }
        }
    }
    
    return crc;
}

/**
 * @brief Stuff byte for SHDLC transmission
 * 
 * Escapes special characters (0x7E, 0x7D, 0x11, 0x13)
 */
static size_t shdlc_stuff_byte(uint8_t byte, uint8_t *output)
{
    if (byte == 0x7E || byte == 0x7D || byte == 0x11 || byte == 0x13) {
        output[0] = SHDLC_ESCAPE;
        output[1] = byte ^ SHDLC_ESCAPE_XOR;
        return 2;
    }
    output[0] = byte;
    return 1;
}

/**
 * @brief Send SHDLC command
 */
static bool shdlc_send_command(uart_port_t uart_port, uint8_t cmd, const uint8_t *data, size_t data_len)
{
    uint8_t tx_buf[64];
    size_t tx_len = 0;
    
    // Start byte
    tx_buf[tx_len++] = SHDLC_START;
    
    // Address (unstuffed)
    tx_buf[tx_len++] = SPS30_ADDR;
    
    // Command (unstuffed)
    tx_buf[tx_len++] = cmd;
    
    // Length (unstuffed)
    tx_buf[tx_len++] = (uint8_t)data_len;
    
    // Calculate CRC over ADR, CMD, LEN, DATA
    uint8_t crc_data[32];
    size_t crc_len = 0;
    crc_data[crc_len++] = SPS30_ADDR;
    crc_data[crc_len++] = cmd;
    crc_data[crc_len++] = (uint8_t)data_len;
    
    // Add data bytes (with stuffing for transmission)
    for (size_t i = 0; i < data_len; i++) {
        crc_data[crc_len++] = data[i];
        tx_len += shdlc_stuff_byte(data[i], &tx_buf[tx_len]);
    }
    
    // Calculate and add CRC (with stuffing)
    uint8_t crc = shdlc_calc_crc(crc_data, crc_len);
    tx_len += shdlc_stuff_byte(crc, &tx_buf[tx_len]);
    
    // Stop byte
    tx_buf[tx_len++] = SHDLC_STOP;
    
    // Send via UART
    int written = uart_write_bytes(uart_port, tx_buf, tx_len);
    if (written != tx_len) {
        ESP_LOGE(TAG, "UART write failed: expected %d, wrote %d", tx_len, written);
        return false;
    }
    
    return true;
}

/**
 * @brief Unstuff SHDLC byte
 */
static bool shdlc_unstuff_byte(const uint8_t *input, size_t *index, uint8_t *output)
{
    if (input[*index] == SHDLC_ESCAPE) {
        (*index)++;
        *output = input[*index] ^ SHDLC_ESCAPE_XOR;
        return true;
    }
    *output = input[*index];
    return false;
}

/**
 * @brief Receive SHDLC response
 */
static bool shdlc_receive_response(uart_port_t uart_port, uint8_t expected_cmd, 
                                   uint8_t *data, size_t *data_len, uint32_t timeout_ms)
{
    uint8_t rx_buf[128];
    size_t rx_len = 0;
    
    // Read response from UART
    int len = uart_read_bytes(uart_port, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(timeout_ms));
    if (len <= 0) {
        ESP_LOGE(TAG, "UART read timeout or error");
        return false;
    }
    rx_len = len;
    
    // Parse SHDLC frame
    if (rx_buf[0] != SHDLC_START) {
        ESP_LOGE(TAG, "Invalid start byte: 0x%02X", rx_buf[0]);
        return false;
    }
    
    size_t idx = 1;
    
    // Read address
    uint8_t addr = rx_buf[idx++];
    if (addr != SPS30_ADDR) {
        ESP_LOGE(TAG, "Invalid address: 0x%02X", addr);
        return false;
    }
    
    // Read command
    uint8_t cmd = rx_buf[idx++];
    if (cmd != expected_cmd) {
        ESP_LOGE(TAG, "Unexpected command: 0x%02X (expected 0x%02X)", cmd, expected_cmd);
        return false;
    }
    
    // Read state (for responses)
    uint8_t state = rx_buf[idx++];
    if (state != 0x00) {
        ESP_LOGE(TAG, "Command failed with state: 0x%02X", state);
        return false;
    }
    
    // Read length
    uint8_t payload_len = rx_buf[idx++];
    
    // Prepare CRC calculation
    uint8_t crc_data[64];
    size_t crc_len = 0;
    crc_data[crc_len++] = addr;
    crc_data[crc_len++] = cmd;
    crc_data[crc_len++] = state;
    crc_data[crc_len++] = payload_len;
    
    // Read data bytes (unstuffing as needed)
    *data_len = 0;
    for (uint8_t i = 0; i < payload_len && idx < rx_len - 2; i++) {
        uint8_t byte;
        shdlc_unstuff_byte(rx_buf, &idx, &byte);
        data[*data_len] = byte;
        crc_data[crc_len++] = byte;
        (*data_len)++;
        idx++;
    }
    
    // Read CRC (may be stuffed)
    uint8_t received_crc;
    shdlc_unstuff_byte(rx_buf, &idx, &received_crc);
    idx++;
    
    // Verify stop byte
    if (idx >= rx_len || rx_buf[idx] != SHDLC_STOP) {
        ESP_LOGE(TAG, "Invalid or missing stop byte");
        return false;
    }
    
    // Verify CRC
    uint8_t calculated_crc = shdlc_calc_crc(crc_data, crc_len);
    if (received_crc != calculated_crc) {
        ESP_LOGE(TAG, "CRC mismatch: received 0x%02X, calculated 0x%02X", 
                 received_crc, calculated_crc);
        return false;
    }
    
    return true;
}

/**
 * @brief Initialize SPS30 sensor
 */
bool sps30_init(sps30_handle_t *handle, uart_port_t uart_port, int tx_pin, int rx_pin)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return false;
    }
    
    memset(handle, 0, sizeof(sps30_handle_t));
    handle->uart_port = uart_port;
    
    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = SPS30_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t ret = uart_param_config(uart_port, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART config failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = uart_set_pin(uart_port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = uart_driver_install(uart_port, SPS30_UART_BUF_SIZE * 2, SPS30_UART_BUF_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Flush any pending data
    uart_flush(uart_port);
    
    handle->initialized = true;
    ESP_LOGI(TAG, "SPS30 initialized on UART%d (TX: GPIO%d, RX: GPIO%d)", 
             uart_port, tx_pin, rx_pin);
    
    return true;
}

/**
 * @brief Deinitialize SPS30 sensor
 */
void sps30_deinit(sps30_handle_t *handle)
{
    if (handle != NULL && handle->initialized) {
        if (handle->measuring) {
            sps30_stop_measurement(handle);
        }
        uart_driver_delete(handle->uart_port);
        handle->initialized = false;
        ESP_LOGI(TAG, "SPS30 deinitialized");
    }
}

/**
 * @brief Start measurement
 */
bool sps30_start_measurement(sps30_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    // Command data: 0x01 for floating point output format, 0x03 for big-endian
    uint8_t cmd_data[] = {0x01, 0x03};
    
    if (!shdlc_send_command(handle->uart_port, SPS30_CMD_START_MEASUREMENT, cmd_data, sizeof(cmd_data))) {
        ESP_LOGE(TAG, "Failed to send start measurement command");
        return false;
    }
    
    // Wait for response
    uint8_t response[16];
    size_t response_len;
    if (!shdlc_receive_response(handle->uart_port, SPS30_CMD_START_MEASUREMENT, 
                                response, &response_len, SPS30_READ_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "Failed to receive start measurement response");
        return false;
    }
    
    // Wait for fan to stabilize
    vTaskDelay(pdMS_TO_TICKS(SPS30_START_DELAY_MS));
    
    handle->measuring = true;
    ESP_LOGI(TAG, "Measurement started");
    
    return true;
}

/**
 * @brief Stop measurement
 */
bool sps30_stop_measurement(sps30_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    if (!shdlc_send_command(handle->uart_port, SPS30_CMD_STOP_MEASUREMENT, NULL, 0)) {
        ESP_LOGE(TAG, "Failed to send stop measurement command");
        return false;
    }
    
    // Wait for response
    uint8_t response[16];
    size_t response_len;
    if (!shdlc_receive_response(handle->uart_port, SPS30_CMD_STOP_MEASUREMENT, 
                                response, &response_len, SPS30_READ_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "Failed to receive stop measurement response");
        return false;
    }
    
    handle->measuring = false;
    ESP_LOGI(TAG, "Measurement stopped");
    
    return true;
}

/**
 * @brief Convert bytes to float (IEEE 754, big-endian)
 */
static float bytes_to_float(const uint8_t *bytes)
{
    uint32_t temp = ((uint32_t)bytes[0] << 24) | 
                    ((uint32_t)bytes[1] << 16) | 
                    ((uint32_t)bytes[2] << 8) | 
                    (uint32_t)bytes[3];
    float result;
    memcpy(&result, &temp, sizeof(float));
    return result;
}

/**
 * @brief Read PM values from sensor
 */
bool sps30_read(sps30_handle_t *handle, sps30_data_t *data)
{
    if (handle == NULL || data == NULL || !handle->initialized) {
        ESP_LOGE(TAG, "Invalid parameters or not initialized");
        return false;
    }
    
    if (!handle->measuring) {
        ESP_LOGE(TAG, "Measurement not started");
        data->valid = false;
        return false;
    }
    
    // Send read command
    if (!shdlc_send_command(handle->uart_port, SPS30_CMD_READ_MEASURED_VALUES, NULL, 0)) {
        ESP_LOGE(TAG, "Failed to send read command");
        data->valid = false;
        return false;
    }
    
    // Receive response (40 bytes: 10 floats x 4 bytes)
    uint8_t response[40];
    size_t response_len;
    if (!shdlc_receive_response(handle->uart_port, SPS30_CMD_READ_MEASURED_VALUES, 
                                response, &response_len, SPS30_READ_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "Failed to receive read response");
        data->valid = false;
        return false;
    }
    
    if (response_len < 40) {
        ESP_LOGE(TAG, "Insufficient data received: %d bytes", response_len);
        data->valid = false;
        return false;
    }
    
    // Parse PM values (mass concentration in µg/m³)
    data->pm1_0 = bytes_to_float(&response[0]);   // PM1.0
    data->pm2_5 = bytes_to_float(&response[4]);   // PM2.5
    data->pm4_0 = bytes_to_float(&response[8]);   // PM4.0
    data->pm10 = bytes_to_float(&response[12]);   // PM10
    
    // Validate ranges (PM values should be reasonable)
    if (data->pm1_0 < 0.0f || data->pm1_0 > 1000.0f ||
        data->pm2_5 < 0.0f || data->pm2_5 > 1000.0f ||
        data->pm4_0 < 0.0f || data->pm4_0 > 1000.0f ||
        data->pm10 < 0.0f || data->pm10 > 1000.0f) {
        ESP_LOGW(TAG, "PM values out of expected range: PM1.0=%.2f, PM2.5=%.2f, PM4.0=%.2f, PM10=%.2f",
                 data->pm1_0, data->pm2_5, data->pm4_0, data->pm10);
        data->valid = false;
        return false;
    }
    
    data->valid = true;
    return true;
}

/**
 * @brief Read PM2.5 value only
 */
bool sps30_read_pm25(sps30_handle_t *handle, float *pm2_5)
{
    sps30_data_t data;
    if (sps30_read(handle, &data)) {
        *pm2_5 = data.pm2_5;
        return true;
    }
    return false;
}

/**
 * @brief Check if sensor is initialized
 */
bool sps30_is_initialized(const sps30_handle_t *handle)
{
    return (handle != NULL && handle->initialized);
}

/**
 * @brief Check if sensor is measuring
 */
bool sps30_is_measuring(const sps30_handle_t *handle)
{
    return (handle != NULL && handle->measuring);
}
