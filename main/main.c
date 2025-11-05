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

    // TODO: Initialize I2C bus
    // TODO: Initialize drivers
    // TODO: Initialize components
    // TODO: Start tasks

    ESP_LOGI(TAG, "Naphome firmware initialized successfully");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // TODO: Main application logic
    }
}
