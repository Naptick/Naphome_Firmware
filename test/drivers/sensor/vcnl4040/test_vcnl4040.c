#include "unity.h"
#include "vcnl4040.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

static uint16_t g_registers[256];

esp_err_t vcnl4040_i2c_transfer(i2c_port_t port,
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
    if (read_size == 0 && write_size == 3) {
        uint8_t reg = write_buf[0];
        uint16_t value = ((uint16_t)write_buf[1] << 8) | write_buf[2];
        g_registers[reg] = value;
        return ESP_OK;
    }
    if (read_size == 0 && write_size == 2) {
        uint8_t reg = write_buf[0];
        uint16_t value = ((uint16_t)write_buf[1] << 8) | 0;
        g_registers[reg] = value;
        return ESP_OK;
    }
    if (read_size == 2 && write_size == 1) {
        uint8_t reg = write_buf[0];
        uint16_t value = g_registers[reg];
        read_buf[0] = value >> 8;
        read_buf[1] = value & 0xFF;
        return ESP_OK;
    }
    return ESP_FAIL;
}

TEST_CASE("vcnl4040 init configures registers", "[vcnl4040]")
{
    memset(g_registers, 0, sizeof(g_registers));
    vcnl4040_t dev = {0};
    vcnl4040_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_4,
        .scl_io_num = GPIO_NUM_5,
        .i2c_clk_speed_hz = 100000,
        .led_current_ma = 100,
        .prox_rate = VCNL4040_PROX_RATE_31_3_SPS
    };
    TEST_ASSERT_EQUAL(ESP_OK, vcnl4040_init(&dev, &cfg));
    TEST_ASSERT_TRUE(dev.initialized);
    TEST_ASSERT_NOT_EQUAL(0, g_registers[0x00]);
    TEST_ASSERT_NOT_EQUAL(0, g_registers[0x03]);
    TEST_ASSERT_EQUAL(100 << 8, g_registers[0x05]);
    TEST_ASSERT_EQUAL(ESP_OK, vcnl4040_deinit(&dev));
}

TEST_CASE("vcnl4040 read functions succeed", "[vcnl4040]")
{
    memset(g_registers, 0, sizeof(g_registers));
    vcnl4040_t dev = {0};
    vcnl4040_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_4,
        .scl_io_num = GPIO_NUM_5,
        .i2c_clk_speed_hz = 100000,
        .led_current_ma = 20,
        .prox_rate = VCNL4040_PROX_RATE_31_3_SPS
    };
    TEST_ASSERT_EQUAL(ESP_OK, vcnl4040_init(&dev, &cfg));

    g_registers[0x09] = 0x1234;
    g_registers[0x08] = 0xABCD;

    uint16_t lux = 0;
    uint16_t prox = 0;
    TEST_ASSERT_EQUAL(ESP_OK, vcnl4040_read_ambient_lux(&dev, &lux));
    TEST_ASSERT_EQUAL_UINT16(0x1234, lux);
    TEST_ASSERT_EQUAL(ESP_OK, vcnl4040_read_proximity(&dev, &prox));
    TEST_ASSERT_EQUAL_UINT16(0xABCD, prox);

    TEST_ASSERT_EQUAL(ESP_OK, vcnl4040_deinit(&dev));
}
