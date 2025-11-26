#include "unity.h"
#include "scd40.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

static uint8_t g_scd40_response[9];
static bool g_transfer_called = false;

static uint8_t test_calc_crc(const uint8_t *data, size_t len)
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

esp_err_t scd40_i2c_transfer(i2c_port_t port,
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
    g_transfer_called = true;
    if (read_size == 0) {
        return ESP_OK;
    }
    if (read_size == sizeof(g_scd40_response)) {
        memcpy(read_buf, g_scd40_response, sizeof(g_scd40_response));
        return ESP_OK;
    }
    return ESP_FAIL;
}

TEST_CASE("scd40 init invalid args", "[scd40]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, scd40_init(NULL, NULL));

    scd40_t dev = {0};
    scd40_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_1,
        .scl_io_num = GPIO_NUM_2,
        .i2c_clk_speed_hz = 100000,
        .rst_io_num = -1
    };
    TEST_ASSERT_EQUAL(ESP_OK, scd40_init(&dev, &cfg));
    TEST_ASSERT_TRUE(dev.initialized);
    TEST_ASSERT_EQUAL(ESP_OK, scd40_deinit(&dev));
}

TEST_CASE("scd40 read measurement success", "[scd40]")
{
    scd40_t dev = {0};
    scd40_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_1,
        .scl_io_num = GPIO_NUM_2,
        .i2c_clk_speed_hz = 100000,
        .rst_io_num = -1
    };
    TEST_ASSERT_EQUAL(ESP_OK, scd40_init(&dev, &cfg));

    uint16_t co2 = 450;
    uint16_t temp = 21845; // approx 45 C
    uint16_t hum = 32768;  // approx 50 %
    g_scd40_response[0] = co2 >> 8;
    g_scd40_response[1] = co2 & 0xFF;
    g_scd40_response[2] = test_calc_crc(&g_scd40_response[0], 2);
    g_scd40_response[3] = temp >> 8;
    g_scd40_response[4] = temp & 0xFF;
    g_scd40_response[5] = test_calc_crc(&g_scd40_response[3], 2);
    g_scd40_response[6] = hum >> 8;
    g_scd40_response[7] = hum & 0xFF;
    g_scd40_response[8] = test_calc_crc(&g_scd40_response[6], 2);

    scd40_measurement_t measurement = {0};
    g_transfer_called = false;
    TEST_ASSERT_EQUAL(ESP_OK, scd40_start_periodic_measurement(&dev));
    TEST_ASSERT_EQUAL(ESP_OK, scd40_read_measurement(&dev, &measurement));
    TEST_ASSERT_TRUE(g_transfer_called);
    TEST_ASSERT_EQUAL_UINT16(co2, measurement.co2_ppm);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 45.0f, measurement.temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 50.0f, measurement.humidity_rh);

    TEST_ASSERT_EQUAL(ESP_OK, scd40_stop_periodic_measurement(&dev));
    TEST_ASSERT_EQUAL(ESP_OK, scd40_deinit(&dev));
}
