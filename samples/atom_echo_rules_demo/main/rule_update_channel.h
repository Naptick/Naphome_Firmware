#pragma once

#include "esp_err.h"
#include "display_matrix.h"
#include "rule_store.h"
#include "scene_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rule_update_channel rule_update_channel_t;

typedef struct {
    rule_store_t *store;
    scene_controller_t *scene;
    display_matrix_t *display;
} rule_update_channel_config_t;

esp_err_t rule_update_channel_init(rule_update_channel_t **out_channel,
                                   const rule_update_channel_config_t *cfg);
void rule_update_channel_deinit(rule_update_channel_t *channel);

void rule_update_channel_handle_mqtt(rule_update_channel_t *channel,
                                     const char *payload);

esp_err_t rule_update_channel_handle_matter(rule_update_channel_t *channel,
                                            const char *json_payload,
                                            const char *checksum);

void rule_update_channel_apply_snapshot(rule_update_channel_t *channel,
                                        const rule_store_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
