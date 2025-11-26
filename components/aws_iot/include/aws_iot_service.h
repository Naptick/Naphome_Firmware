#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "aws_iot.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*aws_iot_service_connected_cb_t)(aws_iot_client_t *client, void *ctx);
typedef esp_err_t (*aws_iot_service_config_loader_cb_t)(aws_iot_config_t *config, void *ctx);

typedef struct {
    const char *subscribe_topic;
    QoS subscribe_qos;
    pApplicationHandler_t subscribe_handler;
    void *subscribe_ctx;
    aws_iot_service_connected_cb_t on_connected;
    void *on_connected_ctx;
    uint32_t yield_timeout_ms;
    aws_iot_service_config_loader_cb_t config_loader;
    void *config_loader_ctx;
} aws_iot_service_config_t;

esp_err_t aws_iot_service_start(const aws_iot_service_config_t *config);
esp_err_t aws_iot_service_stop(void);
bool aws_iot_service_is_running(void);
aws_iot_client_t *aws_iot_service_get_client(void);

#ifdef __cplusplus
}
#endif

