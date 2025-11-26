/**
 * @file sensor_integration.c
 * @brief Unified sensor integration - samples all I2C sensors at 1Hz
 */

#include "sensor_integration.h"

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "driver/i2c.h"
// Compatibility layer for v5.0 API
#define i2c_master_bus_handle_t i2c_port_t
#define i2c_master_dev_handle_t i2c_cmd_handle_t
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

// Sensor drivers - TODO: Re-enable when component discovery is fixed
// #include "sht45.h"
// #include "sgp40.h"
// #include "scd40.h"
// #include "vcnl4040.h"
// #include "ec10.h"

// Sensor manager
#include "sensor_manager.h"

static const char *TAG = "sensor_integration";

// I2C configuration - Using UART pins (TXD/RXD) for sensor I2C bus
// ESP32-S3 UART0: TXD = GPIO43, RXD = GPIO44
#define I2C_MASTER_SCL_IO           43    // GPIO number for I2C master clock (TXD/UART0_TX)
#define I2C_MASTER_SDA_IO           44    // GPIO number for I2C master data (RXD/UART0_RX)
#define I2C_MASTER_NUM              0     // I2C master i2c port number
#define I2C_MASTER_FREQ_HZ          100000 // I2C master clock frequency
#define I2C_MASTER_TIMEOUT_MS       1000

// Sensor sampling rate: 1Hz = 1000ms
#define SENSOR_SAMPLE_INTERVAL_MS   1000

// Sensor handles - TODO: Re-enable when sensor driver components are available
static i2c_master_bus_handle_t s_i2c_bus = I2C_NUM_MAX; // Use invalid port instead of NULL
// static sht45_handle_t s_sht45;
// static sgp40_t s_sgp40;
// static scd40_t s_scd40;
// static vcnl4040_t s_vcnl4040;
// static ec10_t s_ec10;

static bool s_initialized = false;
static bool s_running = false;
static TaskHandle_t s_task_handle = NULL;

// Sensor data cache
typedef struct {
    float temperature_c;
    float humidity_rh;
    uint16_t voc_index;
    float co2_ppm;
    float temperature_co2_c;
    float humidity_co2_rh;
    uint16_t ambient_lux;
    uint16_t proximity;
    float ec_ms_per_cm;
    bool sht45_available;
    bool sgp40_available;
    bool scd40_available;
    bool vcnl4040_available;
    bool ec10_available;
    uint32_t last_update_ms;
} sensor_data_cache_t;

static sensor_data_cache_t s_sensor_cache = {0};

static void sensor_sampling_task(void *arg);
static bool sample_sht45_cb(cJSON *sensor_root);
static bool sample_sgp40_cb(cJSON *sensor_root);
static bool sample_scd40_cb(cJSON *sensor_root);
static bool sample_vcnl4040_cb(cJSON *sensor_root);
static bool sample_ec10_cb(cJSON *sensor_root);

