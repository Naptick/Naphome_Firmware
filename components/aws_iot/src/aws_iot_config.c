#include "aws_iot.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "aws_iot_cfg";

extern const char _binary_root_ca_pem_start[] asm("_binary_root_ca_pem_start");
extern const char _binary_root_ca_pem_end[] asm("_binary_root_ca_pem_end");
extern const char _binary_device_cert_pem_start[] asm("_binary_device_cert_pem_start");
extern const char _binary_device_cert_pem_end[] asm("_binary_device_cert_pem_end");
extern const char _binary_private_key_pem_start[] asm("_binary_private_key_pem_start");
extern const char _binary_private_key_pem_end[] asm("_binary_private_key_pem_end");

static bool buffer_contains_placeholder(const char *buffer, size_t len)
{
    if (!buffer || !len) {
        return true;
    }

    const char *marker = "REPLACE_ME";
    if (buffer[len - 1] == '\0') {
        return strstr(buffer, marker) != NULL;
    }

    /* Last byte is not null-terminated; create a bounded search. */
    size_t marker_len = strlen(marker);
    if (len < marker_len) {
        return false;
    }

    for (size_t i = 0; i <= len - marker_len; ++i) {
        if (memcmp(buffer + i, marker, marker_len) == 0) {
            return true;
        }
    }

    return false;
}

static esp_err_t validate_string_config(const char *value, const char *name)
{
    if (!value || value[0] == '\0') {
        ESP_LOGE(TAG, "%s is not configured", name);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t validate_credentials_placeholders(void)
{
    const size_t root_ca_len = (size_t)(_binary_root_ca_pem_end - _binary_root_ca_pem_start);
    const size_t device_cert_len = (size_t)(_binary_device_cert_pem_end - _binary_device_cert_pem_start);
    const size_t private_key_len = (size_t)(_binary_private_key_pem_end - _binary_private_key_pem_start);

    bool has_placeholder = false;

    if (buffer_contains_placeholder(_binary_root_ca_pem_start, root_ca_len)) {
        ESP_LOGW(TAG, "Root CA placeholder detected");
        has_placeholder = true;
    }

    if (buffer_contains_placeholder(_binary_device_cert_pem_start, device_cert_len)) {
        ESP_LOGW(TAG, "Device certificate placeholder detected");
        has_placeholder = true;
    }

    if (buffer_contains_placeholder(_binary_private_key_pem_start, private_key_len)) {
        ESP_LOGW(TAG, "Private key placeholder detected");
        has_placeholder = true;
    }

#if CONFIG_NAPHOME_AWS_IOT_FAIL_ON_PLACEHOLDER_CERTS
    ESP_RETURN_ON_FALSE(!has_placeholder, ESP_ERR_INVALID_STATE, TAG,
                        "Placeholder credentials detected; update cert files in components/aws_iot/certs");
#endif

    return ESP_OK;
}

esp_err_t aws_iot_config_load_from_kconfig(aws_iot_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "config pointer is NULL");

    esp_err_t err = validate_string_config(CONFIG_NAPHOME_AWS_IOT_ENDPOINT, "CONFIG_NAPHOME_AWS_IOT_ENDPOINT");
    if (err != ESP_OK) {
        return err;
    }

    err = validate_string_config(CONFIG_NAPHOME_AWS_IOT_CLIENT_ID, "CONFIG_NAPHOME_AWS_IOT_CLIENT_ID");
    if (err != ESP_OK) {
        return err;
    }

    err = validate_credentials_placeholders();
    if (err != ESP_OK) {
        return err;
    }

    *config = (aws_iot_config_t)AWS_IOT_CONFIG_DEFAULT();
    config->endpoint = CONFIG_NAPHOME_AWS_IOT_ENDPOINT;
    config->port = (uint16_t)CONFIG_NAPHOME_AWS_IOT_PORT;
    config->client_id = CONFIG_NAPHOME_AWS_IOT_CLIENT_ID;
    config->keepalive_sec = (uint32_t)CONFIG_NAPHOME_AWS_IOT_KEEPALIVE_SEC;
    config->clean_session = CONFIG_NAPHOME_AWS_IOT_CLEAN_SESSION;
    config->auto_reconnect = CONFIG_NAPHOME_AWS_IOT_AUTO_RECONNECT;

    config->root_ca = _binary_root_ca_pem_start;
    config->root_ca_len = (size_t)(_binary_root_ca_pem_end - _binary_root_ca_pem_start);
    config->client_cert = _binary_device_cert_pem_start;
    config->client_cert_len = (size_t)(_binary_device_cert_pem_end - _binary_device_cert_pem_start);
    config->client_key = _binary_private_key_pem_start;
    config->client_key_len = (size_t)(_binary_private_key_pem_end - _binary_private_key_pem_start);

    ESP_LOGI(TAG, "Loaded AWS IoT config: endpoint=%s client_id=%s port=%" PRIu16,
             config->endpoint,
             config->client_id,
             config->port);

    return ESP_OK;
}

esp_err_t aws_iot_client_init_from_settings(aws_iot_client_t *client)
{
    aws_iot_config_t config;
    ESP_RETURN_ON_ERROR(aws_iot_config_load_from_kconfig(&config), TAG, "Failed to load AWS IoT config");
    return aws_iot_client_init(client, &config);
}

