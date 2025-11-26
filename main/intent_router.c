#include "intent_router.h"

#include <ctype.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "intent_router";

static const char *find_keyword(const char *haystack, const char *needle)
{
    if (!haystack || !needle) {
        return NULL;
    }
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return NULL;
    }
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (p[i] && i < needle_len) {
            char a = (char)tolower((unsigned char)p[i]);
            char b = (char)tolower((unsigned char)needle[i]);
            if (a != b) {
                break;
            }
            i++;
        }
        if (i == needle_len) {
            return p;
        }
    }
    return NULL;
}

static void extract_after_keyword(const char *utterance, const char *keyword, char *out, size_t out_len)
{
    if (!utterance || !keyword || !out || out_len == 0) {
        return;
    }
    const char *pos = find_keyword(utterance, keyword);
    if (!pos) {
        out[0] = '\0';
        return;
    }
    pos += strlen(keyword);
    while (*pos && isspace((unsigned char)*pos)) {
        ++pos;
    }
    strlcpy(out, pos, out_len);
}

esp_err_t intent_router_init(intent_router_t *router, const intent_router_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(router, ESP_ERR_INVALID_ARG, TAG, "router required");
    router->default_volume_step = cfg ? cfg->default_volume_step : 10;
    return ESP_OK;
}

intent_router_decision_t intent_router_route(intent_router_t *router, const char *utterance)
{
    intent_router_decision_t decision = {
        .action = INTENT_ROUTER_ACTION_NONE,
        .argument = {0},
        .volume_delta = 0,
    };
    if (!utterance || !router) {
        return decision;
    }

    if (find_keyword(utterance, "pause") || find_keyword(utterance, "stop")) {
        decision.action = INTENT_ROUTER_ACTION_SPOTIFY_PAUSE;
        return decision;
    }

    if (find_keyword(utterance, "resume") || find_keyword(utterance, "continue")) {
        decision.action = INTENT_ROUTER_ACTION_SPOTIFY_RESUME;
        return decision;
    }

    if (find_keyword(utterance, "volume up") || find_keyword(utterance, "louder")) {
        decision.action = INTENT_ROUTER_ACTION_SPOTIFY_VOLUME_DELTA;
        decision.volume_delta = router->default_volume_step;
        return decision;
    }

    if (find_keyword(utterance, "volume down") || find_keyword(utterance, "quieter") || find_keyword(utterance, "lower")) {
        decision.action = INTENT_ROUTER_ACTION_SPOTIFY_VOLUME_DELTA;
        decision.volume_delta = -router->default_volume_step;
        return decision;
    }

    if (find_keyword(utterance, "play")) {
        decision.action = INTENT_ROUTER_ACTION_SPOTIFY_PLAY;
        extract_after_keyword(utterance, "play", decision.argument, sizeof(decision.argument));
        return decision;
    }

    // Lights control - simple local keyword detection
    if (find_keyword(utterance, "lights off") || find_keyword(utterance, "turn off the lights") || 
        find_keyword(utterance, "lights out") || find_keyword(utterance, "turn lights off")) {
        decision.action = INTENT_ROUTER_ACTION_LIGHTS_OFF;
        return decision;
    }

    if (find_keyword(utterance, "lights on") || find_keyword(utterance, "turn on the lights") || 
        find_keyword(utterance, "turn lights on") || find_keyword(utterance, "lights up")) {
        decision.action = INTENT_ROUTER_ACTION_LIGHTS_ON;
        return decision;
    }

    ESP_LOGI(TAG, "No intent matched for \"%s\"", utterance);
    return decision;
}
