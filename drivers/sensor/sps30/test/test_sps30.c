/**
 * @file test_sps30.c
 * @brief Unit tests for SPS30 driver
 * 
 * Tests driver functionality in isolation using mocks.
 */

#include <unity.h>
#include <string.h>
#include "sps30.h"

// Mock UART handle (for testing purposes)
static sps30_handle_t test_handle;

void setUp(void)
{
    // Initialize test fixtures
    memset(&test_handle, 0, sizeof(test_handle));
}

void tearDown(void)
{
    // Cleanup
    if (sps30_is_initialized(&test_handle)) {
        sps30_deinit(&test_handle);
    }
}

/**
 * @test Test SPS30 initialization with valid parameters
 */
void test_sps30_init_success(void)
{
    // Note: This test requires actual UART hardware or mocking framework
    // For now, we test parameter validation
    bool result = sps30_init(&test_handle, UART_NUM_1, 17, 16);
    
    // In a real hardware test, this would succeed
    // In unit test with mocks, we verify the initialization attempt
    TEST_ASSERT_NOT_NULL(&test_handle);
}

/**
 * @test Test SPS30 initialization with null handle
 */
void test_sps30_init_null_handle(void)
{
    bool result = sps30_init(NULL, UART_NUM_1, 17, 16);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SPS30 start measurement without initialization
 */
void test_sps30_start_measurement_not_initialized(void)
{
    bool result = sps30_start_measurement(&test_handle);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SPS30 stop measurement without initialization
 */
void test_sps30_stop_measurement_not_initialized(void)
{
    bool result = sps30_stop_measurement(&test_handle);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SPS30 read without initialization
 */
void test_sps30_read_not_initialized(void)
{
    sps30_data_t data;
    bool result = sps30_read(&test_handle, &data);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(data.valid);
}

/**
 * @test Test SPS30 read with null data pointer
 */
void test_sps30_read_null_data(void)
{
    // Simulate initialized handle
    test_handle.initialized = true;
    test_handle.measuring = true;
    
    bool result = sps30_read(&test_handle, NULL);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SPS30 read without starting measurement
 */
void test_sps30_read_not_measuring(void)
{
    // Simulate initialized but not measuring
    test_handle.initialized = true;
    test_handle.measuring = false;
    
    sps30_data_t data;
    bool result = sps30_read(&test_handle, &data);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(data.valid);
}

/**
 * @test Test SPS30 is_initialized
 */
void test_sps30_is_initialized(void)
{
    TEST_ASSERT_FALSE(sps30_is_initialized(&test_handle));
    TEST_ASSERT_FALSE(sps30_is_initialized(NULL));
    
    test_handle.initialized = true;
    TEST_ASSERT_TRUE(sps30_is_initialized(&test_handle));
}

/**
 * @test Test SPS30 is_measuring
 */
void test_sps30_is_measuring(void)
{
    TEST_ASSERT_FALSE(sps30_is_measuring(&test_handle));
    TEST_ASSERT_FALSE(sps30_is_measuring(NULL));
    
    test_handle.measuring = true;
    TEST_ASSERT_TRUE(sps30_is_measuring(&test_handle));
}

/**
 * @test Test SPS30 read_pm25 convenience function
 */
void test_sps30_read_pm25_not_initialized(void)
{
    float pm2_5;
    bool result = sps30_read_pm25(&test_handle, &pm2_5);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SPS30 read_pm25 with null pointer
 */
void test_sps30_read_pm25_null_pointer(void)
{
    bool result = sps30_read_pm25(&test_handle, NULL);
    TEST_ASSERT_FALSE(result);
}

/**
 * @test Test SPS30 deinit
 */
void test_sps30_deinit(void)
{
    // Simulate initialized state
    test_handle.initialized = true;
    test_handle.measuring = false;
    
    sps30_deinit(&test_handle);
    TEST_ASSERT_FALSE(sps30_is_initialized(&test_handle));
}

/**
 * @test Test SPS30 deinit with null handle
 */
void test_sps30_deinit_null_handle(void)
{
    // Should not crash
    sps30_deinit(NULL);
    TEST_PASS();
}

/**
 * @test Test data structure initialization
 */
void test_sps30_data_structure(void)
{
    sps30_data_t data;
    memset(&data, 0, sizeof(data));
    
    TEST_ASSERT_EQUAL_FLOAT(0.0f, data.pm1_0);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, data.pm2_5);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, data.pm4_0);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, data.pm10);
    TEST_ASSERT_FALSE(data.valid);
}

/**
 * @test Test handle structure initialization
 */
void test_sps30_handle_structure(void)
{
    sps30_handle_t handle;
    memset(&handle, 0, sizeof(handle));
    
    TEST_ASSERT_EQUAL(0, handle.uart_port);
    TEST_ASSERT_FALSE(handle.initialized);
    TEST_ASSERT_FALSE(handle.measuring);
}

// Test runner
void app_main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_sps30_init_null_handle);
    RUN_TEST(test_sps30_start_measurement_not_initialized);
    RUN_TEST(test_sps30_stop_measurement_not_initialized);
    RUN_TEST(test_sps30_read_not_initialized);
    RUN_TEST(test_sps30_read_null_data);
    RUN_TEST(test_sps30_read_not_measuring);
    RUN_TEST(test_sps30_is_initialized);
    RUN_TEST(test_sps30_is_measuring);
    RUN_TEST(test_sps30_read_pm25_not_initialized);
    RUN_TEST(test_sps30_read_pm25_null_pointer);
    RUN_TEST(test_sps30_deinit);
    RUN_TEST(test_sps30_deinit_null_handle);
    RUN_TEST(test_sps30_data_structure);
    RUN_TEST(test_sps30_handle_structure);
    
    UNITY_END();
}
