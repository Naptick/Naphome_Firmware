/**
 * @file main.c
 * @brief Naphome Firmware - Main Application with Naptick Integration
 *
 * This is the main orchestration layer that coordinates:
 * - BLE provisioning for WiFi credentials
 * - WiFi connection management
 * - HTTP device registration
 * - MQTT communication
 * - UART sensor data ingestion
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "naptick_config.h"
#include "naptick_http.h"
#include "naptick_mqtt.h"
#include "naptick_uart.h"
#include "wifi_manager.h"
#include "somnus_ble.h"

static const char *TAG = "naphome_main";

/* Forward declarations */
static void on_wifi_state_change(bool connected, void *user_ctx);
static bool on_ble_wifi_connect(const char *ssid, const char *password,
                                const char *user_token, bool is_production, void *ctx);
static void on_mqtt_message(const char *topic, int topic_len,
                           const char *data, int data_len);
static void orchestration_task(void *param);

/**
 * @brief WiFi state change callback
 *
 * Called by wifi_manager when connection state changes.
 */
static void on_wifi_state_change(bool connected, void *user_ctx)
{
    naptick_system_t *sys = naptick_config_get_system();

    if (connected) {
        ESP_LOGI(TAG, "WiFi connected - triggering HTTP registration");
        xEventGroupSetBits(sys->event_group, NAPTICK_EVT_WIFI_CONNECTED);
        xEventGroupSetBits(sys->event_group, NAPTICK_EVT_HTTP_REGISTER);
    } else {
        ESP_LOGW(TAG, "WiFi disconnected");
        xEventGroupClearBits(sys->event_group, NAPTICK_EVT_WIFI_CONNECTED);
    }
}

/**
 * @brief BLE WiFi connect callback
 *
 * Called by somnus_ble when mobile app sends WiFi credentials.
 */
static bool on_ble_wifi_connect(const char *ssid, const char *password,
                                const char *user_token, bool is_production, void *ctx)
{
    ESP_LOGI(TAG, "BLE received WiFi credentials for: %s", ssid);

    /* Store credentials in naptick_config */
    naptick_config_set_wifi(ssid, password);
    naptick_config_set_token(user_token);

    /* Trigger WiFi connection via event */
    naptick_system_t *sys = naptick_config_get_system();
    xEventGroupSetBits(sys->event_group, NAPTICK_EVT_WIFI_CONNECT_REQ);

    /* Wait for connection result */
    EventBits_t bits = xEventGroupWaitBits(
        sys->event_group,
        NAPTICK_EVT_WIFI_CONNECTED | NAPTICK_EVT_WIFI_FAIL,
        pdTRUE,   /* Clear bits on return */
        pdFALSE,  /* Any bit satisfies */
        pdMS_TO_TICKS(20000)  /* 20 second timeout */
    );

    if (bits & NAPTICK_EVT_WIFI_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connection successful via BLE provisioning");
        return true;
    } else {
        ESP_LOGE(TAG, "WiFi connection failed via BLE provisioning");
        return false;
    }
}

/**
 * @brief MQTT message callback
 *
 * Called when device receives an MQTT message.
 */
static void on_mqtt_message(const char *topic, int topic_len,
                           const char *data, int data_len)
{
    ESP_LOGI(TAG, "MQTT message on topic: %.*s", topic_len, topic);
    ESP_LOGI(TAG, "  Data: %.*s", data_len, data);

    /* TODO: Process device commands (LED, SongChange, etc.) */
}

/**
 * @brief Start BLE provisioning mode
 */
static void start_ble_provisioning(void)
{
    ESP_LOGI(TAG, "Starting BLE provisioning...");

    somnus_ble_config_t ble_config = {
        .connect_cb = on_ble_wifi_connect,
        .connect_ctx = NULL,
        .device_command_cb = NULL,
        .device_command_ctx = NULL,
    };

    esp_err_t err = somnus_ble_start(&ble_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start BLE: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "BLE provisioning started - waiting for mobile app");
    }
}

/**
 * @brief Main orchestration task
 *
 * Handles event-driven state machine for the device.
 */
