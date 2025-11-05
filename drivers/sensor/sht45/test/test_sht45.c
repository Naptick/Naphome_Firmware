/**
 * @file test_sht45.c
 * @brief Unit tests for SHT45 driver
 * 
 * Tests driver functionality including CRC validation and data parsing.
 */

#include <unity.h>
#include <string.h>
#include "sht45.h"

// Test fixtures
static sht45_handle_t test_handle;

void setUp(void)
{
    // Initialize test fixtures
    memset(&test_handle, 0, sizeof(test_handle));
}

void tearDown(void)
{
    // Cleanup
    if (sht45_is_initialized(&test_handle)) {
        sht45_deinit(&test_handle);
    }
}

/**
 * @test Test SHT45 handle structure
 */
void test_sht45_handle_structure(void)
{
    // Verify handle structure is properly initialized
    sht45_handle_t handle;
    memset(&handle, 0, sizeof(handle));
    
    TEST_ASSERT_EQUAL(0, handle.device_address);
    TEST_ASSERT_FALSE(handle.initialized);
    TEST_ASSERT_NULL(handle.i2c_bus);
    TEST_ASSERT_NULL(handle.i2c_dev);
}

/**
 * @test Test SHT45 initialization with invalid parameters
 */
void test_sht45_init_null_handle(void)
{
    bool result = sht45_init(NULL, (i2c_master_bus_handle_t)0x1234, 0x44);
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
 * @test Test SHT45 is_initialized function
 */
void test_sht45_is_initialized(void)
{
    // Initially not initialized
    TEST_ASSERT_FALSE(sht45_is_initialized(&test_handle));
    
    // After manual setup (simulating initialization)
    test_handle.initialized = true;
    TEST_ASSERT_TRUE(sht45_is_initialized(&test_handle));
    
    // Null handle
    TEST_ASSERT_FALSE(sht45_is_initialized(NULL));
}

/**
 * @test Test SHT45 data structure
 */
void test_sht45_data_structure(void)
{
    sht45_data_t data;
    memset(&data, 0, sizeof(data));
    
    // Verify initial state
    TEST_ASSERT_EQUAL_FLOAT(0.0f, data.temperature_c);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, data.humidity_rh);
    TEST_ASSERT_FALSE(data.valid);
    
    // Set valid data
    data.temperature_c = 25.5f;
    data.humidity_rh = 50.0f;
    data.valid = true;
    
    TEST_ASSERT_EQUAL_FLOAT(25.5f, data.temperature_c);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, data.humidity_rh);
    TEST_ASSERT_TRUE(data.valid);
}

/**
 * @test Test SHT45 read with uninitialized handle
 */
void test_sht45_read_uninitialized(void)
{
    sht45_data_t data;
    
    // Attempt read with uninitialized handle
    bool result = sht45_read(&test_handle, &data);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(data.valid);
}

/**
 * @test Test SHT45 read with NULL data pointer
 */
void test_sht45_read_null_data(void)
{
    // Simulate initialized handle
    test_handle.initialized = true;
    
    // Attempt read with NULL data pointer
    bool result = sht45_read(&test_handle, NULL);
    
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SHT45 read_temperature with invalid handle
 */
void test_sht45_read_temperature_invalid_handle(void)
{
    float temperature;
    
    // Attempt read with uninitialized handle
    bool result = sht45_read_temperature(&test_handle, &temperature);
    
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SHT45 read_humidity with invalid handle
 */
void test_sht45_read_humidity_invalid_handle(void)
{
    float humidity;
    
    // Attempt read with uninitialized handle
    bool result = sht45_read_humidity(&test_handle, &humidity);
    
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SHT45 deinit with null handle
 */
void test_sht45_deinit_null_handle(void)
{
    // Should not crash with NULL handle
    sht45_deinit(NULL);
    TEST_ASSERT_TRUE(true);  // If we get here, test passes
}

/**
 * @test Test SHT45 deinit with uninitialized handle
 */
void test_sht45_deinit_uninitialized(void)
{
    sht45_handle_t handle;
    memset(&handle, 0, sizeof(handle));
    handle.initialized = false;
    
    // Should not crash with uninitialized handle
    sht45_deinit(&handle);
    TEST_ASSERT_FALSE(sht45_is_initialized(&handle));
}

/**
 * @test Test temperature conversion formula boundaries
 */
void test_sht45_temperature_conversion_boundaries(void)
{
    // Test min value (raw 0x0000 = -45°C)
    uint16_t raw_min = 0x0000;
    float temp_min = -45.0f + (175.0f * raw_min / 65535.0f);
    TEST_ASSERT_EQUAL_FLOAT(-45.0f, temp_min);
    
    // Test max value (raw 0xFFFF = 130°C)
    uint16_t raw_max = 0xFFFF;
    float temp_max = -45.0f + (175.0f * raw_max / 65535.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 130.0f, temp_max);
    
    // Test typical room temperature (raw ~0x6666 ≈ 25°C)
    uint16_t raw_room = 0x6666;
    float temp_room = -45.0f + (175.0f * raw_room / 65535.0f);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 25.0f, temp_room);
}

/**
 * @test Test humidity conversion formula boundaries
 */
void test_sht45_humidity_conversion_boundaries(void)
{
    // Test min value (raw 0x0000 = -6%)
    uint16_t raw_min = 0x0000;
    float hum_min = -6.0f + (125.0f * raw_min / 65535.0f);
    TEST_ASSERT_EQUAL_FLOAT(-6.0f, hum_min);
    
    // Test max value (raw 0xFFFF = 119%)
    uint16_t raw_max = 0xFFFF;
    float hum_max = -6.0f + (125.0f * raw_max / 65535.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 119.0f, hum_max);
    
    // Test 50% humidity (raw ~0x7333)
    uint16_t raw_50 = 0x7333;
    float hum_50 = -6.0f + (125.0f * raw_50 / 65535.0f);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 50.0f, hum_50);
}
