/**
 * @file somnus_mqtt.c
 */

#include "somnus_mqtt.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "aws_iot_service.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "somnus_profile.h"

#define SOMNUS_MQTT_TAG "somnus_mqtt"

#define SOMNUS_CERT_SUFFIX_CERT "-certificate.pem.crt"
#define SOMNUS_CERT_SUFFIX_KEY  "-private.pem.key"

#define SOMNUS_PATH_MAX 256
#define SOMNUS_DEVICE_ID_LEN (sizeof(SOMNUS_DEVICE_ID_PREFIX) + 12)
#define SOMNUS_TOPIC_MAX 128
#define SOMNUS_LOG_PAYLOAD_MAX 512

typedef struct {
    bool started;
    bool root_ca_owned;
    bool client_cert_owned;
    bool client_key_owned;
    char device_id[SOMNUS_DEVICE_ID_LEN];
    char subscribe_topic[SOMNUS_TOPIC_MAX];
    char log_topic[SOMNUS_TOPIC_MAX];
    char telemetry_topic[SOMNUS_TOPIC_MAX];
    char log_stage_onboarding[16];
    char log_stage_after[16];
    char *root_ca;
    size_t root_ca_len;
    char *client_cert;
    size_t client_cert_len;
    char *client_key;
    size_t client_key_len;
    somnus_mqtt_config_t cfg;
} somnus_mqtt_ctx_t;

static somnus_mqtt_ctx_t s_ctx = {
    .log_stage_onboarding = "Onboarding",
    .log_stage_after = "AfterOnboarding",
};

extern const char _binary_root_ca_pem_start[] asm("_binary_root_ca_pem_start");
extern const char _binary_root_ca_pem_end[] asm("_binary_root_ca_pem_end");
extern const char _binary_device_cert_pem_start[] asm("_binary_device_cert_pem_start");
extern const char _binary_device_cert_pem_end[] asm("_binary_device_cert_pem_end");
extern const char _binary_private_key_pem_start[] asm("_binary_private_key_pem_start");
extern const char _binary_private_key_pem_end[] asm("_binary_private_key_pem_end");

static esp_err_t somnus_mqtt_config_loader(aws_iot_config_t *config, void *arg);
static void somnus_mqtt_on_connected(aws_iot_client_t *client, void *arg);
static void somnus_mqtt_subscribe_handler(AWS_IoT_Client *pClient,
                                          char *topic_name,
                                          uint16_t topic_len,
                                          IoT_Publish_Message_Params *params,
                                          void *ctx);
static esp_err_t somnus_mqtt_discover_certificates(void);
static esp_err_t somnus_mqtt_load_file(const char *path, char **out_buf, size_t *out_len);
static void somnus_mqtt_use_embedded_certificates(void);
static void somnus_mqtt_free_certificates(void);
static bool somnus_str_case_contains(const char *haystack, const char *needle);
static const char *somnus_mqtt_infer_stage(const char *msg);
static char *somnus_mqtt_strndup(const char *src, size_t len);

static inline bool somnus_path_join(const char *dir, const char *file, char *out, size_t out_len)
{
    if (!dir || !file || !out || out_len == 0) {
        return false;
    }

    size_t dir_len = strlen(dir);
    bool needs_sep = dir_len > 0 && dir[dir_len - 1] != '/';

    int written = snprintf(out, out_len, "%s%s%s", dir, needs_sep ? "/" : "", file);
    return written > 0 && (size_t)written < out_len;
}

const char *somnus_mqtt_get_device_id(void)
{
    return s_ctx.device_id[0] ? s_ctx.device_id : NULL;
}

