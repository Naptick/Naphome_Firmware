/**
 * @file main.c
 * @brief Naphome Phase 0.9 Firmware - Main Application
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "somnus_ble.h"

static const char *TAG = "naphome_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Naphome Phase 0.9 Firmware Starting...");
    ESP_LOGI(TAG, "Version: 0.9.0");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS initialized");

    // Start BLE service
    ESP_LOGI(TAG, "Starting BLE service...");
    somnus_ble_config_t ble_config = {0};  // Zero-initialize (all callbacks NULL)
    esp_err_t ble_err = somnus_ble_start(&ble_config);
    if (ble_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start BLE: %s", esp_err_to_name(ble_err));
    } else {
        ESP_LOGI(TAG, "BLE service started");
    }

    // Start Wi-Fi connection
    ESP_LOGI(TAG, "Starting Wi-Fi...");
    // Note: Wi-Fi initialization happens in BLE component when needed

    ESP_LOGI(TAG, "Naphome firmware initialized successfully");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // Main application logic
    }
}
