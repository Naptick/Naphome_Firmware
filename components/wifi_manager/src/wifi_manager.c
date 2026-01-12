#include "wifi_manager.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "lwip/dns.h"
#include "sdkconfig.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT       BIT1

static const char *TAG = "wifi_manager";

static EventGroupHandle_t s_wifi_event_group;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static esp_netif_t *s_netif;
static bool s_started;
static bool s_auto_connect_configured;
static int s_retry_count;

/* State callback */
static wifi_manager_state_cb_t s_state_callback = NULL;
static void *s_state_callback_ctx = NULL;

static void set_custom_dns(void)
{
    esp_netif_dns_info_t dns_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    /* Set primary DNS to 8.8.8.8 (Google) */
    IP4_ADDR(&dns_info.ip.u_addr.ip4, 8, 8, 8, 8);
    dns_info.ip.type = IPADDR_TYPE_V4;
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);

    /* Set secondary DNS to 8.8.4.4 (Google) */
    IP4_ADDR(&dns_info.ip.u_addr.ip4, 8, 8, 4, 4);
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info);

    ESP_LOGI(TAG, "Custom DNS set to 8.8.8.8 / 8.8.4.4");
}

void wifi_manager_set_state_callback(wifi_manager_state_cb_t callback, void *user_ctx)
{
    s_state_callback = callback;
    s_state_callback_ctx = user_ctx;
}

static esp_err_t ensure_event_loop(void)
{
    esp_err_t err = esp_event_loop_create_default();
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return err;
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_auto_connect_configured) {
            ESP_LOGI(TAG, "Wi-Fi started, attempting connect");
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "Wi-Fi started with no stored credentials; awaiting provisioning");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Disconnected from AP (reason=%d)", disconnected ? disconnected->reason : 0);
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        /* Notify callback of disconnection */
        if (s_state_callback) {
            s_state_callback(false, s_state_callback_ctx);
        }

        if (CONFIG_NAPHOME_WIFI_MAX_RETRY == 0 || s_retry_count < CONFIG_NAPHOME_WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "Retrying connection (%d)", s_retry_count);
        } else {
            ESP_LOGE(TAG, "Max retry count reached (%d)", CONFIG_NAPHOME_WIFI_MAX_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Obtained IP address: " IPSTR, IP2STR(&event->ip_info.ip));

        /* Set custom DNS servers */
        set_custom_dns();

        /* Notify callback of connection */
        if (s_state_callback) {
            s_state_callback(true, s_state_callback_ctx);
        }
    }
}

static esp_err_t register_event_handlers(void)
{
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT,
                                            ESP_EVENT_ANY_ID,
                                            wifi_event_handler,
                                            NULL,
                                            &s_wifi_handler),
        TAG,
        "Failed to register Wi-Fi handler");

    esp_err_t err = esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        wifi_event_handler,
                                                        NULL,
                                                        &s_ip_handler);
    if (err != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        s_wifi_handler = NULL;
    }
    return err;
}

static void unregister_event_handlers(void)
{
    if (s_wifi_handler) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        s_wifi_handler = NULL;
    }
    if (s_ip_handler) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        s_ip_handler = NULL;
    }
}

esp_err_t wifi_manager_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
        ESP_RETURN_ON_FALSE(s_wifi_event_group, ESP_ERR_NO_MEM, TAG, "Failed to create Wi-Fi event group");
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(ensure_event_loop(), TAG, "event loop init failed");

    if (!s_netif) {
        s_netif = esp_netif_create_default_wifi_sta();
        ESP_RETURN_ON_FALSE(s_netif, ESP_ERR_NO_MEM, TAG, "Failed to create default Wi-Fi STA");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    ESP_RETURN_ON_ERROR(register_event_handlers(), TAG, "Failed to register event handlers");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");

    if (CONFIG_NAPHOME_WIFI_SSID[0] != '\0') {
        wifi_config_t wifi_config = { 0 };
        strlcpy((char *)wifi_config.sta.ssid, CONFIG_NAPHOME_WIFI_SSID, sizeof(wifi_config.sta.ssid));
        strlcpy((char *)wifi_config.sta.password, CONFIG_NAPHOME_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
        wifi_config.sta.threshold.authmode =
            (CONFIG_NAPHOME_WIFI_PASSWORD[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;

        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");
        s_auto_connect_configured = true;
    } else {
        s_auto_connect_configured = false;
        ESP_LOGW(TAG, "CONFIG_NAPHOME_WIFI_SSID not set; waiting for runtime credentials");
    }
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    s_started = true;
    ESP_LOGI(TAG, "Wi-Fi manager started");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password, TickType_t ticks_to_wait)
{
    if (!ssid || ssid[0] == '\0') {
        ESP_LOGE(TAG, "SSID is required for runtime connect");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_started) {
        esp_err_t err = wifi_manager_start();
        if (err != ESP_OK) {
            return err;
        }
    }

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (password) {
        strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    } else {
        wifi_config.sta.password[0] = '\0';
    }
    wifi_config.sta.threshold.authmode =
        (password && password[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "runtime esp_wifi_set_config failed");

    if (s_wifi_event_group) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    }
    s_retry_count = 0;
    s_auto_connect_configured = true;

    ESP_RETURN_ON_ERROR(esp_wifi_disconnect(), TAG, "esp_wifi_disconnect failed");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "esp_wifi_connect failed");

    if (ticks_to_wait > 0) {
        esp_err_t wait = wifi_manager_wait_for_connection(ticks_to_wait);
        if (wait != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi connect to \"%s\" timed out or failed (%s)", ssid, esp_err_to_name(wait));
            return wait;
        }
    }

    ESP_LOGI(TAG, "Connected to runtime network \"%s\"", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    esp_wifi_stop();
    esp_wifi_deinit();
    unregister_event_handlers();

    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    s_netif = NULL;
    s_started = false;
    s_retry_count = 0;

    ESP_LOGI(TAG, "Wi-Fi manager stopped");
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    if (!s_wifi_event_group) {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

esp_err_t wifi_manager_wait_for_connection(TickType_t ticks_to_wait)
{
    if (!s_started || !s_wifi_event_group) {
        return ESP_ERR_INVALID_STATE;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           ticks_to_wait);

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }

    if (bits & WIFI_FAIL_BIT) {
        return ESP_FAIL;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t max_records, uint16_t *num_found)
{
    if (ap_records == NULL || num_found == NULL || max_records == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_started) {
        esp_err_t err = wifi_manager_start();
        if (err != ESP_OK) {
            return err;
        }
    }

    ESP_LOGI(TAG, "Starting WiFi scan...");

    wifi_scan_config_t scan_cfg = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        return err;
    }

    if (ap_count > max_records) {
        ap_count = max_records;
    }

    err = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (err != ESP_OK) {
        return err;
    }

    *num_found = ap_count;
    ESP_LOGI(TAG, "WiFi scan complete, found %d networks", ap_count);

    return ESP_OK;
}

