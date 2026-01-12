/**
 * @file naptick_config.h
 * @brief Naptick system configuration and state management
 */

#ifndef NAPTICK_CONFIG_H
#define NAPTICK_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Event group bit definitions */
#define NAPTICK_EVT_WIFI_SCAN_REQ      (1 << 0)
#define NAPTICK_EVT_WIFI_CONNECT_REQ   (1 << 1)
#define NAPTICK_EVT_WIFI_CONNECTED     (1 << 2)
#define NAPTICK_EVT_WIFI_FAIL          (1 << 3)
#define NAPTICK_EVT_HTTP_REGISTER      (1 << 4)
#define NAPTICK_EVT_SENSOR_UPLOAD      (1 << 5)

/* Configuration magic number and version for NVS validation */
#define NAPTICK_CONFIG_MAGIC    0x4E415054  /* "NAPT" */
#define NAPTICK_CONFIG_VERSION  1

/* BLE message structure for queue communication */
typedef struct {
    uint8_t *data;
    uint16_t len;
} naptick_ble_msg_t;

/* Persistent configuration stored in NVS */
typedef struct {
    uint32_t magic;
    uint32_t version;
    char wifi_ssid[32];
    char wifi_pass[64];
    char token[512];
    char mqtt_url[128];
    char mqtt_client_id[64];
    uint8_t registered;
    uint8_t reserved[31];
} naptick_config_t;

/* Device information derived at runtime */
typedef struct {
    uint8_t mac_raw[6];
    char mac_str[13];
    char device_id[32];
} naptick_device_info_t;

/* Global system resources */
typedef struct {
    naptick_config_t config;
    naptick_device_info_t dev_info;
    EventGroupHandle_t event_group;
    QueueHandle_t ble_queue;
} naptick_system_t;

/**
 * @brief Initialize the configuration system
 *
 * Creates event group, BLE queue, and generates device ID from MAC address.
 * Must be called before any other naptick_config functions.
 *
 * @return ESP_OK on success
 */
esp_err_t naptick_config_init(void);

/**
 * @brief Save current configuration to NVS
 *
 * Persists WiFi credentials, token, and registration state.
 *
 * @return ESP_OK on success
 */
esp_err_t naptick_config_save(void);

/**
 * @brief Load configuration from NVS
 *
 * Loads and validates stored configuration. Returns ESP_ERR_NOT_FOUND
 * if no valid configuration exists.
 *
 * @return ESP_OK if valid config loaded, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t naptick_config_load(void);

/**
 * @brief Reset to factory defaults
 *
 * Erases NVS configuration and restarts device.
 * Device will enter BLE provisioning mode on next boot.
 */
void naptick_config_reset(void);

/**
 * @brief Get the device ID string
 *
 * @return Device ID in format "NAPTICK_XXXXXXXXXXXX"
 */
const char* naptick_config_get_device_id(void);

/**
 * @brief Get pointer to global system resources
 *
 * @return Pointer to naptick_system_t structure
 */
naptick_system_t* naptick_config_get_system(void);

/**
 * @brief Set WiFi credentials
 *
 * @param ssid WiFi SSID (max 31 chars)
 * @param password WiFi password (max 63 chars)
 * @return ESP_OK on success
 */
esp_err_t naptick_config_set_wifi(const char *ssid, const char *password);

/**
 * @brief Set authentication token
 *
 * @param token Bearer token for API authentication (max 511 chars)
 * @return ESP_OK on success
 */
esp_err_t naptick_config_set_token(const char *token);

/**
 * @brief Check if device has valid stored configuration
 *
 * @return true if valid config exists in NVS
 */
bool naptick_config_is_provisioned(void);

#ifdef __cplusplus
}
#endif

#endif /* NAPTICK_CONFIG_H */
