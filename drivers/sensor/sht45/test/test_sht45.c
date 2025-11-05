/**
 * @file test_sht45.c
 * @brief Unit tests for SHT45 driver
 * 
 * Tests driver functionality in isolation using mocks.
 */

#include <unity.h>
#include "sht45.h"
#include "mock_i2c_master.h"  // Mock for I2C operations

// Test fixtures
static sht45_handle_t test_handle;
static i2c_master_bus_handle_t mock_i2c_bus;

void setUp(void)
{
    // Initialize test fixtures
    memset(&test_handle, 0, sizeof(test_handle));
    mock_i2c_bus = (i2c_master_bus_handle_t)0x1234;  // Mock I2C bus handle
}

void tearDown(void)
{
    // Cleanup
    if (sht45_is_initialized(&test_handle)) {
        sht45_deinit(&test_handle);
    }
}

/**
 * @test Test SHT45 initialization
 */
void test_sht45_init_success(void)
{
    // Mock I2C soft reset command
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x44, 
        NULL, 1, NULL, 0, -1,
        ESP_OK
    );

    bool result = sht45_init(&test_handle, mock_i2c_bus, 0x44);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(sht45_is_initialized(&test_handle));
    TEST_ASSERT_EQUAL(0x44, test_handle.device_address);
}

/**
 * @test Test SHT45 initialization with invalid parameters
 */
void test_sht45_init_null_handle(void)
{
    bool result = sht45_init(NULL, mock_i2c_bus, 0x44);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SHT45 initialization with null I2C bus
 */
void test_sht45_init_null_i2c_bus(void)
{
    bool result = sht45_init(&test_handle, NULL, 0x44);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SHT45 read temperature and humidity
 */
void test_sht45_read_success(void)
{
    // Initialize sensor
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x44, NULL, 1, NULL, 0, -1, ESP_OK
    );
    sht45_init(&test_handle, mock_i2c_bus, 0x44);

    // Mock measurement command
    uint8_t cmd = 0xFD;
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x44, &cmd, 1, NULL, 6, -1, ESP_OK
    );

    // Mock measurement data (valid reading)
    uint8_t rx_data[6] = {
        0x68, 0x3A, 0x00,  // Temperature: ~20°C
        0x4E, 0x95, 0x00   // Humidity: ~50% RH
    };
    i2c_master_receive_ExpectAndReturn(
        mock_i2c_bus, 0x44, NULL, 6, -1, ESP_OK
    );
    i2c_master_receive_IgnoreArg_rx_buffer();

    sht45_data_t data;
    bool result = sht45_read(&test_handle, &data);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(data.valid);
    // Temperature should be around 20°C
    TEST_ASSERT_FLOAT_WITHIN(5.0, 20.0, data.temperature_c);
    // Humidity should be around 50%
    TEST_ASSERT_FLOAT_WITHIN(10.0, 50.0, data.humidity_rh);
}

/**
 * @test Test SHT45 read with I2C error
 */
void test_sht45_read_i2c_error(void)
{
    // Initialize sensor
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x44, NULL, 1, NULL, 0, -1, ESP_OK
    );
    sht45_init(&test_handle, mock_i2c_bus, 0x44);

    // Mock I2C error
    uint8_t cmd = 0xFD;
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x44, &cmd, 1, NULL, 6, -1, ESP_ERR_TIMEOUT
    );

    sht45_data_t data;
    bool result = sht45_read(&test_handle, &data);

    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(data.valid);
}

/**
 * @test Test SHT45 deinit
 */
void test_sht45_deinit(void)
{
    // Initialize then deinit
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x44, NULL, 1, NULL, 0, -1, ESP_OK
    );
    sht45_init(&test_handle, mock_i2c_bus, 0x44);
    
    sht45_deinit(&test_handle);
    
    TEST_ASSERT_FALSE(sht45_is_initialized(&test_handle));
}

/**
 * @test Test SHT45 read with out of range values
 */
void test_sht45_read_out_of_range(void)
{
    // Initialize sensor
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x44, NULL, 1, NULL, 0, -1, ESP_OK
    );
    sht45_init(&test_handle, mock_i2c_bus, 0x44);

    // Mock measurement with out-of-range data
    uint8_t cmd = 0xFD;
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x44, &cmd, 1, NULL, 6, -1, ESP_OK
    );

    // Mock invalid data (temperature > 125°C)
    i2c_master_receive_ExpectAndReturn(
        mock_i2c_bus, 0x44, NULL, 6, -1, ESP_OK
    );
    i2c_master_receive_IgnoreArg_rx_buffer();

    sht45_data_t data;
    bool result = sht45_read(&test_handle, &data);

    // Should detect invalid data
    TEST_ASSERT_FALSE(data.valid);
}
