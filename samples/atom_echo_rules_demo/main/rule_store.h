#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char sha256[65];  // Hex encoded digest (null terminated)
    char *json;       // Owning pointer to rule set JSON
} rule_store_snapshot_t;

typedef struct rule_store rule_store_t;

typedef enum {
    RULE_STORE_SOURCE_BOOT = 0,
    RULE_STORE_SOURCE_AWS,
    RULE_STORE_SOURCE_MATTER,
    RULE_STORE_SOURCE_LOCAL_TEST,
} rule_store_source_t;

typedef struct {
    bool auto_flush;
    const char *spiffs_path;
} rule_store_config_t;

esp_err_t rule_store_init(rule_store_t **out_store, const rule_store_config_t *cfg);
void rule_store_deinit(rule_store_t *store);

esp_err_t rule_store_get_snapshot(rule_store_t *store, rule_store_snapshot_t *out_snapshot);
void rule_store_release_snapshot(rule_store_snapshot_t *snapshot);

esp_err_t rule_store_update(rule_store_t *store,
                            rule_store_source_t source,
                            const char *json_payload,
                            const char *expected_sha256,
                            bool *out_changed);

typedef void (*rule_store_observer_cb_t)(const rule_store_snapshot_t *snapshot,
                                         rule_store_source_t source,
                                         void *ctx);

esp_err_t rule_store_set_observer(rule_store_t *store,
                                  rule_store_observer_cb_t callback,
                                  void *ctx);

const char *rule_store_source_name(rule_store_source_t source);

#ifdef __cplusplus
}
#endif
