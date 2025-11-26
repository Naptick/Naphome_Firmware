#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "intent_router.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t wake_events;
    uint32_t simulated_wake_events;
    uint32_t button_events;
    uint32_t stt_success;
    uint32_t stt_failure;
    uint32_t tts_success;
    uint32_t tts_failure;
    uint32_t spotify_success;
    uint32_t spotify_failure;
    uint32_t interactions;
    uint32_t interaction_errors;
    int last_button_id;
} aws_iot_bridge_metrics_t;

typedef struct aws_iot_bridge {
    uint32_t telemetry_period_ms;
    TimerHandle_t telemetry_timer;
    SemaphoreHandle_t metrics_mutex;
    aws_iot_bridge_metrics_t metrics;
} aws_iot_bridge_t;

typedef struct {
    uint32_t telemetry_period_ms;
} aws_iot_bridge_config_t;

esp_err_t aws_iot_bridge_init(aws_iot_bridge_t *bridge, const aws_iot_bridge_config_t *cfg);
esp_err_t aws_iot_bridge_publish_interaction(aws_iot_bridge_t *bridge,
                                             const char *transcript,
                                             const intent_router_decision_t *decision,
                                             esp_err_t action_status);
void aws_iot_bridge_record_wake(aws_iot_bridge_t *bridge, bool simulated);
void aws_iot_bridge_record_button(aws_iot_bridge_t *bridge, int button_id);
void aws_iot_bridge_record_stt_result(aws_iot_bridge_t *bridge, esp_err_t status);
void aws_iot_bridge_record_tts_result(aws_iot_bridge_t *bridge, esp_err_t status);
void aws_iot_bridge_record_spotify_result(aws_iot_bridge_t *bridge, esp_err_t status);
void aws_iot_bridge_record_interaction(aws_iot_bridge_t *bridge, esp_err_t status);

#ifdef __cplusplus
}
#endif