esp_err_t somnus_mqtt_start(const somnus_mqtt_config_t *config)
{
    if (s_ctx.started) {
        return ESP_OK;
    }

    if (config) {
        s_ctx.cfg = *config;
    } else {
        memset(&s_ctx.cfg, 0, sizeof(s_ctx.cfg));
    }

    ESP_RETURN_ON_ERROR(somnus_profile_get_device_id(s_ctx.device_id, sizeof(s_ctx.device_id)),
                        SOMNUS_MQTT_TAG,
                        "Failed to determine Somnus device ID");

    ESP_RETURN_ON_ERROR(somnus_profile_get_topics(s_ctx.subscribe_topic,
                                                  sizeof(s_ctx.subscribe_topic),
                                                  s_ctx.log_topic,
                                                  sizeof(s_ctx.log_topic)),
                        SOMNUS_MQTT_TAG,
                        "Failed to build Somnus MQTT topics");

    ESP_RETURN_ON_ERROR(somnus_profile_get_telemetry_topic(s_ctx.telemetry_topic,
                                                           sizeof(s_ctx.telemetry_topic)),
                        SOMNUS_MQTT_TAG,
                        "Failed to build Somnus telemetry topic");

    esp_err_t err = somnus_mqtt_discover_certificates();
    if (err != ESP_OK) {
        // This is expected behavior when SPIFFS certs are not available - use INFO level
        ESP_LOGI(SOMNUS_MQTT_TAG, "Certificate discovery: using embedded certificates from build-time binary assets");
        ESP_LOGD(SOMNUS_MQTT_TAG, "Certificate discovery failed (err: %s) - this is normal if SPIFFS certs not provisioned", 
                 esp_err_to_name(err));
        somnus_mqtt_use_embedded_certificates();
    } else {
        ESP_LOGI(SOMNUS_MQTT_TAG, "Successfully loaded certificates from SPIFFS filesystem");
    }

    aws_iot_service_config_t service_cfg = {
        .subscribe_topic = s_ctx.subscribe_topic,
        .subscribe_qos = (QoS)CONFIG_SOMNUS_MQTT_SUBSCRIBE_QOS,
        .subscribe_handler = somnus_mqtt_subscribe_handler,
        .subscribe_ctx = &s_ctx,
        .on_connected = somnus_mqtt_on_connected,
        .on_connected_ctx = &s_ctx,
        .config_loader = somnus_mqtt_config_loader,
        .config_loader_ctx = &s_ctx,
    };

    err = aws_iot_service_start(&service_cfg);
    if (err == ESP_OK) {
        s_ctx.started = true;
        ESP_LOGI(SOMNUS_MQTT_TAG, "Somnus MQTT service started");
    } else {
        ESP_LOGE(SOMNUS_MQTT_TAG, "Failed to start AWS IoT service (%s)", esp_err_to_name(err));
        somnus_mqtt_free_certificates();
    }

    return err;
}

esp_err_t somnus_mqtt_stop(void)
{
    if (!s_ctx.started) {
        return ESP_OK;
    }

    esp_err_t err = aws_iot_service_stop();
    somnus_mqtt_free_certificates();
    s_ctx.started = false;
    return err;
}

static const char *somnus_mqtt_infer_stage(const char *msg)
{
    if (somnus_str_case_contains(msg, "ONBOARDING")) {
        return s_ctx.log_stage_onboarding;
    }
    return s_ctx.log_stage_after;
}

esp_err_t somnus_mqtt_publish_log(const char *level, const char *message)
{
    if (!level || !message) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.started) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *stage = somnus_mqtt_infer_stage(message);
    char payload[SOMNUS_LOG_PAYLOAD_MAX];
    esp_err_t err = somnus_profile_format_log_payload(level, stage, message, payload, sizeof(payload));
    if (err != ESP_OK) {
        return err;
    }

    return somnus_mqtt_publish_raw_log(payload);
}

esp_err_t somnus_mqtt_publish_raw_log(const char *json_payload)
{
    if (!json_payload) {
        return ESP_ERR_INVALID_ARG;
    }

    aws_iot_client_t *client = aws_iot_service_get_client();
    if (!client || !aws_iot_client_is_connected(client)) {
        return ESP_ERR_INVALID_STATE;
    }

    return aws_iot_client_publish(client,
                                  s_ctx.log_topic,
                                  QOS1,
                                  json_payload,
                                  strlen(json_payload),
                                  false);
}

esp_err_t somnus_mqtt_publish_telemetry(const char *json_payload)
{
    if (!json_payload) {
        return ESP_ERR_INVALID_ARG;
    }

    aws_iot_client_t *client = aws_iot_service_get_client();
    if (!client || !aws_iot_client_is_connected(client)) {
        return ESP_ERR_INVALID_STATE;
    }

    return aws_iot_client_publish(client,
                                  s_ctx.telemetry_topic,
                                  QOS1,
                                  json_payload,
                                  strlen(json_payload),
                                  false);
}

