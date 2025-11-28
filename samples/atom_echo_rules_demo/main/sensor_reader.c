#include "sensor_reader.h"

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Sensor drivers - use relative paths
// Note: SHT45 and SGP40 drivers use I2C master compat layer
// For now, we'll use direct I2C reads for all sensors to avoid dependencies

#define TAG "sensor_reader"

// I2C Configuration - using Port A (GPIO 2/1)
#define I2C_PORT I2C_NUM_1
#define I2C_SDA_GPIO 2
#define I2C_SCL_GPIO 1
#define I2C_FREQ_HZ 100000

// Sensor addresses
#define BME280_ADDR_0x76 0x76
#define BME280_ADDR_0x77 0x77
#define BME680_ADDR_0x76 0x76
#define BME680_ADDR_0x77 0x77
#define SHT41_ADDR 0x44
#define SGP40_ADDR 0x59
#define AS7341_ADDR 0x39

// BME280/BME680 registers
#define BME280_REG_ID 0xD0
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG 0xF5
#define BME280_REG_PRESS_MSB 0xF7
#define BME280_REG_TEMP_MSB 0xFA
#define BME280_REG_HUM_MSB 0xFD

// BME680 specific
#define BME680_REG_ID 0xD0
#define BME680_REG_CTRL_GAS_1 0x71
#define BME680_REG_GAS_R_MSB 0x2A

// AS7341 registers
#define AS7341_REG_ID 0x92
#define AS7341_REG_ENABLE 0x80
#define AS7341_REG_CFG1 0xA3
#define AS7341_REG_CFG6 0xA8
#define AS7341_REG_CH0_DATA_L 0x95

static bool s_i2c_initialized = false;

// BME280 calibration data
static struct {
    uint16_t dig_T1;
    int16_t dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t dig_H1, dig_H3;
    int16_t dig_H2, dig_H4, dig_H5, dig_H6;
    bool loaded;
} bme280_cal = {0};

// BME680 calibration data
static struct {
    uint16_t par_t1;
    int16_t par_t2, par_t3;
    uint16_t par_p1;
    int16_t par_p2, par_p3, par_p4, par_p5, par_p6, par_p7, par_p8, par_p9, par_p10;
    uint8_t par_h1, par_h3;
    int16_t par_h2, par_h4, par_h5, par_h6, par_h7;
    uint8_t par_g1;
    int16_t par_g2, par_g3;
    bool loaded;
} bme680_cal = {0};

