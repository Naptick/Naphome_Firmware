#include "unity.h"
#include "korvo1.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

static size_t g_last_read_size = 0;
static bool g_read_called = false;

esp_err_t korvo1_i2s_read(i2s_port_t port, void *dest, size_t size, size_t *bytes_read, TickType_t ticks_to_wait)
{
    (void)port;
    (void)ticks_to_wait;
    g_read_called = true;
    g_last_read_size = size;
    memset(dest, 0xAA, size);
    if (bytes_read) {
        *bytes_read = size;
    }
    return ESP_OK;
}

TEST_CASE("korvo1 init start stop", "[korvo1]")
{
    korvo1_t dev = {0};
    korvo1_config_t cfg = {
        .port = I2S_NUM_0,
        .din_io_num = GPIO_NUM_19,
        .bclk_io_num = GPIO_NUM_18,
        .ws_io_num = GPIO_NUM_17,
        .mclk_io_num = GPIO_NUM_0,
        .sample_rate_hz = 16000,
        .dma_buffer_count = 4,
        .dma_buffer_len = 256,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT
    };
    TEST_ASSERT_EQUAL(ESP_OK, korvo1_init(&dev, &cfg));
    TEST_ASSERT_TRUE(dev.initialized);
    TEST_ASSERT_EQUAL(ESP_OK, korvo1_start(&dev));
    TEST_ASSERT_TRUE(dev.streaming);

    uint8_t buffer[64];
    size_t bytes_read = 0;
    g_read_called = false;
    TEST_ASSERT_EQUAL(ESP_OK, korvo1_read(&dev, buffer, sizeof(buffer), &bytes_read, pdMS_TO_TICKS(50)));
    TEST_ASSERT_TRUE(g_read_called);
    TEST_ASSERT_EQUAL(sizeof(buffer), bytes_read);

    TEST_ASSERT_EQUAL(ESP_OK, korvo1_stop(&dev));
    TEST_ASSERT_FALSE(dev.streaming);
    TEST_ASSERT_EQUAL(ESP_OK, korvo1_deinit(&dev));
}

TEST_CASE("korvo1 read requires start", "[korvo1]")
{
    korvo1_t dev = {0};
    korvo1_config_t cfg = {
        .port = I2S_NUM_0,
        .din_io_num = GPIO_NUM_19,
        .bclk_io_num = GPIO_NUM_18,
        .ws_io_num = GPIO_NUM_17,
        .mclk_io_num = GPIO_NUM_0,
        .sample_rate_hz = 16000,
        .dma_buffer_count = 4,
        .dma_buffer_len = 256,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT
    };
    TEST_ASSERT_EQUAL(ESP_OK, korvo1_init(&dev, &cfg));
    uint8_t buffer[32];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, korvo1_read(&dev, buffer, sizeof(buffer), NULL, 0));
    TEST_ASSERT_EQUAL(ESP_OK, korvo1_deinit(&dev));
}