static bool somnus_str_case_contains(const char *haystack, const char *needle)
{
    if (!haystack || !needle) {
        return false;
    }

    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return true;
    }

    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < needle_len) {
            char hc = (char)toupper((int)p[i]);
            char nc = (char)toupper((int)needle[i]);
            if (hc != nc || p[i] == '\0') {
                break;
            }
            ++i;
        }
        if (i == needle_len) {
            return true;
        }
    }
    return false;
}

static esp_err_t somnus_mqtt_config_loader(aws_iot_config_t *config, void *arg)
{
    (void)arg;
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    *config = (aws_iot_config_t)AWS_IOT_CONFIG_DEFAULT();
    config->endpoint = SOMNUS_AWS_ENDPOINT;
    config->port = SOMNUS_AWS_PORT;
    config->client_id = s_ctx.device_id;
    config->root_ca = s_ctx.root_ca;
    config->root_ca_len = s_ctx.root_ca_len;
    config->client_cert = s_ctx.client_cert;
    config->client_cert_len = s_ctx.client_cert_len;
    config->client_key = s_ctx.client_key;
    config->client_key_len = s_ctx.client_key_len;
    config->keepalive_sec = 120;
    config->clean_session = true;
    config->auto_reconnect = true;

    ESP_LOGI(SOMNUS_MQTT_TAG, "AWS IoT config: endpoint=%s port=%d client_id=%s",
             config->endpoint ? config->endpoint : "(null)",
             config->port,
             config->client_id ? config->client_id : "(null)");
    ESP_LOGI(SOMNUS_MQTT_TAG, "Certificate lengths: root_ca=%zu client_cert=%zu client_key=%zu",
             config->root_ca_len, config->client_cert_len, config->client_key_len);

    return ESP_OK;
}

// Forward declaration - defined in main app if available
extern void aws_led_set_connected(bool connected);

static void somnus_mqtt_on_connected(aws_iot_client_t *client, void *arg)
{
    (void)arg;
    if (!client) {
        return;
    }

    const char *device_id = somnus_mqtt_get_device_id();
    ESP_LOGI(SOMNUS_MQTT_TAG, "Connected to AWS IoT as %s", device_id ? device_id : "(unknown)");
    
    // Update AWS LED status
    extern void aws_led_set_connected(bool connected);
    aws_led_set_connected(true);

    static const char *kTestLevel = "INFO";
    char message[128];
    snprintf(message,
             sizeof(message),
             "Device %s connected and ready for commands",
             device_id ? device_id : "UNKNOWN");

    char payload[SOMNUS_LOG_PAYLOAD_MAX];
    if (somnus_profile_format_log_payload(kTestLevel,
                                          s_ctx.log_stage_after,
                                          message,
                                          payload,
                                          sizeof(payload)) == ESP_OK) {
        aws_iot_client_publish(client,
                               s_ctx.log_topic,
                               QOS1,
                               payload,
                               strlen(payload),
                               false);
        ESP_LOGI(SOMNUS_MQTT_TAG, "Published Somnus readiness log");
    }
}

static void somnus_mqtt_dispatch_payload(const char *payload)
{
    if (s_ctx.cfg.action_cb) {
        s_ctx.cfg.action_cb(payload, s_ctx.cfg.action_ctx);
    } else {
        ESP_LOGI(SOMNUS_MQTT_TAG, "Somnus MQTT payload: %s", payload);
    }
}

static void somnus_mqtt_handle_dictionary(cJSON *obj)
{
    if (!obj) {
        return;
    }

    cJSON *store = cJSON_GetObjectItem(obj, "store");
    if (cJSON_IsBool(store) && cJSON_IsTrue(store)) {
        ESP_LOGI(SOMNUS_MQTT_TAG, "Received schedule update payload");
        char *serialized = cJSON_PrintUnformatted(obj);
        if (serialized) {
            somnus_mqtt_dispatch_payload(serialized);
            free(serialized);
        }
        return;
    }

    cJSON *action = cJSON_GetObjectItem(obj, "Action");
    if (cJSON_IsString(action)) {
        char *serialized = cJSON_PrintUnformatted(obj);
        if (serialized) {
            somnus_mqtt_dispatch_payload(serialized);
            free(serialized);
        }
        return;
    }

    ESP_LOGW(SOMNUS_MQTT_TAG, "Unrecognised Somnus dictionary payload");
}