static esp_err_t i2c_read_reg(i2c_port_t port, uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_write_reg(i2c_port_t port, uint8_t addr, uint8_t reg, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static bool detect_bme280(i2c_port_t port, uint8_t addr)
{
    uint8_t chip_id = 0;
    if (i2c_read_reg(port, addr, BME280_REG_ID, &chip_id, 1) == ESP_OK) {
        return (chip_id == 0x60);  // BME280 chip ID
    }
    return false;
}

static bool detect_bme680(i2c_port_t port, uint8_t addr)
{
    uint8_t chip_id = 0;
    if (i2c_read_reg(port, addr, BME680_REG_ID, &chip_id, 1) == ESP_OK) {
        return (chip_id == 0x61);  // BME680 chip ID
    }
    return false;
}

static bool read_bme280_calibration(i2c_port_t port, uint8_t addr)
{
    uint8_t cal[26] = {0};
    
    // Read calibration data (0x88-0xA1)
    if (i2c_read_reg(port, addr, 0x88, cal, 24) != ESP_OK) return false;
    
    bme280_cal.dig_T1 = (uint16_t)(cal[0] | (cal[1] << 8));
    bme280_cal.dig_T2 = (int16_t)(cal[2] | (cal[3] << 8));
    bme280_cal.dig_T3 = (int16_t)(cal[4] | (cal[5] << 8));
    bme280_cal.dig_P1 = (uint16_t)(cal[6] | (cal[7] << 8));
    bme280_cal.dig_P2 = (int16_t)(cal[8] | (cal[9] << 8));
    bme280_cal.dig_P3 = (int16_t)(cal[10] | (cal[11] << 8));
    bme280_cal.dig_P4 = (int16_t)(cal[12] | (cal[13] << 8));
    bme280_cal.dig_P5 = (int16_t)(cal[14] | (cal[15] << 8));
    bme280_cal.dig_P6 = (int16_t)(cal[16] | (cal[17] << 8));
    bme280_cal.dig_P7 = (int16_t)(cal[18] | (cal[19] << 8));
    bme280_cal.dig_P8 = (int16_t)(cal[20] | (cal[21] << 8));
    bme280_cal.dig_P9 = (int16_t)(cal[22] | (cal[23] << 8));
    
    // Read H1 (0xA1)
    if (i2c_read_reg(port, addr, 0xA1, &bme280_cal.dig_H1, 1) != ESP_OK) return false;
    
    // Read H2-H6 (0xE1-0xE7)
    uint8_t h_cal[7] = {0};
    if (i2c_read_reg(port, addr, 0xE1, h_cal, 7) != ESP_OK) return false;
    
    bme280_cal.dig_H2 = (int16_t)(h_cal[0] | (h_cal[1] << 8));
    bme280_cal.dig_H3 = h_cal[2];
    bme280_cal.dig_H4 = (int16_t)((h_cal[3] << 4) | (h_cal[4] & 0x0F));
    bme280_cal.dig_H5 = (int16_t)((h_cal[4] & 0xF0) >> 4 | (h_cal[5] << 4));
    bme280_cal.dig_H6 = (int8_t)h_cal[6];
    
    bme280_cal.loaded = true;
    return true;
}

static bool read_bme280(i2c_port_t port, uint8_t addr, float *temp, float *humidity, float *pressure)
{
    if (!bme280_cal.loaded) {
        if (!read_bme280_calibration(port, addr)) {
            return false;
        }
    }
    
    // Configure for forced mode
    i2c_write_reg(port, addr, BME280_REG_CTRL_HUM, 0x05);  // Humidity oversampling x16
    i2c_write_reg(port, addr, BME280_REG_CONFIG, 0xA0);    // Standby 1000ms, filter off
    i2c_write_reg(port, addr, BME280_REG_CTRL_MEAS, 0xB7); // Forced mode, temp/press x16
    
    vTaskDelay(pdMS_TO_TICKS(20));  // Wait for measurement
    
    uint8_t data[8] = {0};
    if (i2c_read_reg(port, addr, BME280_REG_PRESS_MSB, data, 8) != ESP_OK) {
        return false;
    }
    
    int32_t press_raw = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
    int32_t temp_raw = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
    int32_t hum_raw = (data[6] << 8) | data[7];
    
    // Temperature compensation
    int32_t var1 = ((((temp_raw >> 3) - ((int32_t)bme280_cal.dig_T1 << 1))) * 
                    ((int32_t)bme280_cal.dig_T2)) >> 11;
    int32_t var2 = (((((temp_raw >> 4) - ((int32_t)bme280_cal.dig_T1)) * 
                     ((temp_raw >> 4) - ((int32_t)bme280_cal.dig_T1))) >> 12) * 
                    ((int32_t)bme280_cal.dig_T3)) >> 14;
    int32_t t_fine = var1 + var2;
    *temp = ((t_fine * 5 + 128) >> 8) / 100.0f;
    
    // Pressure compensation
    int64_t var1_p = ((int64_t)t_fine) - 128000;
    int64_t var2_p = var1_p * var1_p * (int64_t)bme280_cal.dig_P6;
    var2_p = var2_p + ((var1_p * (int64_t)bme280_cal.dig_P5) << 17);
    var2_p = var2_p + (((int64_t)bme280_cal.dig_P4) << 35);
    var1_p = ((var1_p * var1_p * (int64_t)bme280_cal.dig_P3) >> 8) + 
             ((var1_p * (int64_t)bme280_cal.dig_P2) << 12);
    var1_p = (((((int64_t)1) << 47) + var1_p)) * ((int64_t)bme280_cal.dig_P1) >> 33;
    if (var1_p == 0) {
        *pressure = 0;
    } else {
        int64_t p = 1048576 - press_raw;
        p = (((p << 31) - var2_p) * 3125) / var1_p;
        var1_p = (((int64_t)bme280_cal.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
        var2_p = (((int64_t)bme280_cal.dig_P8) * p) >> 19;
        p = ((p + var1_p + var2_p) >> 8) + (((int64_t)bme280_cal.dig_P7) << 4);
        *pressure = p / 256.0f / 100.0f;  // Convert to hPa
    }
    
    // Humidity compensation
    int32_t v_x1_u32r = (t_fine - ((int32_t)76800));
    v_x1_u32r = (((((hum_raw << 14) - (((int32_t)bme280_cal.dig_H4) << 20) - 
                    (((int32_t)bme280_cal.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) * 
                 (((((((v_x1_u32r * ((int32_t)bme280_cal.dig_H6)) >> 10) * 
                      (((v_x1_u32r * ((int32_t)bme280_cal.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + 
                    ((int32_t)2097152)) * ((int32_t)bme280_cal.dig_H2) + 8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * 
                               ((int32_t)bme280_cal.dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
    v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;
    *humidity = (v_x1_u32r >> 12) / 1024.0f;
    
    return true;
}

static bool read_bme680(i2c_port_t port, uint8_t addr, float *temp, float *humidity, 
                        float *pressure, uint32_t *gas_resistance)
{
    // Simplified BME680 read - full implementation would require calibration data
    // For now, just detect and return basic values
    uint8_t chip_id = 0;
    if (i2c_read_reg(port, addr, BME680_REG_ID, &chip_id, 1) != ESP_OK || chip_id != 0x61) {
        return false;
    }
    
    // Configure and trigger measurement
    i2c_write_reg(port, addr, 0x74, 0x10);  // Gas heater off, run gas
    i2c_write_reg(port, addr, 0x75, 0x00);  // Gas wait 0
    i2c_write_reg(port, addr, BME680_REG_CTRL_GAS_1, 0x10);  // Enable gas
    i2c_write_reg(port, addr, 0x72, 0x2C);  // Humidity x2, temp x2
    i2c_write_reg(port, addr, 0x74, 0x93);  // Pressure x16, forced mode
    
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait for measurement
    
    uint8_t data[15] = {0};
    if (i2c_read_reg(port, addr, 0x1F, data, 15) != ESP_OK) {
        return false;
    }
    
    // Parse raw values (simplified - would need full calibration)
    int32_t press_raw = (data[2] << 12) | (data[3] << 4) | (data[4] >> 4);
    int32_t temp_raw = (data[5] << 12) | (data[6] << 4) | (data[7] >> 4);
    int32_t hum_raw = (data[8] << 8) | data[9];
    uint16_t gas_raw = (data[13] << 2) | (data[14] >> 6);
    
    // Basic conversion (would need calibration for accuracy)
    *temp = temp_raw / 100.0f;
    *humidity = hum_raw / 1024.0f;
    *pressure = press_raw / 256.0f;
    *gas_resistance = gas_raw;
    
    return true;
}

static bool read_as7341(i2c_port_t port, uint8_t addr, uint16_t *channels)
{
    uint8_t chip_id = 0;
    if (i2c_read_reg(port, addr, AS7341_REG_ID, &chip_id, 1) != ESP_OK) {
        return false;
    }
    if ((chip_id & 0xF0) != 0x90) {  // AS7341 ID mask
        return false;
    }
    
    // Enable sensor
    i2c_write_reg(port, addr, AS7341_REG_ENABLE, 0x01);  // Power on
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Configure for spectral measurement
    i2c_write_reg(port, addr, AS7341_REG_CFG1, 0x00);  // Low gain
    i2c_write_reg(port, addr, AS7341_REG_CFG6, 0x10);  // Integration time
    
    // Start measurement
    uint8_t enable = 0;
    i2c_read_reg(port, addr, AS7341_REG_ENABLE, &enable, 1);
    i2c_write_reg(port, addr, AS7341_REG_ENABLE, enable | 0x02);  // Start measurement
    
    vTaskDelay(pdMS_TO_TICKS(500));  // Wait for measurement
    
    // Read channel data (simplified - read first few channels)
    uint8_t data[2] = {0};
    for (int i = 0; i < 8; i++) {
        if (i2c_read_reg(port, addr, AS7341_REG_CH0_DATA_L + (i * 2), data, 2) == ESP_OK) {
            channels[i] = (uint16_t)(data[0] | (data[1] << 8));
        } else {
            channels[i] = 0;
        }
    }
    
    return true;
}

esp_err_t sensor_reader_init(void)
{
    if (s_i2c_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing I2C bus for sensor reading...");
    
    // Initialize I2C bus
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    
    // I2C Port Allocation: I2C_NUM_1 is used for sensor bus (GPIO 2/1)
    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
        return err;
    }
    
    err = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "I2C driver already installed, reusing");
    }
    
    s_i2c_initialized = true;
    
    ESP_LOGI(TAG, "Sensor reader initialized (I2C ready for direct reads)");
    return ESP_OK;
}

esp_err_t sensor_reader_read_all(sensor_readings_t *readings)
{
    if (!readings || !s_i2c_initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(readings, 0, sizeof(sensor_readings_t));
    
    // Read BME280 (try both addresses)
    if (detect_bme280(I2C_PORT, BME280_ADDR_0x76)) {
        readings->bme280_available = read_bme280(I2C_PORT, BME280_ADDR_0x76,
                                                 &readings->bme280_temp_c,
                                                 &readings->bme280_humidity_rh,
                                                 &readings->bme280_pressure_hpa);
    } else if (detect_bme280(I2C_PORT, BME280_ADDR_0x77)) {
        readings->bme280_available = read_bme280(I2C_PORT, BME280_ADDR_0x77,
                                                 &readings->bme280_temp_c,
                                                 &readings->bme280_humidity_rh,
                                                 &readings->bme280_pressure_hpa);
    }
    
    // Read BME680 (try both addresses)
    if (detect_bme680(I2C_PORT, BME680_ADDR_0x76)) {
        readings->bme680_available = read_bme680(I2C_PORT, BME680_ADDR_0x76,
                                                  &readings->bme680_temp_c,
                                                  &readings->bme680_humidity_rh,
                                                  &readings->bme680_pressure_hpa,
                                                  &readings->bme680_gas_resistance);
    } else if (detect_bme680(I2C_PORT, BME680_ADDR_0x77)) {
        readings->bme680_available = read_bme680(I2C_PORT, BME680_ADDR_0x77,
                                                  &readings->bme680_temp_c,
                                                  &readings->bme680_humidity_rh,
                                                  &readings->bme680_pressure_hpa,
                                                  &readings->bme680_gas_resistance);
    }
    
    // Read SHT41 (SHT4x protocol - similar to SHT45)
    uint8_t sht_cmd = 0xFD;  // High precision measurement
    uint8_t sht_data[6] = {0};
    if (i2c_write_reg(I2C_PORT, SHT41_ADDR, sht_cmd, 0) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(15));  // Wait for measurement
        if (i2c_read_reg(I2C_PORT, SHT41_ADDR, 0, sht_data, 6) == ESP_OK) {
            uint16_t temp_raw = (sht_data[0] << 8) | sht_data[1];
            uint16_t hum_raw = (sht_data[3] << 8) | sht_data[4];
            readings->sht41_available = true;
            readings->sht41_temp_c = -45.0f + (175.0f * temp_raw / 65535.0f);
            readings->sht41_humidity_rh = -6.0f + (125.0f * hum_raw / 65535.0f);
        }
    }
    
    // Read SGP40 (simplified - just check if device responds)
    uint8_t sgp_cmd[2] = {0x26, 0x0F};  // Measure raw command
    uint8_t sgp_data[3] = {0};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SGP40_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, sgp_cmd, 2, true);
    i2c_master_stop(cmd);
    if (i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100)) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(30));  // Wait for measurement
        i2c_cmd_link_delete(cmd);
        cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (SGP40_ADDR << 1) | I2C_MASTER_READ, true);
        i2c_master_read(cmd, sgp_data, 3, I2C_MASTER_LAST_NACK);
        i2c_master_stop(cmd);
        if (i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100)) == ESP_OK) {
            readings->sgp40_available = true;
            readings->sgp40_voc_index = (sgp_data[0] << 8) | sgp_data[1];
        }
    }
    i2c_cmd_link_delete(cmd);
    
    // Read AS7341
    readings->as7341_available = read_as7341(I2C_PORT, AS7341_ADDR, readings->as7341_channels);
    
    return ESP_OK;
}

void sensor_reader_print(const sensor_readings_t *readings)
{
    if (!readings) return;
    
    ESP_LOGI(TAG, "=== Sensor Readings ===");
    
    if (readings->bme280_available) {
        ESP_LOGI(TAG, "BME280: T=%.2f°C H=%.1f%% P=%.2f hPa",
                readings->bme280_temp_c, readings->bme280_humidity_rh, readings->bme280_pressure_hpa);
    } else {
        ESP_LOGD(TAG, "BME280: Not available");
    }
    
    if (readings->bme680_available) {
        ESP_LOGI(TAG, "BME680: T=%.2f°C H=%.1f%% P=%.2f hPa Gas=%lu",
                readings->bme680_temp_c, readings->bme680_humidity_rh, 
                readings->bme680_pressure_hpa, (unsigned long)readings->bme680_gas_resistance);
    } else {
        ESP_LOGD(TAG, "BME680: Not available");
    }
    
    if (readings->sht41_available) {
        ESP_LOGI(TAG, "SHT41: T=%.2f°C H=%.1f%%",
                readings->sht41_temp_c, readings->sht41_humidity_rh);
    } else {
        ESP_LOGD(TAG, "SHT41: Not available");
    }
    
    if (readings->sgp40_available) {
        ESP_LOGI(TAG, "SGP40: VOC Index=%u", readings->sgp40_voc_index);
    } else {
        ESP_LOGD(TAG, "SGP40: Not available");
    }
    
    if (readings->as7341_available) {
        ESP_LOGI(TAG, "AS7341: Channels: ");
        for (int i = 0; i < 8; i++) {
            ESP_LOGI(TAG, "  Ch%d=%u", i, readings->as7341_channels[i]);
        }
    } else {
        ESP_LOGD(TAG, "AS7341: Not available");
    }
    
    ESP_LOGI(TAG, "======================");
}
