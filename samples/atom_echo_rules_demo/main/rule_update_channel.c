#include "rule_update_channel.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

struct rule_update_channel {
    rule_store_t *store;
    scene_controller_t *scene;
    display_matrix_t *display;
};

static const char *TAG = "rule_channel";

static bool is_rule_document(const cJSON *root)
{
    if (!cJSON_IsObject(root)) {
        return false;
    }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *rules = cJSON_GetObjectItemCaseSensitive(root, "rules");
    return cJSON_IsString(version) && cJSON_IsArray(rules);
}

static char *extract_rules_blob(cJSON *root, const char **out_checksum)
{
    if (!root) {
        return NULL;
    }
    if (out_checksum) {
        *out_checksum = NULL;
    }
    if (is_rule_document(root)) {
        const cJSON *checksum = cJSON_GetObjectItemCaseSensitive(root, "checksum");
        if (cJSON_IsString(checksum) && out_checksum) {
            *out_checksum = checksum->valuestring;
        }
        return cJSON_PrintUnformatted(root);
    }

    cJSON *rules_obj = cJSON_GetObjectItemCaseSensitive(root, "rules");
    if (!rules_obj) {
        rules_obj = cJSON_GetObjectItemCaseSensitive(root, "document");
    }
    if (!rules_obj) {
        return NULL;
    }
    const cJSON *checksum = cJSON_GetObjectItemCaseSensitive(root, "checksum");
    if (cJSON_IsString(checksum) && out_checksum) {
        *out_checksum = checksum->valuestring;
    }
    return cJSON_PrintUnformatted(rules_obj);
}

static void log_rule_summary(const cJSON *rule)
{
    const cJSON *rule_id = cJSON_GetObjectItemCaseSensitive(rule, "id");
    const cJSON *description = cJSON_GetObjectItemCaseSensitive(rule, "description");
    ESP_LOGI(TAG, "  rule %s: %s",
             cJSON_IsString(rule_id) ? rule_id->valuestring : "<no-id>",
             cJSON_IsString(description) ? description->valuestring : "");
    const cJSON *trigger = cJSON_GetObjectItemCaseSensitive(rule, "trigger");
    if (cJSON_IsObject(trigger)) {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(trigger, "type");
        ESP_LOGI(TAG, "    trigger: %s", cJSON_IsString(type) ? type->valuestring : "<unknown>");
    }
    const cJSON *conditions = cJSON_GetObjectItemCaseSensitive(rule, "conditions");
    if (cJSON_IsObject(conditions)) {
        const cJSON *logic = cJSON_GetObjectItemCaseSensitive(conditions, "logic");
        const cJSON *items = cJSON_GetObjectItemCaseSensitive(conditions, "items");
        ESP_LOGI(TAG, "    conditions: %s (%d items)",
                 cJSON_IsString(logic) ? logic->valuestring : "ALL",
                 cJSON_IsArray(items) ? cJSON_GetArraySize(items) : 0);
    }
    const cJSON *actions = cJSON_GetObjectItemCaseSensitive(rule, "actions");
    if (cJSON_IsArray(actions)) {
        ESP_LOGI(TAG, "    actions: %d entries", cJSON_GetArraySize(actions));
    }
}

static void apply_first_rule_actions(rule_update_channel_t *channel, const cJSON *rules_array)
{
    if (!channel || !channel->scene || !cJSON_IsArray(rules_array) || cJSON_GetArraySize(rules_array) == 0) {
        return;
    }
    const cJSON *rule = cJSON_GetArrayItem(rules_array, 0);
    const cJSON *actions = cJSON_GetObjectItemCaseSensitive(rule, "actions");
    if (!cJSON_IsArray(actions)) {
        return;
    }
    ESP_LOGI(TAG, "Applying actions from first rule to seed demo state");
    const cJSON *action = NULL;
    cJSON_ArrayForEach(action, actions) {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(action, "type");
        if (!cJSON_IsString(type)) {
            continue;
        }
        if (strcmp(type->valuestring, "set_light") == 0) {
            const cJSON *mode = cJSON_GetObjectItemCaseSensitive(action, "mode");
            if (cJSON_IsString(mode) && strcmp(mode->valuestring, "scene") == 0) {
                const cJSON *scene_id = cJSON_GetObjectItemCaseSensitive(action, "scene_id");
                const cJSON *transition = cJSON_GetObjectItemCaseSensitive(action, "transition_ms");
                scene_controller_apply_light_scene(channel->scene,
                                                   cJSON_IsString(scene_id) ? scene_id->valuestring : "warm_dim",
                                                   cJSON_IsNumber(transition) ? (uint32_t)transition->valuedouble : 1000U);
            } else if (cJSON_IsString(mode) && strcmp(mode->valuestring, "color") == 0) {
                const cJSON *color = cJSON_GetObjectItemCaseSensitive(action, "color_rgb");
                const cJSON *brightness = cJSON_GetObjectItemCaseSensitive(action, "brightness");
                const cJSON *transition = cJSON_GetObjectItemCaseSensitive(action, "transition_ms");
                if (cJSON_IsArray(color) && cJSON_GetArraySize(color) == 3) {
                    uint8_t r = (uint8_t)cJSON_GetArrayItem(color, 0)->valuedouble;
                    uint8_t g = (uint8_t)cJSON_GetArrayItem(color, 1)->valuedouble;
                    uint8_t b = (uint8_t)cJSON_GetArrayItem(color, 2)->valuedouble;
                    float bright = cJSON_IsNumber(brightness) ? brightness->valuedouble : 0.2f;
                    scene_controller_set_light_color(channel->scene,
                                                     r,
                                                     g,
                                                     b,
                                                     bright,
                                                     cJSON_IsNumber(transition) ? (uint32_t)transition->valuedouble : 500U);
                }
            }
        } else if (strcmp(type->valuestring, "set_sound") == 0) {
            const cJSON *mode = cJSON_GetObjectItemCaseSensitive(action, "mode");
            const cJSON *volume = cJSON_GetObjectItemCaseSensitive(action, "volume");
            float vol = cJSON_IsNumber(volume) ? volume->valuedouble : 0.25f;
            if (cJSON_IsString(mode) && strcmp(mode->valuestring, "scene") == 0) {
                const cJSON *scene_id = cJSON_GetObjectItemCaseSensitive(action, "scene_id");
                scene_controller_play_sound_scene(channel->scene,
                                                  cJSON_IsString(scene_id) ? scene_id->valuestring : "gentle_rain",
                                                  vol);
            } else if (cJSON_IsString(mode) && strcmp(mode->valuestring, "playlist") == 0) {
                const cJSON *playlist_id = cJSON_GetObjectItemCaseSensitive(action, "playlist_id");
                scene_controller_play_sound_playlist(channel->scene,
                                                     cJSON_IsString(playlist_id) ? playlist_id->valuestring : "default_playlist",
                                                     vol);
            }
        }
    }
}

