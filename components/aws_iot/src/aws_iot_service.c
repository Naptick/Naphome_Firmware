#include "aws_iot_service.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "aws_iot.h"

#define AWS_IOT_SERVICE_TASK_STACK (8 * 1024)
#define AWS_IOT_SERVICE_TASK_PRIO   5
#define AWS_IOT_SERVICE_HAS_IP_BIT  BIT0
#define AWS_IOT_SERVICE_STOP_BIT    BIT1

typedef struct {
    aws_iot_service_config_t cfg;
    aws_iot_client_t client;
    TaskHandle_t task;
    EventGroupHandle_t events;
    esp_event_handler_instance_t wifi_disconnect_handler;
    esp_event_handler_instance_t ip_handler;
    bool should_run;
    bool subscribed;
} aws_iot_service_ctx_t;

static const char *TAG = "aws_iot_srv";
static aws_iot_service_ctx_t s_ctx = { 0 };
static bool s_was_connected = false; // Track previous connection state for LED updates

static uint32_t resolve_yield_timeout_ms(const aws_iot_service_config_t *cfg)
{
    if (cfg && cfg->yield_timeout_ms) {
        return cfg->yield_timeout_ms;
    }
    return CONFIG_NAPHOME_AWS_IOT_YIELD_TIMEOUT_MS;
}

static QoS resolve_subscribe_qos(const aws_iot_service_config_t *cfg)
{
    if (cfg && cfg->subscribe_topic && cfg->subscribe_handler) {
        return cfg->subscribe_qos;
    }
    return (QoS)CONFIG_NAPHOME_AWS_IOT_SUBSCRIBE_QOS;
}

static const char *resolve_subscribe_topic(const aws_iot_service_config_t *cfg)
{
    if (cfg && cfg->subscribe_topic) {
        return cfg->subscribe_topic;
    }

    if (CONFIG_NAPHOME_AWS_IOT_SUBSCRIBE_TOPIC[0] != '\0') {
        return CONFIG_NAPHOME_AWS_IOT_SUBSCRIBE_TOPIC;
    }

    return NULL;
}

static pApplicationHandler_t resolve_subscribe_handler(const aws_iot_service_config_t *cfg)
{
    if (cfg && cfg->subscribe_handler) {
        return cfg->subscribe_handler;
    }
    return NULL;
}

static void aws_iot_service_event_handler(void *arg,
                                          esp_event_base_t event_base,
                                          int32_t event_id,
                                          void *event_data)
{
    (void)event_data;
    aws_iot_service_ctx_t *ctx = (aws_iot_service_ctx_t *)arg;

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi got IP address, enabling AWS IoT service");
        xEventGroupSetBits(ctx->events, AWS_IOT_SERVICE_HAS_IP_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, suspending AWS IoT service");
        xEventGroupClearBits(ctx->events, AWS_IOT_SERVICE_HAS_IP_BIT);
        ctx->subscribed = false;
    }
}

