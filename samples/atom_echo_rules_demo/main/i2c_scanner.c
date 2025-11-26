#include "i2c_scanner.h"

#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2c_scanner";

typedef struct {
    uint8_t addr;
    const char *name;
} known_device_t;

static const known_device_t kKnownDevices[] = {
    {0x14, "Atom HUB (IO expander?)"},
    {0x34, "M5 Atom PMIC AXP2101"},
    {0x38, "VCNL4040 proximity/ambient"},
    {0x3C, "SSD1307/Display"},
    {0x44, "SHT30/SHT4x temperature"},
    {0x45, "SHT45 temperature/humidity"},
    {0x47, "SHT31 temperature/humidity"},
    {0x48, "ADS1115 ADC"},
    {0x4A, "MCP9808 temperature"},
    {0x4C, "TCA9534 IO expander"},
    {0x4D, "MAX17048 fuel gauge"},
    {0x52, "Atom Motion I2C MCU"},
    {0x58, "SGP40 VOC sensor"},
    {0x5A, "SGP30 VOC sensor"},
    {0x5C, "SCD40 CO2 sensor"},
    {0x60, "VCNL4040/Light sensor"},
    {0x62, "Si7021 humidity"},
    {0x68, "MPU6886 IMU"},
    {0x69, "MPU6050 IMU"},
    {0x6A, "LSM6DS3 IMU"},
    {0x6B, "BMI270 IMU"},
};

static const char *lookup_device_name(uint8_t address)
{
    for (size_t i = 0; i < sizeof(kKnownDevices) / sizeof(kKnownDevices[0]); ++i) {
        if (kKnownDevices[i].addr == address) {
            return kKnownDevices[i].name;
        }
    }
    return NULL;
}

static esp_err_t configure_bus(const i2c_bus_config_t *bus)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = bus->sda,
        .scl_io_num = bus->scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = bus->frequency_hz,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(bus->port, &cfg), TAG, "param config");
    return i2c_driver_install(bus->port, cfg.mode, 0, 0, 0);
}

static void teardown_bus(const i2c_bus_config_t *bus)
{
    i2c_driver_delete(bus->port);
}

esp_err_t i2c_scanner_scan_bus_with_callback(const i2c_bus_config_t *bus,
                                             i2c_scanner_result_cb_t cb,
                                             void *ctx)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(configure_bus(bus), TAG, "configure");
    ESP_LOGI(TAG, "Scanning %s (port=%d SDA=GPIO%d SCL=GPIO%d freq=%lu Hz)",
             bus->label ? bus->label : "unnamed bus",
             bus->port,
             bus->sda,
             bus->scl,
             (unsigned long)bus->frequency_hz);

    int found = 0;
    for (uint8_t addr = 0x03; addr < 0x78; ++addr) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(bus->port, cmd, pdMS_TO_TICKS(20));
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK) {
            const char *name = lookup_device_name(addr);
            ESP_LOGI(TAG, "  - 0x%02X %s%s",
                     addr,
                     name ? "→ " : "",
                     name ? name : "");
            if (cb) {
                cb(bus, addr, name, ctx);
            }
            found++;
        } else if (err != ESP_ERR_TIMEOUT && err != ESP_FAIL) {
            ESP_LOGV(TAG, "addr 0x%02X err=%s", addr, esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    if (found == 0) {
        ESP_LOGW(TAG, "  No devices detected on %s", bus->label ? bus->label : "bus");
    }

    teardown_bus(bus);
    return ESP_OK;
}

esp_err_t i2c_scanner_scan_bus(const i2c_bus_config_t *bus)
{
    return i2c_scanner_scan_bus_with_callback(bus, NULL, NULL);
}

esp_err_t i2c_scanner_scan_all(const i2c_bus_config_t *buses, size_t count)
{
    if (!buses || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; ++i) {
        esp_err_t err = i2c_scanner_scan_bus(&buses[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Scan failed on %s (%s)",
                     buses[i].label ? buses[i].label : "bus",
                     esp_err_to_name(err));
        }
    }
    return ESP_OK;
}