esp_err_t sensor_integration_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing sensor integration...");

    // TODO: Re-enable when sensor driver components are available
    // Initialize I2C master bus
    // i2c_master_bus_config_t i2c_bus_config = {
    //     .i2c_port = I2C_MASTER_NUM,
    //     .sda_io_num = I2C_MASTER_SDA_IO,
    //     .scl_io_num = I2C_MASTER_SCL_IO,
    //     .clk_source = I2C_CLK_SRC_DEFAULT,
    //     .glitch_ignore_cnt = 7,
    //     .flags = {
    //         .enable_internal_pullup = true,
    //     },
    // };
    // i2c_master_bus_handle_t bus_handle = I2C_NUM_MAX;
    // esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &bus_handle);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
    //     return ret;
    // }
    // s_i2c_bus = bus_handle;
    
    // For now, mark all sensors as unavailable (will use synthetic data)
    s_sensor_cache.sht45_available = false;
    s_sensor_cache.sgp40_available = false;
    s_sensor_cache.scd40_available = false;
    s_sensor_cache.vcnl4040_available = false;
    s_sensor_cache.ec10_available = false;
    ESP_LOGI(TAG, "Sensor drivers disabled - using synthetic data");

    // Initialize sensor manager with 1Hz sampling (1000ms)
    sensor_manager_config_t mgr_cfg = {
        .publish_interval_ms = SENSOR_SAMPLE_INTERVAL_MS,
    };
    sensor_manager_init(&mgr_cfg);

    // Register all sensors
    sensor_manager_sensor_t sensors[] = {
        {.name = "sht45", .sample_cb = sample_sht45_cb},
        {.name = "sgp40", .sample_cb = sample_sgp40_cb},
        {.name = "scd40", .sample_cb = sample_scd40_cb},
        {.name = "vcnl4040", .sample_cb = sample_vcnl4040_cb},
        {.name = "ec10", .sample_cb = sample_ec10_cb},
    };

    for (size_t i = 0; i < sizeof(sensors) / sizeof(sensors[0]); i++) {
        esp_err_t err = sensor_manager_register(&sensors[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register sensor %s", sensors[i].name);
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Sensor integration initialized - sampling at 1Hz");
    return ESP_OK;
}

esp_err_t sensor_integration_start(void)
{
    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(sensor_integration_init(), TAG, "init failed");
    }

    if (s_running) {
        return ESP_OK;
    }

    // Start sensor manager
    ESP_RETURN_ON_ERROR(sensor_manager_start(), TAG, "sensor manager start failed");

    // Start sampling task
    BaseType_t ret = xTaskCreate(sensor_sampling_task,
                                 "sensor_sampling",
                                 4096,
                                 NULL,
                                 5,
                                 &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor sampling task");
        return ESP_ERR_NO_MEM;
    }

    s_running = true;
    ESP_LOGI(TAG, "Sensor integration started");
    return ESP_OK;
}

esp_err_t sensor_integration_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    sensor_manager_stop();
    s_running = false;

    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }

    ESP_LOGI(TAG, "Sensor integration stopped");
    return ESP_OK;
}

sensor_integration_data_t sensor_integration_get_data(void)
{
    sensor_integration_data_t data = {0};
    data.temperature_c = s_sensor_cache.temperature_c;
    data.humidity_rh = s_sensor_cache.humidity_rh;
    data.voc_index = s_sensor_cache.voc_index;
    data.co2_ppm = s_sensor_cache.co2_ppm;
    data.temperature_co2_c = s_sensor_cache.temperature_co2_c;
    data.humidity_co2_rh = s_sensor_cache.humidity_co2_rh;
    data.ambient_lux = s_sensor_cache.ambient_lux;
    data.proximity = s_sensor_cache.proximity;
    data.ec_ms_per_cm = s_sensor_cache.ec_ms_per_cm;
    data.sht45_available = s_sensor_cache.sht45_available;
    data.sgp40_available = s_sensor_cache.sgp40_available;
    data.scd40_available = s_sensor_cache.scd40_available;
    data.vcnl4040_available = s_sensor_cache.vcnl4040_available;
    data.ec10_available = s_sensor_cache.ec10_available;
    data.last_update_ms = s_sensor_cache.last_update_ms;
    return data;
}

