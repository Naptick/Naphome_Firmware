#include "rule_store.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "mbedtls/md.h"

struct rule_store {
    rule_store_config_t cfg;
    char *json;
    char sha256[65];
    rule_store_observer_cb_t observer;
    void *observer_ctx;
};

static const char *TAG = "rule_store";
static const char *DEFAULT_SPIFFS_PATH = "/spiffs/rules.json";

static void compute_sha256_hex(const char *json, char out_hex[65])
{
    unsigned char digest[32];
    size_t json_len = json ? strlen(json) : 0;
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, info, 0) != 0) {
        memset(out_hex, '0', 64);
        out_hex[64] = '\0';
        mbedtls_md_free(&ctx);
        return;
    }
    mbedtls_md_starts(&ctx);
    if (json_len > 0) {
        mbedtls_md_update(&ctx, (const unsigned char *)json, json_len);
    }
    mbedtls_md_finish(&ctx, digest);
    mbedtls_md_free(&ctx);
    for (size_t i = 0; i < sizeof(digest); ++i) {
        sprintf(&out_hex[i * 2], "%02x", digest[i]);
    }
    out_hex[64] = '\0';
}

static esp_err_t load_from_disk(rule_store_t *store)
{
    const char *path = store->cfg.spiffs_path ? store->cfg.spiffs_path : DEFAULT_SPIFFS_PATH;
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Rules file not found at %s", path);
        return ESP_ERR_NOT_FOUND;
    }
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
    char *buf = calloc((size_t)size + 1, 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size) {
        free(buf);
        return ESP_FAIL;
    }
    free(store->json);
    store->json = buf;
    compute_sha256_hex(store->json, store->sha256);
    ESP_LOGI(TAG, "Loaded rules (%zu bytes) sha=%s", read, store->sha256);
    return ESP_OK;
}

static esp_err_t flush_to_disk(rule_store_t *store)
{
    if (!store->cfg.auto_flush) {
        return ESP_OK;
    }
    const char *path = store->cfg.spiffs_path ? store->cfg.spiffs_path : DEFAULT_SPIFFS_PATH;
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", path);
        return ESP_FAIL;
    }
    size_t len = store->json ? strlen(store->json) : 0;
    size_t written = len > 0 ? fwrite(store->json, 1, len, f) : 0;
    fclose(f);
    if (len != written) {
        ESP_LOGE(TAG, "Short write: expected %zu, wrote %zu", len, written);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Persisted rules to %s (%zu bytes)", path, len);
    return ESP_OK;
}

esp_err_t rule_store_init(rule_store_t **out_store, const rule_store_config_t *cfg)
{
    if (!out_store) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_store = NULL;

    rule_store_t *store = calloc(1, sizeof(rule_store_t));
    if (!store) {
        return ESP_ERR_NO_MEM;
    }
    store->cfg.auto_flush = true;
    store->cfg.spiffs_path = DEFAULT_SPIFFS_PATH;
    if (cfg) {
        store->cfg = *cfg;
        if (!store->cfg.spiffs_path) {
            store->cfg.spiffs_path = DEFAULT_SPIFFS_PATH;
        }
    }

    esp_err_t load_err = load_from_disk(store);
    if (load_err != ESP_OK) {
        // Start with empty JSON
        store->json = calloc(1, 1);
        if (!store->json) {
            free(store);
            return ESP_ERR_NO_MEM;
        }
        compute_sha256_hex(store->json, store->sha256);
        if (store->cfg.auto_flush && flush_to_disk(store) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to create empty rules file at boot");
        }
    }

    *out_store = store;
    if (store->observer) {
        rule_store_snapshot_t snapshot = {
            .json = store->json,
        };
        memcpy(snapshot.sha256, store->sha256, sizeof(store->sha256));
        store->observer(&snapshot, RULE_STORE_SOURCE_BOOT, store->observer_ctx);
    }
    return ESP_OK;
}

void rule_store_deinit(rule_store_t *store)
{
    if (!store) {
        return;
    }
    if (store->cfg.auto_flush) {
        flush_to_disk(store);
    }
    free(store->json);
    free(store);
}

esp_err_t rule_store_get_snapshot(rule_store_t *store, rule_store_snapshot_t *out_snapshot)
{
    if (!store || !out_snapshot) {
        return ESP_ERR_INVALID_ARG;
    }
    out_snapshot->json = store->json ? strdup(store->json) : NULL;
    if (store->json && !out_snapshot->json) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(out_snapshot->sha256, store->sha256, sizeof(store->sha256));
    return ESP_OK;
}

void rule_store_release_snapshot(rule_store_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    free(snapshot->json);
    snapshot->json = NULL;
}

static bool sha_matches(const char *expected, const char actual[65])
{
    if (!expected || expected[0] == '\0') {
        return true;
    }
    return strcasecmp(expected, actual) == 0;
}

esp_err_t rule_store_update(rule_store_t *store,
                            rule_store_source_t source,
                            const char *json_payload,
                            const char *expected_sha256,
                            bool *out_changed)
{
    if (!store || !json_payload) {
        return ESP_ERR_INVALID_ARG;
    }
    char new_sha[65];
    compute_sha256_hex(json_payload, new_sha);
    if (!sha_matches(expected_sha256, new_sha)) {
        ESP_LOGW(TAG, "%s provided checksum %s does not match payload sha %s",
                 rule_store_source_name(source),
                 expected_sha256 ? expected_sha256 : "(null)",
                 new_sha);
        return ESP_ERR_INVALID_CRC;
    }

    bool changed = store->json == NULL || strcmp(store->json, json_payload) != 0;
    if (!changed) {
        ESP_LOGI(TAG, "Rules unchanged (%s)", rule_store_source_name(source));
        if (out_changed) {
            *out_changed = false;
        }
        return ESP_OK;
    }

    char *copy = strdup(json_payload);
    if (!copy) {
        return ESP_ERR_NO_MEM;
    }
    free(store->json);
    store->json = copy;
    memcpy(store->sha256, new_sha, sizeof(new_sha));

    esp_err_t flush_err = flush_to_disk(store);
    if (flush_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist new rules file (%s)", esp_err_to_name(flush_err));
    }

    ESP_LOGI(TAG, "Rules updated from %s (sha=%s)", rule_store_source_name(source), store->sha256);
    if (store->observer) {
        rule_store_snapshot_t snapshot = {
            .json = store->json,
        };
        memcpy(snapshot.sha256, store->sha256, sizeof(store->sha256));
        store->observer(&snapshot, source, store->observer_ctx);
    }
    if (out_changed) {
        *out_changed = true;
    }
    return ESP_OK;
}

esp_err_t rule_store_set_observer(rule_store_t *store,
                                  rule_store_observer_cb_t callback,
                                  void *ctx)
{
    if (!store) {
        return ESP_ERR_INVALID_ARG;
    }
    store->observer = callback;
    store->observer_ctx = ctx;
    if (callback && store->json) {
        rule_store_snapshot_t snapshot = {
            .json = store->json,
        };
        memcpy(snapshot.sha256, store->sha256, sizeof(store->sha256));
        callback(&snapshot, RULE_STORE_SOURCE_BOOT, ctx);
    }
    return ESP_OK;
}

const char *rule_store_source_name(rule_store_source_t source)
{
    switch (source) {
    case RULE_STORE_SOURCE_BOOT:
        return "boot";
    case RULE_STORE_SOURCE_AWS:
        return "aws";
    case RULE_STORE_SOURCE_MATTER:
        return "matter";
    case RULE_STORE_SOURCE_LOCAL_TEST:
        return "local-test";
    default:
        return "unknown";
    }
}