static void orchestration_task(void *param)
{
    naptick_system_t *sys = naptick_config_get_system();

    ESP_LOGI(TAG, "Orchestration task started");

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            sys->event_group,
            NAPTICK_EVT_HTTP_REGISTER | NAPTICK_EVT_WIFI_CONNECT_REQ | NAPTICK_EVT_WIFI_SCAN_REQ,
            pdTRUE,   /* Clear bits on return */
            pdFALSE,  /* Any bit satisfies */
            portMAX_DELAY
        );

        ESP_LOGI(TAG, "Orchestration: received events 0x%lx", (unsigned long)bits);

        /* Handle WiFi scan request (from BLE) */
        if (bits & NAPTICK_EVT_WIFI_SCAN_REQ) {
            ESP_LOGI(TAG, "WiFi scan requested");
            /* BLE component handles scan internally */
        }

        /* Handle WiFi connect request */
        if (bits & NAPTICK_EVT_WIFI_CONNECT_REQ) {
            ESP_LOGI(TAG, "WiFi connect requested");
            esp_err_t err = wifi_manager_connect(
                sys->config.wifi_ssid,
                sys->config.wifi_pass,
                pdMS_TO_TICKS(15000)
            );
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "WiFi connection failed: %s", esp_err_to_name(err));
                xEventGroupSetBits(sys->event_group, NAPTICK_EVT_WIFI_FAIL);
            }
        }

        /* Handle HTTP registration */
        if (bits & NAPTICK_EVT_HTTP_REGISTER) {
            ESP_LOGI(TAG, "Starting HTTP device registration...");

            esp_err_t err = naptick_http_register_device();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Device registration successful");

                /* Save configuration to NVS */
                naptick_config_save();

                /* Start MQTT */
                ESP_LOGI(TAG, "Starting MQTT client...");
                err = naptick_mqtt_start(on_mqtt_message);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start MQTT: %s", esp_err_to_name(err));
                }

                /* Enable sensor uploads */
                xEventGroupSetBits(sys->event_group, NAPTICK_EVT_SENSOR_UPLOAD);
                ESP_LOGI(TAG, "Sensor uploads enabled");
            } else {
                ESP_LOGE(TAG, "Device registration failed");
            }
        }
    }
}

/**
 * @brief Application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Naphome Firmware Starting...");
    ESP_LOGI(TAG, "Version: 1.0.0");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "========================================");

    /* 1. Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    /* 2. Initialize naptick_config (creates event group, queue, device ID) */
    ret = naptick_config_init();
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "Naptick config initialized");

    /* 3. Initialize WiFi manager (but don't connect yet) */
    ret = wifi_manager_start();
    ESP_ERROR_CHECK(ret);
    wifi_manager_set_state_callback(on_wifi_state_change, NULL);
    ESP_LOGI(TAG, "WiFi manager started");

    /* 4. Try to load saved configuration */
    if (naptick_config_load() == ESP_OK && naptick_config_is_provisioned()) {
        /* Configuration exists - connect to WiFi */
        naptick_system_t *sys = naptick_config_get_system();
        ESP_LOGI(TAG, "Found saved WiFi config: %s", sys->config.wifi_ssid);
        ESP_LOGI(TAG, "Connecting to saved WiFi network...");

        ret = wifi_manager_connect(
            sys->config.wifi_ssid,
            sys->config.wifi_pass,
            pdMS_TO_TICKS(15000)
        );

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to connect to saved WiFi, starting BLE provisioning");
            start_ble_provisioning();
        }
    } else {
        /* No saved configuration - start BLE provisioning */
        ESP_LOGI(TAG, "No saved WiFi config found");
        start_ble_provisioning();
    }

    /* 5. Initialize and start UART for sensor data */
    ret = naptick_uart_init();
    if (ret == ESP_OK) {
        naptick_uart_start();
        ESP_LOGI(TAG, "UART sensor task started");
    } else {
        ESP_LOGW(TAG, "Failed to initialize UART: %s", esp_err_to_name(ret));
    }

    /* 6. Start orchestration task */
    xTaskCreate(
        orchestration_task,
        "orchestration",
        8192,
        NULL,
        5,
        NULL
    );

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Naphome firmware initialized");
    ESP_LOGI(TAG, "Device ID: %s", naptick_config_get_device_id());
    ESP_LOGI(TAG, "========================================");
}
