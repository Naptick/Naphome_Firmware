/**
 * @file naptick_config.c
 * @brief Naptick system configuration and state management implementation
 */

#include "naptick_config.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "naptick_config";

#define NVS_NAMESPACE "naptick"
#define NVS_KEY_CONFIG "config"
#define BLE_QUEUE_SIZE 10

static naptick_system_t s_system = {0};
static bool s_initialized = false;

static void generate_device_id(void)
{
    esp_read_mac(s_system.dev_info.mac_raw, ESP_MAC_WIFI_STA);

    snprintf(s_system.dev_info.mac_str, sizeof(s_system.dev_info.mac_str),
             "%02X%02X%02X%02X%02X%02X",
             s_system.dev_info.mac_raw[0],
             s_system.dev_info.mac_raw[1],
             s_system.dev_info.mac_raw[2],
             s_system.dev_info.mac_raw[3],
             s_system.dev_info.mac_raw[4],
             s_system.dev_info.mac_raw[5]);

    snprintf(s_system.dev_info.device_id, sizeof(s_system.dev_info.device_id),
             "NAPTICK_%s", s_system.dev_info.mac_str);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Device ID: %s", s_system.dev_info.device_id);
    ESP_LOGI(TAG, "========================================");
}

esp_err_t naptick_config_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Create FreeRTOS event group */
    s_system.event_group = xEventGroupCreate();
    if (s_system.event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    /* Create BLE message queue */
    s_system.ble_queue = xQueueCreate(BLE_QUEUE_SIZE, sizeof(naptick_ble_msg_t));
    if (s_system.ble_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create BLE queue");
        vEventGroupDelete(s_system.event_group);
        return ESP_ERR_NO_MEM;
    }

    /* Generate device ID from MAC address */
    generate_device_id();

    /* Clear configuration */
    memset(&s_system.config, 0, sizeof(naptick_config_t));

    s_initialized = true;
    ESP_LOGI(TAG, "Configuration system initialized");

    return ESP_OK;
}

esp_err_t naptick_config_save(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    /* Set magic and version before saving */
    s_system.config.magic = NAPTICK_CONFIG_MAGIC;
    s_system.config.version = NAPTICK_CONFIG_VERSION;

    err = nvs_set_blob(handle, NVS_KEY_CONFIG, &s_system.config, sizeof(naptick_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write config: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Configuration saved to NVS");
    }

    nvs_close(handle);
    return err;
}

esp_err_t naptick_config_load(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No stored configuration found");
        return ESP_ERR_NOT_FOUND;
    }

    size_t size = sizeof(naptick_config_t);
    naptick_config_t temp_config;

    err = nvs_get_blob(handle, NVS_KEY_CONFIG, &temp_config, &size);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read config: %s", esp_err_to_name(err));
        return ESP_ERR_NOT_FOUND;
    }

    /* Validate magic number */
    if (temp_config.magic != NAPTICK_CONFIG_MAGIC) {
        ESP_LOGW(TAG, "Invalid config magic (0x%08lX), expected 0x%08X",
                 (unsigned long)temp_config.magic, NAPTICK_CONFIG_MAGIC);
        return ESP_ERR_NOT_FOUND;
    }

    /* Copy validated config */
    memcpy(&s_system.config, &temp_config, sizeof(naptick_config_t));

    ESP_LOGI(TAG, "Configuration loaded from NVS");
    ESP_LOGI(TAG, "  WiFi SSID: %s", s_system.config.wifi_ssid);
    ESP_LOGI(TAG, "  Token: %.20s...", s_system.config.token);

    return ESP_OK;
}

void naptick_config_reset(void)
{
    ESP_LOGW(TAG, "Factory reset requested");

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_key(handle, NVS_KEY_CONFIG);
        nvs_commit(handle);
        nvs_close(handle);
    }

    /* Clear RAM config */
    memset(&s_system.config, 0, sizeof(naptick_config_t));

    ESP_LOGW(TAG, "Configuration erased, restarting in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

const char* naptick_config_get_device_id(void)
{
    return s_system.dev_info.device_id;
}

naptick_system_t* naptick_config_get_system(void)
{
    return &s_system;
}

esp_err_t naptick_config_set_wifi(const char *ssid, const char *password)
{
    if (!s_initialized || ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_system.config.wifi_ssid, ssid, sizeof(s_system.config.wifi_ssid) - 1);
    s_system.config.wifi_ssid[sizeof(s_system.config.wifi_ssid) - 1] = '\0';

    strncpy(s_system.config.wifi_pass, password, sizeof(s_system.config.wifi_pass) - 1);
    s_system.config.wifi_pass[sizeof(s_system.config.wifi_pass) - 1] = '\0';

    ESP_LOGI(TAG, "WiFi credentials set: SSID=%s", s_system.config.wifi_ssid);

    return ESP_OK;
}

esp_err_t naptick_config_set_token(const char *token)
{
    if (!s_initialized || token == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_system.config.token, token, sizeof(s_system.config.token) - 1);
    s_system.config.token[sizeof(s_system.config.token) - 1] = '\0';

    ESP_LOGI(TAG, "Token set: %.20s...", s_system.config.token);

    return ESP_OK;
}

bool naptick_config_is_provisioned(void)
{
    return s_system.config.magic == NAPTICK_CONFIG_MAGIC &&
           strlen(s_system.config.wifi_ssid) > 0;
}
