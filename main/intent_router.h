#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INTENT_ROUTER_ACTION_NONE = 0,
    INTENT_ROUTER_ACTION_SPOTIFY_PLAY,
    INTENT_ROUTER_ACTION_SPOTIFY_PAUSE,
    INTENT_ROUTER_ACTION_SPOTIFY_RESUME,
    INTENT_ROUTER_ACTION_SPOTIFY_VOLUME_DELTA,
    INTENT_ROUTER_ACTION_LIGHTS_ON,
    INTENT_ROUTER_ACTION_LIGHTS_OFF,
} intent_router_action_t;

typedef struct {
    intent_router_action_t action;
    char argument[64];
    int volume_delta;
} intent_router_decision_t;

typedef struct intent_router {
    int default_volume_step;
} intent_router_t;

typedef struct {
    int default_volume_step;
} intent_router_config_t;

esp_err_t intent_router_init(intent_router_t *router, const intent_router_config_t *cfg);
intent_router_decision_t intent_router_route(intent_router_t *router, const char *utterance);

#ifdef __cplusplus
}
#endif