static bool somnus_mqtt_is_routine_list(const cJSON *array)
{
    if (!cJSON_IsArray(array)) {
        return false;
    }

    const cJSON *first = cJSON_GetArrayItem(array, 0);
    if (!cJSON_IsObject(first)) {
        return false;
    }

    const cJSON *element = first->child;
    while (element) {
        if (element->string) {
            const char *key = element->string;
            if (somnus_str_case_contains(key, "presleeproutine") ||
                somnus_str_case_contains(key, "sleeproutine") ||
                somnus_str_case_contains(key, "wakeuproutine")) {
                return true;
            }
        }
        element = element->next;
    }
    return false;
}

static void somnus_mqtt_handle_array(cJSON *array)
{
    if (!array) {
        return;
    }

    char *serialized = cJSON_PrintUnformatted(array);
    if (!serialized) {
        return;
    }

    if (somnus_mqtt_is_routine_list(array)) {
        ESP_LOGI(SOMNUS_MQTT_TAG, "Received Somnus routine list");
    } else {
        ESP_LOGI(SOMNUS_MQTT_TAG, "Received Somnus action list");
    }

    somnus_mqtt_dispatch_payload(serialized);
    free(serialized);
}

static void somnus_mqtt_subscribe_handler(AWS_IoT_Client *pClient,
                                          char *topic_name,
                                          uint16_t topic_len,
                                          IoT_Publish_Message_Params *params,
                                          void *ctx)
{
    (void)pClient;
    (void)ctx;

    char topic[129] = { 0 };
    if (topic_name && topic_len > 0) {
        size_t copy_len = topic_len < sizeof(topic) - 1 ? topic_len : sizeof(topic) - 1;
        memcpy(topic, topic_name, copy_len);
    }
    ESP_LOGI(SOMNUS_MQTT_TAG, "Somnus MQTT message on %s", topic);

    if (!params || !params->payload || params->payloadLen == 0) {
        ESP_LOGW(SOMNUS_MQTT_TAG, "Empty Somnus MQTT payload");
        return;
    }

    char *payload = somnus_mqtt_strndup((const char *)params->payload, params->payloadLen);
    if (!payload) {
        ESP_LOGE(SOMNUS_MQTT_TAG, "Failed to allocate payload buffer");
        return;
    }

    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        ESP_LOGW(SOMNUS_MQTT_TAG, "Invalid JSON payload");
        somnus_mqtt_dispatch_payload(payload);
        free(payload);
        return;
    }

    if (cJSON_IsObject(root)) {
        somnus_mqtt_handle_dictionary(root);
    } else if (cJSON_IsArray(root)) {
        somnus_mqtt_handle_array(root);
    } else {
        ESP_LOGW(SOMNUS_MQTT_TAG, "Unexpected MQTT payload type");
    }

    cJSON_Delete(root);
    free(payload);
}

