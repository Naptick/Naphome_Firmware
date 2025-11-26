#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_stop(void);
bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_wait_for_connection(TickType_t ticks_to_wait);
esp_err_t wifi_manager_connect(const char *ssid, const char *password, TickType_t ticks_to_wait);

#ifdef __cplusplus
}
#endif

