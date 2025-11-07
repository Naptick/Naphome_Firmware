/**
 * @file test_scd41.c
 * @brief Unit tests for SCD41 driver
 * 
 * Tests driver functionality in isolation using mocks.
 */

#include <unity.h>
#include <string.h>
#include "scd41.h"
#include "mock_i2c_master.h"  // Mock for I2C operations

// Test fixtures
static scd41_handle_t test_handle;
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
    if (scd41_is_initialized(&test_handle)) {
        scd41_deinit(&test_handle);
    }
}

/**
 * @test Test SCD41 initialization
 */
void test_scd41_init_success(void)
{
    bool result = scd41_init(&test_handle, mock_i2c_bus, 0x62);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(scd41_is_initialized(&test_handle));
    TEST_ASSERT_EQUAL(0x62, test_handle.device_address);
    TEST_ASSERT_FALSE(test_handle.measurement_started);
}

/**
 * @test Test SCD41 initialization with default address
 */
void test_scd41_init_default_address(void)
{
    bool result = scd41_init(&test_handle, mock_i2c_bus, 0);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0x62, test_handle.device_address);
}

/**
 * @test Test SCD41 initialization with invalid parameters
 */
void test_scd41_init_null_handle(void)
{
    bool result = scd41_init(NULL, mock_i2c_bus, 0x62);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SCD41 initialization with null I2C bus
 */
void test_scd41_init_null_i2c_bus(void)
{
    bool result = scd41_init(&test_handle, NULL, 0x62);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test starting periodic measurement
 */
void test_scd41_start_periodic_measurement_success(void)
{
    // Initialize sensor
    scd41_init(&test_handle, mock_i2c_bus, 0x62);

    // Mock I2C transmit for start command
    uint8_t cmd[2] = {0x21, 0xB1};
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x62, cmd, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();

    bool result = scd41_start_periodic_measurement(&test_handle);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(test_handle.measurement_started);
}

/**
 * @test Test starting periodic measurement twice
 */
void test_scd41_start_periodic_measurement_already_started(void)
{
    // Initialize and start
    scd41_init(&test_handle, mock_i2c_bus, 0x62);
    
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x62, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    scd41_start_periodic_measurement(&test_handle);

    // Try to start again
    bool result = scd41_start_periodic_measurement(&test_handle);
    
    TEST_ASSERT_TRUE(result);  // Should return true without error
}

/**
 * @test Test stopping periodic measurement
 */
void test_scd41_stop_periodic_measurement_success(void)
{
    // Initialize and start measurement
    scd41_init(&test_handle, mock_i2c_bus, 0x62);
    test_handle.measurement_started = true;

    // Mock I2C transmit for stop command
    uint8_t cmd[2] = {0x3F, 0x86};
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x62, cmd, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();

    bool result = scd41_stop_periodic_measurement(&test_handle);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(test_handle.measurement_started);
}

/**
 * @test Test read with valid data
 */
void test_scd41_read_success(void)
{
    // Initialize and start measurement
    scd41_init(&test_handle, mock_i2c_bus, 0x62);
    test_handle.measurement_started = true;

    // Mock send read command
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x62, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();

    // Mock receive measurement data
    // CO2: 800 ppm (0x0320), Temp: 25°C, Humidity: 50% RH
    uint8_t rx_data[9] = {
        0x03, 0x20, 0x2A,  // CO2: 800 ppm + CRC
        0x66, 0x66, 0x93,  // Temperature: ~25°C + CRC
        0x80, 0x00, 0xA2   // Humidity: ~50% RH + CRC
    };
    
    i2c_master_receive_ExpectAndReturn(
        mock_i2c_bus, 0x62, NULL, 9, -1, ESP_OK
    );
    i2c_master_receive_IgnoreArg_rx_buffer();
    i2c_master_receive_ReturnArrayThruPtr_rx_buffer(rx_data, 9);

    scd41_data_t data;
    bool result = scd41_read(&test_handle, &data);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(data.valid);
    TEST_ASSERT_EQUAL_UINT16(800, data.co2_ppm);
    TEST_ASSERT_FLOAT_WITHIN(5.0, 25.0, data.temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(5.0, 50.0, data.humidity_rh);
}

/**
 * @test Test read without starting measurement
 */
void test_scd41_read_measurement_not_started(void)
{
    // Initialize without starting measurement
    scd41_init(&test_handle, mock_i2c_bus, 0x62);

    scd41_data_t data;
    bool result = scd41_read(&test_handle, &data);

    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(data.valid);
}

/**
 * @test Test read with I2C error
 */
void test_scd41_read_i2c_error(void)
{
    // Initialize and start measurement
    scd41_init(&test_handle, mock_i2c_bus, 0x62);
    test_handle.measurement_started = true;

    // Mock I2C error on transmit
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x62, NULL, 2, -1, ESP_ERR_TIMEOUT
    );
    i2c_master_transmit_IgnoreArg_write_buffer();

    scd41_data_t data;
    bool result = scd41_read(&test_handle, &data);

    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(data.valid);
}

/**
 * @test Test read CO2 only
 */
void test_scd41_read_co2_success(void)
{
    // Initialize and start measurement
    scd41_init(&test_handle, mock_i2c_bus, 0x62);
    test_handle.measurement_started = true;

    // Mock I2C operations
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x62, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();

    uint8_t rx_data[9] = {
        0x03, 0x20, 0x2A,  // CO2: 800 ppm
        0x66, 0x66, 0x93,  // Temperature
        0x80, 0x00, 0xA2   // Humidity
    };
    
    i2c_master_receive_ExpectAndReturn(
        mock_i2c_bus, 0x62, NULL, 9, -1, ESP_OK
    );
    i2c_master_receive_IgnoreArg_rx_buffer();
    i2c_master_receive_ReturnArrayThruPtr_rx_buffer(rx_data, 9);

    uint16_t co2_ppm;
    bool result = scd41_read_co2(&test_handle, &co2_ppm);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT16(800, co2_ppm);
}

/**
 * @test Test deinit
 */
void test_scd41_deinit(void)
{
    // Initialize
    scd41_init(&test_handle, mock_i2c_bus, 0x62);
    
    // Start measurement
    test_handle.measurement_started = true;
    
    // Mock stop command when deinit is called
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x62, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();
    
    scd41_deinit(&test_handle);
    
    TEST_ASSERT_FALSE(scd41_is_initialized(&test_handle));
    TEST_ASSERT_FALSE(test_handle.measurement_started);
}

/**
 * @test Test self-test
 */
void test_scd41_self_test_success(void)
{
    // Initialize
    scd41_init(&test_handle, mock_i2c_bus, 0x62);

    // Mock self-test command
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x62, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();

    // Mock self-test result (0x0000 = pass)
    uint8_t result_data[3] = {0x00, 0x00, 0x81};  // Pass + CRC
    i2c_master_receive_ExpectAndReturn(
        mock_i2c_bus, 0x62, NULL, 3, -1, ESP_OK
    );
    i2c_master_receive_IgnoreArg_rx_buffer();
    i2c_master_receive_ReturnArrayThruPtr_rx_buffer(result_data, 3);

    bool result = scd41_self_test(&test_handle);

    TEST_ASSERT_TRUE(result);
}

/**
 * @test Test self-test failure
 */
void test_scd41_self_test_failure(void)
{
    // Initialize
    scd41_init(&test_handle, mock_i2c_bus, 0x62);

    // Mock self-test command
    i2c_master_transmit_ExpectAndReturn(
        mock_i2c_bus, 0x62, NULL, 2, -1, ESP_OK
    );
    i2c_master_transmit_IgnoreArg_write_buffer();

    // Mock self-test result (non-zero = fail)
    uint8_t result_data[3] = {0x00, 0x01, 0xB0};  // Fail + CRC
    i2c_master_receive_ExpectAndReturn(
        mock_i2c_bus, 0x62, NULL, 3, -1, ESP_OK
    );
    i2c_master_receive_IgnoreArg_rx_buffer();
    i2c_master_receive_ReturnArrayThruPtr_rx_buffer(result_data, 3);

    bool result = scd41_self_test(&test_handle);

    TEST_ASSERT_FALSE(result);
}
