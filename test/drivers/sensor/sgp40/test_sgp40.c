#include "unity.h"
#include "sgp40.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

static uint16_t g_last_command = 0;
static uint16_t g_measurement_value = 0;
static uint16_t g_self_test_value = 0xD400;

static uint8_t test_crc(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

esp_err_t sgp40_i2c_transfer(i2c_port_t port,
                             uint8_t addr,
                             const uint8_t *write_buf,
                             size_t write_size,
                             uint8_t *read_buf,
                             size_t read_size,
                             TickType_t ticks_to_wait)
{
    (void)port;
    (void)addr;
    (void)ticks_to_wait;
    if (write_size >= 2 && write_buf) {
        g_last_command = ((uint16_t)write_buf[0] << 8) | write_buf[1];
        return ESP_OK;
    }

    if (read_size == 3 && read_buf) {
        if (g_last_command == 0x260F) {
            read_buf[0] = g_measurement_value >> 8;
            read_buf[1] = g_measurement_value & 0xFF;
            read_buf[2] = test_crc(read_buf, 2);
            return ESP_OK;
        }
        if (g_last_command == 0x280E) {
            read_buf[0] = g_self_test_value >> 8;
            read_buf[1] = g_self_test_value & 0xFF;
            read_buf[2] = test_crc(read_buf, 2);
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

TEST_CASE("sgp40 init and deinit", "[sgp40]")
{
    sgp40_t dev = {0};
    sgp40_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_6,
        .scl_io_num = GPIO_NUM_7,
        .i2c_clk_speed_hz = 400000
    };
    TEST_ASSERT_EQUAL(ESP_OK, sgp40_init(&dev, &cfg));
    TEST_ASSERT_TRUE(dev.initialized);
    TEST_ASSERT_EQUAL(ESP_OK, sgp40_deinit(&dev));
}

TEST_CASE("sgp40 measurement and self test", "[sgp40]")
{
    sgp40_t dev = {0};
    sgp40_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_6,
        .scl_io_num = GPIO_NUM_7,
        .i2c_clk_speed_hz = 400000
    };
    TEST_ASSERT_EQUAL(ESP_OK, sgp40_init(&dev, &cfg));

    g_measurement_value = 0x8888;
    sgp40_raw_data_t data = {0};
    TEST_ASSERT_EQUAL(ESP_OK, sgp40_measure_raw(&dev, 0x8000, 0x6666, &data));
    TEST_ASSERT_EQUAL_UINT16(g_measurement_value, data.voc_ticks);

    bool passed = false;
    TEST_ASSERT_EQUAL(ESP_OK, sgp40_perform_self_test(&dev, &passed));
    TEST_ASSERT_TRUE(passed);

    TEST_ASSERT_EQUAL(ESP_OK, sgp40_deinit(&dev));
}
