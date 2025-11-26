#include "unity.h"
#include "ec10.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

static uint8_t g_uart_frame[EC10_FRAME_LENGTH];
static int g_uart_read_calls = 0;

static uint16_t ec10_test_checksum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return (uint16_t)sum;
}

int ec10_uart_read_bytes(uart_port_t uart_num, uint8_t *buf, uint32_t length, TickType_t ticks_to_wait)
{
    (void)uart_num;
    (void)ticks_to_wait;
    g_uart_read_calls++;
    if (length < EC10_FRAME_LENGTH) {
        return -1;
    }
    memcpy(buf, g_uart_frame, EC10_FRAME_LENGTH);
    return EC10_FRAME_LENGTH;
}

TEST_CASE("ec10 init and deinit", "[ec10]")
{
    ec10_t dev = {0};
    ec10_config_t cfg = {
        .uart_port = UART_NUM_1,
        .tx_io_num = GPIO_NUM_10,
        .rx_io_num = GPIO_NUM_11,
        .baud_rate = 9600,
        .rx_buffer_size = 128
    };
    TEST_ASSERT_EQUAL(ESP_OK, ec10_init(&dev, &cfg));
    TEST_ASSERT_TRUE(dev.initialized);
    TEST_ASSERT_EQUAL(ESP_OK, ec10_deinit(&dev));
}

TEST_CASE("ec10 measurement parsing", "[ec10]")
{
    ec10_t dev = {0};
    ec10_config_t cfg = {
        .uart_port = UART_NUM_1,
        .tx_io_num = GPIO_NUM_10,
        .rx_io_num = GPIO_NUM_11,
        .baud_rate = 9600,
        .rx_buffer_size = 128
    };
    TEST_ASSERT_EQUAL(ESP_OK, ec10_init(&dev, &cfg));

    memset(g_uart_frame, 0, sizeof(g_uart_frame));
    g_uart_frame[0] = 0x42;
    g_uart_frame[1] = 0x4D;
    g_uart_frame[2] = 0x00;
    g_uart_frame[3] = 0x1C;
    g_uart_frame[4] = 0x00;
    g_uart_frame[5] = 0x14; // PM1.0 = 20
    g_uart_frame[6] = 0x00;
    g_uart_frame[7] = 0x32; // PM2.5 = 50
    g_uart_frame[8] = 0x00;
    g_uart_frame[9] = 0x46; // PM10 = 70
    uint16_t checksum = ec10_test_checksum(g_uart_frame, EC10_FRAME_LENGTH - 2);
    g_uart_frame[EC10_FRAME_LENGTH - 2] = checksum >> 8;
    g_uart_frame[EC10_FRAME_LENGTH - 1] = checksum & 0xFF;

    ec10_measurement_t measurement = {0};
    g_uart_read_calls = 0;
    TEST_ASSERT_EQUAL(ESP_OK, ec10_read_measurement(&dev, &measurement, pdMS_TO_TICKS(100)));
    TEST_ASSERT_EQUAL(1, g_uart_read_calls);
    TEST_ASSERT_EQUAL_UINT16(20, measurement.pm1_0_ug_m3);
    TEST_ASSERT_EQUAL_UINT16(50, measurement.pm2_5_ug_m3);
    TEST_ASSERT_EQUAL_UINT16(70, measurement.pm10_ug_m3);

    g_uart_frame[9] ^= 0x01; // corrupt data
    checksum = ec10_test_checksum(g_uart_frame, EC10_FRAME_LENGTH - 2);
    g_uart_frame[EC10_FRAME_LENGTH - 2] = checksum >> 8;
    g_uart_frame[EC10_FRAME_LENGTH - 1] = checksum & 0xFF;
    g_uart_frame[0] = 0; // break header
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, ec10_read_measurement(&dev, &measurement, pdMS_TO_TICKS(100)));

    TEST_ASSERT_EQUAL(ESP_OK, ec10_deinit(&dev));
}
