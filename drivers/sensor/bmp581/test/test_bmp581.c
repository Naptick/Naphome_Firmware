/**
 * @file test_bmp581.c
 * @brief Unit tests for BMP581 driver
 * 
 * Tests driver functionality in isolation using mocks.
 */

#include <unity.h>
#include <string.h>
#include "bmp581.h"
#include "mock_i2c_master.h"  // Mock for I2C operations

// Test fixtures
static bmp581_handle_t test_handle;
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
    if (bmp581_is_initialized(&test_handle)) {
        bmp581_deinit(&test_handle);
    }
}

/**
 * @test Test BMP581 initialization
 */
void test_bmp581_init_success(void)
{
    // Mock I2C soft reset command
    uint8_t reset_cmd[2] = {0x7E, 0xB6};
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, 
        reset_cmd, 2, -1,
        ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();

    // Mock chip ID read
    uint8_t chip_id = 0x50;
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46,
        NULL, 1, NULL, 1, -1,
        ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(&chip_id, 1);

    // Mock OSR configuration
    uint8_t osr_cmd[2] = {0x36, 0x03};
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46,
        osr_cmd, 2, -1,
        ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();

    bool result = bmp581_init(&test_handle, mock_i2c_bus, 0x46);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(bmp581_is_initialized(&test_handle));
    TEST_ASSERT_EQUAL(0x46, test_handle.device_address);
}

/**
 * @test Test BMP581 initialization with invalid parameters
 */
void test_bmp581_init_null_handle(void)
{
    bool result = bmp581_init(NULL, mock_i2c_bus, 0x46);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test BMP581 initialization with null I2C bus
 */
void test_bmp581_init_null_i2c_bus(void)
{
    bool result = bmp581_init(&test_handle, NULL, 0x46);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test BMP581 initialization with invalid chip ID
 */
void test_bmp581_init_invalid_chip_id(void)
{
    // Mock I2C soft reset command
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();

    // Mock chip ID read with invalid ID
    uint8_t chip_id = 0xFF;
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 1, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(&chip_id, 1);

    bool result = bmp581_init(&test_handle, mock_i2c_bus, 0x46);
    
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test BMP581 read pressure and temperature
 */
void test_bmp581_read_success(void)
{
    // Initialize sensor
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    uint8_t chip_id = 0x50;
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 1, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(&chip_id, 1);
    
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    bmp581_init(&test_handle, mock_i2c_bus, 0x46);

    // Mock pressure data read (3 bytes) - ~100000 Pa (1000 hPa / 1 bar)
    uint8_t pressure_data[3] = {0x00, 0x64, 0x00};  // 6400000 / 64 = 100000 Pa
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 3, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(pressure_data, 3);

    // Mock temperature data read (3 bytes) - ~20°C
    uint8_t temp_data[3] = {0x00, 0x47, 0x01};  // Example temp value
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 3, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(temp_data, 3);

    bmp581_data_t data;
    bool result = bmp581_read(&test_handle, &data);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(data.valid);
    // Pressure should be around 100000 Pa (1000 hPa)
    TEST_ASSERT_FLOAT_WITHIN(10000.0, 100000.0, data.pressure_pa);
}

/**
 * @test Test BMP581 read with I2C error
 */
void test_bmp581_read_i2c_error(void)
{
    // Initialize sensor
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    uint8_t chip_id = 0x50;
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 1, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(&chip_id, 1);
    
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    bmp581_init(&test_handle, mock_i2c_bus, 0x46);

    // Mock I2C error on pressure read
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 3, -1, ESP_ERR_TIMEOUT
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();

    bmp581_data_t data;
    bool result = bmp581_read(&test_handle, &data);

    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(data.valid);
}

/**
 * @test Test BMP581 read pressure only
 */
void test_bmp581_read_pressure_only(void)
{
    // Initialize sensor
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    uint8_t chip_id = 0x50;
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 1, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(&chip_id, 1);
    
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    bmp581_init(&test_handle, mock_i2c_bus, 0x46);

    // Mock pressure data
    uint8_t pressure_data[3] = {0x00, 0x64, 0x00};
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 3, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(pressure_data, 3);

    // Mock temperature data
    uint8_t temp_data[3] = {0x00, 0x47, 0x01};
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 3, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(temp_data, 3);

    float pressure_pa;
    bool result = bmp581_read_pressure(&test_handle, &pressure_pa);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FLOAT_WITHIN(10000.0, 100000.0, pressure_pa);
}

/**
 * @test Test BMP581 read pressure in hPa
 */
void test_bmp581_read_pressure_hpa(void)
{
    // Initialize sensor
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    uint8_t chip_id = 0x50;
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 1, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(&chip_id, 1);
    
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    bmp581_init(&test_handle, mock_i2c_bus, 0x46);

    // Mock pressure data
    uint8_t pressure_data[3] = {0x00, 0x64, 0x00};
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 3, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(pressure_data, 3);

    // Mock temperature data
    uint8_t temp_data[3] = {0x00, 0x47, 0x01};
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 3, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(temp_data, 3);

    float pressure_hpa;
    bool result = bmp581_read_pressure_hpa(&test_handle, &pressure_hpa);

    TEST_ASSERT_TRUE(result);
    // Should be around 1000 hPa (100000 Pa / 100)
    TEST_ASSERT_FLOAT_WITHIN(100.0, 1000.0, pressure_hpa);
}

/**
 * @test Test BMP581 deinit
 */
void test_bmp581_deinit(void)
{
    // Initialize then deinit
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    uint8_t chip_id = 0x50;
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 1, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(&chip_id, 1);
    
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    bmp581_init(&test_handle, mock_i2c_bus, 0x46);
    
    bmp581_deinit(&test_handle);
    
    TEST_ASSERT_FALSE(bmp581_is_initialized(&test_handle));
}

/**
 * @test Test BMP581 read with out of range values
 */
void test_bmp581_read_out_of_range(void)
{
    // Initialize sensor
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    uint8_t chip_id = 0x50;
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 1, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(&chip_id, 1);
    
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    bmp581_init(&test_handle, mock_i2c_bus, 0x46);

    // Mock invalid pressure data (out of range)
    uint8_t pressure_data[3] = {0xFF, 0xFF, 0xFF};  // Very high value
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 3, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(pressure_data, 3);

    // Mock temperature data
    uint8_t temp_data[3] = {0x00, 0x47, 0x01};
    i2c_master_transmit_receive_ExpectAndReturn(
        mock_i2c_bus, 0x46, NULL, 1, NULL, 3, -1, ESP_OK
    );
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    i2c_master_transmit_receive_IgnoreArg_read_buffer();
    i2c_master_transmit_receive_ReturnArrayThruPtr_read_buffer(temp_data, 3);

    bmp581_data_t data;
    bool result = bmp581_read(&test_handle, &data);

    // Should detect invalid data
    TEST_ASSERT_FALSE(data.valid);
}