esp_err_t rule_update_channel_init(rule_update_channel_t **out_channel,
                                   const rule_update_channel_config_t *cfg)
{
    if (!out_channel || !cfg || !cfg->store) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_channel = NULL;
    rule_update_channel_t *channel = calloc(1, sizeof(rule_update_channel_t));
    if (!channel) {
        return ESP_ERR_NO_MEM;
    }
    channel->store = cfg->store;
    channel->scene = cfg->scene;
    channel->display = cfg->display;
    *out_channel = channel;
    return ESP_OK;
}

void rule_update_channel_deinit(rule_update_channel_t *channel)
{
    free(channel);
}

void rule_update_channel_handle_mqtt(rule_update_channel_t *channel,
                                     const char *payload)
{
    if (!channel || !payload) {
        return;
    }
    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        ESP_LOGW(TAG, "MQTT payload not valid JSON");
        return;
    }
    const char *checksum = NULL;
    char *rules_blob = extract_rules_blob(root, &checksum);
    if (!rules_blob) {
        ESP_LOGW(TAG, "MQTT payload missing rules block");
        cJSON_Delete(root);
        return;
    }
    bool changed = false;
    esp_err_t err = rule_store_update(channel->store,
                                      RULE_STORE_SOURCE_AWS,
                                      rules_blob,
                                      checksum,
                                      &changed);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update rules from MQTT (%s)", esp_err_to_name(err));
    } else if (changed) {
        ESP_LOGI(TAG, "Rules updated from AWS IoT");
    } else {
        ESP_LOGI(TAG, "AWS IoT supplied identical rules");
    }
    free(rules_blob);
    cJSON_Delete(root);
}

esp_err_t rule_update_channel_handle_matter(rule_update_channel_t *channel,
                                            const char *json_payload,
                                            const char *checksum)
{
    if (!channel || !json_payload) {
        return ESP_ERR_INVALID_ARG;
    }
    return rule_store_update(channel->store,
                             RULE_STORE_SOURCE_MATTER,
                             json_payload,
                             checksum,
                             NULL);
}

void rule_update_channel_apply_snapshot(rule_update_channel_t *channel,
                                        const rule_store_snapshot_t *snapshot)
{
    if (!channel || !snapshot || !snapshot->json) {
        return;
    }
    cJSON *root = cJSON_Parse(snapshot->json);
    if (!root) {
        ESP_LOGW(TAG, "Stored rules JSON invalid");
        return;
    }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *rules = cJSON_GetObjectItemCaseSensitive(root, "rules");
    ESP_LOGI(TAG, "Rule snapshot sha=%s version=%s rules=%d",
             snapshot->sha256,
             cJSON_IsString(version) ? version->valuestring : "unknown",
             cJSON_IsArray(rules) ? cJSON_GetArraySize(rules) : 0);
    if (cJSON_IsArray(rules)) {
        const cJSON *rule_iter = NULL;
        cJSON_ArrayForEach(rule_iter, rules) {
            log_rule_summary(rule_iter);
        }
        apply_first_rule_actions(channel, rules);
        // DISABLED: Rule display drawing to keep background image visible
        // if (channel->display) {
        //     int rule_index = 0;
        //     cJSON_ArrayForEach(rule_iter, rules) {
        //         const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(rule_iter, "enabled");
        //         bool is_enabled = cJSON_IsTrue(enabled);
        //         const cJSON *limits = cJSON_GetObjectItemCaseSensitive(rule_iter, "limits");
        //         bool has_limits = cJSON_IsObject(limits);

        //         uint16_t color = 0x07E0; // Green
        //         if (!is_enabled) {
        //             color = 0xF800; // Red
        //         } else if (has_limits) {
        //             color = 0xFFE0; // Yellow indicates throttled
        //         }

        //         int tile_x = rule_index % 10;
        //         int tile_y = rule_index / 10;
        //         if (tile_y < 10) {
        //             display_matrix_draw_tile(channel->display, tile_x, tile_y, color);
        //         }
        //         rule_index++;
        //     }
        //     for (int idx = cJSON_GetArraySize(rules); idx < 100; ++idx) {
        //         int tile_x = idx % 10;
        //         int tile_y = idx / 10;
        //         if (tile_y < 10) {
        //             display_matrix_draw_tile(channel->display, tile_x, tile_y, 0x4208);
        //         }
        //     }
        //     display_matrix_flush(channel->display);
        // }
    }
    cJSON_Delete(root);
}