static esp_err_t somnus_mqtt_discover_certificates(void)
{
#if CONFIG_SOMNUS_MQTT_CERT_DISCOVERY
    // Check if directory exists and is accessible
    // Use LOGD (debug) instead of LOGW to avoid false error counts when falling back to embedded certs
    struct stat dir_stat;
    if (stat(CONFIG_SOMNUS_MQTT_CERT_DIR, &dir_stat) != 0) {
        // ENOENT (2) is expected if SPIFFS not mounted or certs not provisioned - use debug level
        if (errno == ENOENT) {
            ESP_LOGD(SOMNUS_MQTT_TAG, "Certificate directory not found: %s (SPIFFS may not be mounted or certs not provisioned)", 
                     CONFIG_SOMNUS_MQTT_CERT_DIR);
        } else {
            ESP_LOGW(SOMNUS_MQTT_TAG, "Certificate directory access error: %s (errno: %d, %s)", 
                     CONFIG_SOMNUS_MQTT_CERT_DIR, errno, strerror(errno));
        }
        ESP_LOGI(SOMNUS_MQTT_TAG, "Using embedded PEM certificates (expected behavior if SPIFFS certs not available)");
        return ESP_FAIL;
    }
    
    if (!S_ISDIR(dir_stat.st_mode)) {
        ESP_LOGE(SOMNUS_MQTT_TAG, "Certificate path is not a directory: %s", CONFIG_SOMNUS_MQTT_CERT_DIR);
        return ESP_FAIL;
    }
    
    DIR *dir = opendir(CONFIG_SOMNUS_MQTT_CERT_DIR);
    if (!dir) {
        // ENOENT is expected - use debug level to avoid false error counts
        if (errno == ENOENT) {
            ESP_LOGD(SOMNUS_MQTT_TAG, "Certificate directory not accessible: %s (SPIFFS may not be mounted)", 
                     CONFIG_SOMNUS_MQTT_CERT_DIR);
        } else {
            ESP_LOGE(SOMNUS_MQTT_TAG, "Failed to open cert directory: %s (errno: %d, %s)", 
                     CONFIG_SOMNUS_MQTT_CERT_DIR, errno, strerror(errno));
        }
        ESP_LOGI(SOMNUS_MQTT_TAG, "Using embedded PEM certificates");
        return ESP_FAIL;
    }
    
    ESP_LOGI(SOMNUS_MQTT_TAG, "Scanning certificate directory: %s", CONFIG_SOMNUS_MQTT_CERT_DIR);

    char cert_path[SOMNUS_PATH_MAX] = { 0 };
    char key_path[SOMNUS_PATH_MAX] = { 0 };
    char ca_path[SOMNUS_PATH_MAX] = { 0 };

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (!name) {
            continue;
        }

        size_t len = strlen(name);
        if (len > strlen(SOMNUS_CERT_SUFFIX_CERT) &&
            strcmp(name + len - strlen(SOMNUS_CERT_SUFFIX_CERT), SOMNUS_CERT_SUFFIX_CERT) == 0) {
            if (!somnus_path_join(CONFIG_SOMNUS_MQTT_CERT_DIR, name, cert_path, sizeof(cert_path))) {
                continue;
            }
            char key_name[SOMNUS_PATH_MAX];
            size_t prefix_len = len - strlen(SOMNUS_CERT_SUFFIX_CERT);
            snprintf(key_name, sizeof(key_name), "%.*s%s", (int)prefix_len, name, SOMNUS_CERT_SUFFIX_KEY);
            if (!somnus_path_join(CONFIG_SOMNUS_MQTT_CERT_DIR, key_name, key_path, sizeof(key_path))) {
                key_path[0] = '\0';
            } else if (access(key_path, R_OK) != 0) {
                key_path[0] = '\0';
            }
        } else if (somnus_str_case_contains(name, "CA1.pem")) {
            somnus_path_join(CONFIG_SOMNUS_MQTT_CERT_DIR, name, ca_path, sizeof(ca_path));
        }
    }
    closedir(dir);

    if (cert_path[0] == '\0' || key_path[0] == '\0') {
        ESP_LOGD(SOMNUS_MQTT_TAG, "Incomplete certificate set in %s (expected if certs not provisioned)", 
                 CONFIG_SOMNUS_MQTT_CERT_DIR);
        ESP_LOGD(SOMNUS_MQTT_TAG, "Expected files: *-certificate.pem.crt and *-private.pem.key");
        ESP_LOGD(SOMNUS_MQTT_TAG, "Found cert: %s, key: %s", 
                 cert_path[0] ? cert_path : "(none)", key_path[0] ? key_path : "(none)");
        ESP_LOGI(SOMNUS_MQTT_TAG, "Using embedded PEM certificates");
        return ESP_FAIL;
    }

    if (ca_path[0] == '\0') {
        somnus_path_join(CONFIG_SOMNUS_MQTT_CERT_DIR, "AmazonRootCA1.pem", ca_path, sizeof(ca_path));
        if (access(ca_path, R_OK) != 0) {
            ESP_LOGD(SOMNUS_MQTT_TAG, "CA file not found: %s (using embedded CA)", ca_path);
            ESP_LOGI(SOMNUS_MQTT_TAG, "Using embedded PEM certificates");
            return ESP_FAIL;
        }
    }

    ESP_RETURN_ON_ERROR(somnus_mqtt_load_file(ca_path, &s_ctx.root_ca, &s_ctx.root_ca_len),
                        SOMNUS_MQTT_TAG,
                        "Failed to load CA file");
    s_ctx.root_ca_owned = true;

    ESP_RETURN_ON_ERROR(somnus_mqtt_load_file(cert_path, &s_ctx.client_cert, &s_ctx.client_cert_len),
                        SOMNUS_MQTT_TAG,
                        "Failed to load device certificate");
    s_ctx.client_cert_owned = true;

    ESP_RETURN_ON_ERROR(somnus_mqtt_load_file(key_path, &s_ctx.client_key, &s_ctx.client_key_len),
                        SOMNUS_MQTT_TAG,
                        "Failed to load device key");
    s_ctx.client_key_owned = true;

    ESP_LOGI(SOMNUS_MQTT_TAG, "Using certificate set: %.*s",
             12,
             cert_path);

    return ESP_OK;