static void aws_iot_service_task(void *arg)
{
    aws_iot_service_ctx_t *ctx = (aws_iot_service_ctx_t *)arg;
    const uint32_t yield_timeout_ms = resolve_yield_timeout_ms(&ctx->cfg);

    ESP_LOGI(TAG, "AWS IoT service task started");

    while (ctx->should_run) {
        EventBits_t bits = xEventGroupWaitBits(ctx->events,
                                               AWS_IOT_SERVICE_HAS_IP_BIT,
                                               pdFALSE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(1000));

        if (!(bits & AWS_IOT_SERVICE_HAS_IP_BIT)) {
            continue;
        }

        if (!ctx->client.initialized) {
            esp_err_t err = ESP_FAIL;
            if (ctx->cfg.config_loader) {
                aws_iot_config_t custom_cfg = AWS_IOT_CONFIG_DEFAULT();
                err = ctx->cfg.config_loader(&custom_cfg, ctx->cfg.config_loader_ctx);
                if (err == ESP_OK) {
                    err = aws_iot_client_init(&ctx->client, &custom_cfg);
                }
            } else {
                err = aws_iot_client_init_from_settings(&ctx->client);
            }

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "aws_iot_client_init_from_settings failed (%s)", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }

        bool is_connected = aws_iot_client_is_connected(&ctx->client);
        
        // Track connection state (LED updates disabled when AWS IoT is disabled)
        if (is_connected != s_was_connected) {
            s_was_connected = is_connected;
        }
        
        if (!is_connected) {
            esp_err_t err = aws_iot_client_connect(&ctx->client);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "aws_iot_client_connect failed (%s)", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            ctx->subscribed = false;
            ESP_LOGI(TAG, "AWS IoT connection established");

            if (ctx->cfg.on_connected) {
                ctx->cfg.on_connected(&ctx->client, ctx->cfg.on_connected_ctx);
            }
        }

        if (!ctx->subscribed) {
            const char *topic = resolve_subscribe_topic(&ctx->cfg);
            pApplicationHandler_t handler = resolve_subscribe_handler(&ctx->cfg);
            if (topic && handler) {
                esp_err_t err = aws_iot_client_subscribe(&ctx->client,
                                                         topic,
                                                         resolve_subscribe_qos(&ctx->cfg),
                                                         handler,
                                                         ctx->cfg.subscribe_ctx);
                if (err == ESP_OK) {
                    ctx->subscribed = true;
                    ESP_LOGI(TAG, "Subscribed to %s", topic);
                } else {
                    ESP_LOGW(TAG, "Failed to subscribe to %s (%s)", topic, esp_err_to_name(err));
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    continue;
                }
            } else {
                ctx->subscribed = true; /* Nothing to subscribe to. */
            }
        }

        esp_err_t err = aws_iot_client_yield(&ctx->client, yield_timeout_ms);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "aws_iot_client_yield returned %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }

    if (ctx->client.connected) {
        ESP_LOGI(TAG, "Disconnecting AWS IoT client");
        aws_iot_client_disconnect(&ctx->client);
    }

    memset(&ctx->client, 0, sizeof(ctx->client));
    ctx->task = NULL;
    xEventGroupSetBits(ctx->events, AWS_IOT_SERVICE_STOP_BIT);
    ESP_LOGI(TAG, "AWS IoT service task stopped");
    vTaskDelete(NULL);
}

esp_err_t aws_iot_service_start(const aws_iot_service_config_t *config)
{
    if (s_ctx.should_run) {
        ESP_LOGW(TAG, "AWS IoT service already running");
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    if (config) {
        s_ctx.cfg = *config;
    }

    s_ctx.events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_ctx.events, ESP_ERR_NO_MEM, TAG, "Failed to create event group");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT,
                                            IP_EVENT_STA_GOT_IP,
                                            aws_iot_service_event_handler,
                                            &s_ctx,
                                            &s_ctx.ip_handler),
        TAG,
        "Failed to register IP handler");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT,
                                            WIFI_EVENT_STA_DISCONNECTED,
                                            aws_iot_service_event_handler,
                                            &s_ctx,
                                            &s_ctx.wifi_disconnect_handler),
        TAG,
        "Failed to register Wi-Fi handler");

    s_ctx.should_run = true;

    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta) {
        esp_netif_ip_info_t ip_info = { 0 };
        if (esp_netif_get_ip_info(sta, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            xEventGroupSetBits(s_ctx.events, AWS_IOT_SERVICE_HAS_IP_BIT);
        }
    }

    BaseType_t created = xTaskCreate(aws_iot_service_task,
                                     "aws_iot_service",
                                     AWS_IOT_SERVICE_TASK_STACK,
                                     &s_ctx,
                                     AWS_IOT_SERVICE_TASK_PRIO,
                                     &s_ctx.task);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create AWS IoT service task");
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ctx.ip_handler);
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, s_ctx.wifi_disconnect_handler);
        vEventGroupDelete(s_ctx.events);
        memset(&s_ctx, 0, sizeof(s_ctx));
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t aws_iot_service_stop(void)
{
    if (!s_ctx.should_run) {
        return ESP_OK;
    }

    s_ctx.should_run = false;
    xEventGroupClearBits(s_ctx.events, AWS_IOT_SERVICE_HAS_IP_BIT);

    if (s_ctx.task) {
        EventBits_t bits = xEventGroupWaitBits(s_ctx.events,
                                               AWS_IOT_SERVICE_STOP_BIT,
                                               pdTRUE,
                                               pdTRUE,
                                               pdMS_TO_TICKS(5000));
        if (!(bits & AWS_IOT_SERVICE_STOP_BIT)) {
            ESP_LOGW(TAG, "Timed out waiting for AWS IoT service to stop");
        }
    }

    if (s_ctx.ip_handler) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ctx.ip_handler);
    }
    if (s_ctx.wifi_disconnect_handler) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, s_ctx.wifi_disconnect_handler);
    }

    if (s_ctx.events) {
        vEventGroupDelete(s_ctx.events);
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    return ESP_OK;
}

bool aws_iot_service_is_running(void)
{
    return s_ctx.should_run;
}

aws_iot_client_t *aws_iot_service_get_client(void)
{
    return s_ctx.should_run ? &s_ctx.client : NULL;
}