static void sensor_sampling_task(void *arg)
{
    (void)arg;
    const TickType_t delay_ticks = pdMS_TO_TICKS(SENSOR_SAMPLE_INTERVAL_MS);
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "Sensor sampling task started (1Hz)");

    while (s_running) {
        // TODO: Re-enable when sensor driver components are available
        // For now, generate synthetic sensor data
        static uint32_t sensor_synthetic_counter = 0;
        sensor_synthetic_counter++;
        
        // Generate synthetic SHT45 data (temperature, humidity)
        s_sensor_cache.temperature_c = 22.0f + 3.0f * sinf(sensor_synthetic_counter * 0.01f);
        s_sensor_cache.humidity_rh = 50.0f + 10.0f * sinf(sensor_synthetic_counter * 0.008f);
        
        // Generate synthetic SGP40 data (VOC)
        s_sensor_cache.voc_index = 100 + (uint16_t)(50.0f * sinf(sensor_synthetic_counter * 0.01f));
        
        // Generate synthetic SCD40 data (CO2, temperature, humidity)
        s_sensor_cache.co2_ppm = 450.0f + 50.0f * sinf(sensor_synthetic_counter * 0.005f);
        s_sensor_cache.temperature_co2_c = s_sensor_cache.temperature_c;
        s_sensor_cache.humidity_co2_rh = s_sensor_cache.humidity_rh;
        
        // Generate synthetic VCNL4040 data (light, proximity)
        s_sensor_cache.ambient_lux = 200 + (uint16_t)(100.0f * sinf(sensor_synthetic_counter * 0.02f));
        s_sensor_cache.proximity = 0;  // No proximity by default

        // TODO: Re-enable when ec10 component is available
        // ec10_measurement_t ec10_data;
        // esp_err_t ec10_ret = ec10_read_measurement(&s_ec10, &ec10_data, pdMS_TO_TICKS(100));
        // if (ec10_ret == ESP_OK) {
        //     // EC10 is PM sensor, not EC - use PM2.5 as primary value
        //     s_sensor_cache.ec_ms_per_cm = ec10_data.pm2_5_ug_m3;  // Store PM2.5 value
        // Use synthetic data for now
        if (!s_sensor_cache.ec10_available) {
            // Generate synthetic PM2.5 data
            static uint32_t ec10_synthetic_counter = 0;
            ec10_synthetic_counter++;
            s_sensor_cache.ec_ms_per_cm = 15.0f + 5.0f * sinf(ec10_synthetic_counter * 0.01f);  // PM2.5 in μg/m³
        }

        s_sensor_cache.last_update_ms = esp_log_timestamp();

        // Log sensor readings
        ESP_LOGI(TAG, "Sensors: T=%.1f°C H=%.1f%% VOC=%u CO2=%.0fppm Lux=%u Prox=%u PM2.5=%.0f",
                 s_sensor_cache.temperature_c,
                 s_sensor_cache.humidity_rh,
                 s_sensor_cache.voc_index,
                 s_sensor_cache.co2_ppm,
                 s_sensor_cache.ambient_lux,
                 s_sensor_cache.proximity,
                 s_sensor_cache.ec_ms_per_cm);

        vTaskDelayUntil(&last_wake, delay_ticks);
    }

    vTaskDelete(NULL);
}

// TODO: Re-enable when sensor driver components are available
static bool sample_sht45_cb(cJSON *sensor_root)
{
    // Return synthetic data
    cJSON_AddNumberToObject(sensor_root, "temperature_c", s_sensor_cache.temperature_c);
    cJSON_AddNumberToObject(sensor_root, "humidity_rh", s_sensor_cache.humidity_rh);
    cJSON_AddBoolToObject(sensor_root, "synthetic", true);
    return true;
}

static bool sample_sgp40_cb(cJSON *sensor_root)
{
    // Return synthetic data
    cJSON_AddNumberToObject(sensor_root, "voc_index", s_sensor_cache.voc_index);
    cJSON_AddNumberToObject(sensor_root, "voc_ticks", s_sensor_cache.voc_index * 10);
    cJSON_AddBoolToObject(sensor_root, "synthetic", true);
    return true;
}

static bool sample_scd40_cb(cJSON *sensor_root)
{
    // Return synthetic data
    cJSON_AddNumberToObject(sensor_root, "co2_ppm", s_sensor_cache.co2_ppm);
    cJSON_AddNumberToObject(sensor_root, "temperature_c", s_sensor_cache.temperature_co2_c);
    cJSON_AddNumberToObject(sensor_root, "humidity_rh", s_sensor_cache.humidity_co2_rh);
    cJSON_AddBoolToObject(sensor_root, "synthetic", true);
    return true;
}

static bool sample_vcnl4040_cb(cJSON *sensor_root)
{
    // Return synthetic data
    cJSON_AddNumberToObject(sensor_root, "ambient_lux", s_sensor_cache.ambient_lux);
    cJSON_AddNumberToObject(sensor_root, "proximity", s_sensor_cache.proximity);
    cJSON_AddBoolToObject(sensor_root, "synthetic", true);
    return true;
}

static bool sample_ec10_cb(cJSON *sensor_root)
{
    // Return synthetic PM data
    float pm2_5 = s_sensor_cache.ec_ms_per_cm;  // Stored as PM2.5
    cJSON_AddNumberToObject(sensor_root, "pm1_0_ug_m3", (uint16_t)(pm2_5 * 0.7f));
    cJSON_AddNumberToObject(sensor_root, "pm2_5_ug_m3", (uint16_t)pm2_5);
    cJSON_AddNumberToObject(sensor_root, "pm10_ug_m3", (uint16_t)(pm2_5 * 1.5f));
    cJSON_AddBoolToObject(sensor_root, "synthetic", true);
    return true;
}