#else
    return ESP_FAIL;
#endif
}

static esp_err_t somnus_mqtt_load_file(const char *path, char **out_buf, size_t *out_len)
{
    if (!path || !out_buf || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check file exists and is readable before opening
    if (access(path, R_OK) != 0) {
        ESP_LOGE(SOMNUS_MQTT_TAG, "Certificate file not accessible: %s (errno: %d, %s)", 
                 path, errno, strerror(errno));
        return ESP_FAIL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(SOMNUS_MQTT_TAG, "Failed to open certificate file: %s (errno: %d, %s)", 
                 path, errno, strerror(errno));
        return ESP_FAIL;
    }
    
    ESP_LOGI(SOMNUS_MQTT_TAG, "Loading certificate file: %s", path);

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }

    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return ESP_FAIL;
    }
    rewind(f);

    char *buffer = (char *)malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(buffer, 1, size, f);
    fclose(f);

    if (read != (size_t)size) {
        free(buffer);
        return ESP_FAIL;
    }

    buffer[size] = '\0';
    *out_buf = buffer;
    *out_len = size + 1;
    return ESP_OK;
}

static char *somnus_mqtt_strndup(const char *src, size_t len)
{
    if (!src) {
        return NULL;
    }

    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        return NULL;
    }
    memcpy(buf, src, len);
    buf[len] = '\0';
    return buf;
}

static void somnus_mqtt_use_embedded_certificates(void)
{
    s_ctx.root_ca = (char *)_binary_root_ca_pem_start;
    s_ctx.root_ca_len = (size_t)(_binary_root_ca_pem_end - _binary_root_ca_pem_start);
    s_ctx.client_cert = (char *)_binary_device_cert_pem_start;
    s_ctx.client_cert_len = (size_t)(_binary_device_cert_pem_end - _binary_device_cert_pem_start);
    s_ctx.client_key = (char *)_binary_private_key_pem_start;
    s_ctx.client_key_len = (size_t)(_binary_private_key_pem_end - _binary_private_key_pem_start);
    s_ctx.root_ca_owned = false;
    s_ctx.client_cert_owned = false;
    s_ctx.client_key_owned = false;
}

static void somnus_mqtt_free_certificates(void)
{
    if (s_ctx.root_ca_owned && s_ctx.root_ca) {
        free(s_ctx.root_ca);
    }
    if (s_ctx.client_cert_owned && s_ctx.client_cert) {
        free(s_ctx.client_cert);
    }
    if (s_ctx.client_key_owned && s_ctx.client_key) {
        free(s_ctx.client_key);
    }

    s_ctx.root_ca = NULL;
    s_ctx.client_cert = NULL;
    s_ctx.client_key = NULL;
    s_ctx.root_ca_len = 0;
    s_ctx.client_cert_len = 0;
    s_ctx.client_key_len = 0;
    s_ctx.root_ca_owned = false;
    s_ctx.client_cert_owned = false;
    s_ctx.client_key_owned = false;
}

