/**
 * @file naptick_uart.c
 * @brief Naptick UART sensor data ingestion implementation
 */

#include "naptick_uart.h"
#include "naptick_config.h"
#include "naptick_http.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "naptick_uart";

#ifndef CONFIG_NAPTICK_UART_PORT_NUM
#define CONFIG_NAPTICK_UART_PORT_NUM 1
#endif

#ifndef CONFIG_NAPTICK_UART_BAUD_RATE
#define CONFIG_NAPTICK_UART_BAUD_RATE 9600
#endif

#ifndef CONFIG_NAPTICK_UART_TX_PIN
#define CONFIG_NAPTICK_UART_TX_PIN 17
#endif

#ifndef CONFIG_NAPTICK_UART_RX_PIN
#define CONFIG_NAPTICK_UART_RX_PIN 18
#endif

#ifndef CONFIG_NAPTICK_UART_RX_BUF_SIZE
#define CONFIG_NAPTICK_UART_RX_BUF_SIZE 2048
#endif

#ifndef CONFIG_NAPTICK_UART_TASK_STACK
#define CONFIG_NAPTICK_UART_TASK_STACK 4096
#endif

#define UART_PORT_NUM  CONFIG_NAPTICK_UART_PORT_NUM
#define UART_TX_PIN    CONFIG_NAPTICK_UART_TX_PIN
#define UART_RX_PIN    CONFIG_NAPTICK_UART_RX_PIN
#define UART_BAUD_RATE CONFIG_NAPTICK_UART_BAUD_RATE
#define UART_RX_BUF    CONFIG_NAPTICK_UART_RX_BUF_SIZE

static TaskHandle_t s_uart_task = NULL;
static naptick_sensor_data_t s_sensor_data = {0};
static char s_rx_buffer[CONFIG_NAPTICK_UART_RX_BUF_SIZE];
static int s_rx_buffer_pos = 0;

static void parse_sensor_json(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse sensor JSON");
        return;
    }

    /* Parse sensor values */
    cJSON *temp = cJSON_GetObjectItem(root, "temperature");
    cJSON *humidity = cJSON_GetObjectItem(root, "humidity");
    cJSON *co2 = cJSON_GetObjectItem(root, "co2");
    cJSON *voc = cJSON_GetObjectItem(root, "vocIndex");
    cJSON *pm25 = cJSON_GetObjectItem(root, "pm25");
    cJSON *light = cJSON_GetObjectItem(root, "ambientLightLux");
    cJSON *noise = cJSON_GetObjectItem(root, "noiseDb");
    cJSON *motion = cJSON_GetObjectItem(root, "motionDetected");

    if (cJSON_IsNumber(temp)) s_sensor_data.temperature = (float)temp->valuedouble;
    if (cJSON_IsNumber(humidity)) s_sensor_data.humidity = (float)humidity->valuedouble;
    if (cJSON_IsNumber(co2)) s_sensor_data.co2 = (float)co2->valuedouble;
    if (cJSON_IsNumber(voc)) s_sensor_data.voc_index = (float)voc->valuedouble;
    if (cJSON_IsNumber(pm25)) s_sensor_data.pm25 = (float)pm25->valuedouble;
    if (cJSON_IsNumber(light)) s_sensor_data.ambient_light_lux = light->valueint;
    if (cJSON_IsNumber(noise)) s_sensor_data.noise_db = noise->valueint;
    if (cJSON_IsBool(motion)) s_sensor_data.motion_detected = cJSON_IsTrue(motion);

    s_sensor_data.valid = true;

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Sensor data updated: temp=%.1f, humidity=%.1f, co2=%.0f",
             s_sensor_data.temperature, s_sensor_data.humidity, s_sensor_data.co2);
}

static void uart_rx_task(void *param)
{
    uint8_t byte;
    naptick_system_t *sys = naptick_config_get_system();

    ESP_LOGI(TAG, "UART receive task started");

    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, &byte, 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            if (byte == '\n' || byte == '\r') {
                if (s_rx_buffer_pos > 0) {
                    s_rx_buffer[s_rx_buffer_pos] = '\0';
                    ESP_LOGI(TAG, "Received: %s", s_rx_buffer);

                    /* Parse JSON */
                    parse_sensor_json(s_rx_buffer);

                    /* Check if we should upload */
                    EventBits_t bits = xEventGroupGetBits(sys->event_group);
                    if (bits & NAPTICK_EVT_SENSOR_UPLOAD) {
                        /* Build and upload sensor payload */
                        char payload[1024];
                        esp_err_t err = naptick_http_build_sensor_payload(
                            s_sensor_data.temperature,
                            s_sensor_data.humidity,
                            s_sensor_data.co2,
                            s_sensor_data.voc_index,
                            s_sensor_data.pm25,
                            s_sensor_data.ambient_light_lux,
                            s_sensor_data.noise_db,
                            s_sensor_data.motion_detected,
                            payload,
                            sizeof(payload)
                        );

                        if (err == ESP_OK) {
                            naptick_http_upload_sensors(payload);
                        }
                    }

                    s_rx_buffer_pos = 0;
                }
            } else {
                if (s_rx_buffer_pos < sizeof(s_rx_buffer) - 1) {
                    s_rx_buffer[s_rx_buffer_pos++] = byte;
                }
            }
        }
    }
}

esp_err_t naptick_uart_init(void)
{
    ESP_LOGI(TAG, "Initializing UART%d (TX=%d, RX=%d, baud=%d)",
             UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);

    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(UART_PORT_NUM, UART_RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(UART_PORT_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "UART initialized");
    return ESP_OK;
}

esp_err_t naptick_uart_start(void)
{
    if (s_uart_task) {
        ESP_LOGW(TAG, "UART task already running");
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreate(
        uart_rx_task,
        "naptick_uart",
        CONFIG_NAPTICK_UART_TASK_STACK,
        NULL,
        5,
        &s_uart_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UART task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UART task started");
    return ESP_OK;
}

esp_err_t naptick_uart_stop(void)
{
    if (s_uart_task) {
        vTaskDelete(s_uart_task);
        s_uart_task = NULL;
        ESP_LOGI(TAG, "UART task stopped");
    }
    return ESP_OK;
}

const naptick_sensor_data_t* naptick_uart_get_sensor_data(void)
{
    return &s_sensor_data;
}
