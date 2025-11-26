#include "matter_bridge.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "matter_bridge";

#if !CONFIG_NAPHOME_MATTER_BRIDGE_ENABLE

esp_err_t matter_bridge_init(const matter_bridge_config_t *config)
{
    (void)config;
    ESP_LOGI(TAG, "Matter bridge disabled (CONFIG_NAPHOME_MATTER_BRIDGE_ENABLE=n)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t matter_bridge_start(void)
{
    ESP_LOGI(TAG, "Matter bridge disabled");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t matter_bridge_register_sensor(const matter_bridge_sensor_registration_t *registration)
{
    (void)registration;
    return ESP_ERR_NOT_SUPPORTED;
}

void matter_bridge_sensor_observer(const char *sensor_name, const cJSON *sensor_state, void *user_ctx)
{
    (void)sensor_name;
    (void)sensor_state;
    (void)user_ctx;
}

#else

typedef struct {
    bool in_use;
    matter_bridge_sensor_kind_t kind;
    char name[CONFIG_NAPHOME_MATTER_BRIDGE_SENSOR_NAME_MAX_LEN];
    char label[CONFIG_NAPHOME_MATTER_BRIDGE_ENDPOINT_LABEL_MAX_LEN];
#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
    // Placeholder for esp-matter endpoint handles
    void *endpoint_handle;
#endif
} matter_bridge_sensor_entry_t;

typedef struct {
    bool in_use;
    matter_bridge_device_kind_t kind;
    char label[CONFIG_NAPHOME_MATTER_BRIDGE_ENDPOINT_LABEL_MAX_LEN];
    void *device_handle;  // Device context (e.g., spotify_client_t*)
#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
    void *endpoint_handle;
#endif
} matter_bridge_device_entry_t;

static SemaphoreHandle_t s_registry_lock;
static matter_bridge_sensor_entry_t s_registry[CONFIG_NAPHOME_MATTER_BRIDGE_MAX_SENSORS];
#define MATTER_BRIDGE_MAX_DEVICES 4
static matter_bridge_device_entry_t s_device_registry[MATTER_BRIDGE_MAX_DEVICES];
static bool s_initialized;
static bool s_started;
static matter_bridge_config_t s_cfg;

static void matter_bridge_copy_string(char *dest, size_t dest_len, const char *src)
{
    if (!dest || dest_len == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t copy_len = strnlen(src, dest_len - 1);
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

static matter_bridge_sensor_entry_t *matter_bridge_find_entry(const char *sensor_name)
{
    if (!sensor_name) {
        return NULL;
    }
    for (size_t i = 0; i < CONFIG_NAPHOME_MATTER_BRIDGE_MAX_SENSORS; ++i) {
        if (s_registry[i].in_use && strcmp(s_registry[i].name, sensor_name) == 0) {
            return &s_registry[i];
        }
    }
    return NULL;
}

static matter_bridge_sensor_entry_t *matter_bridge_alloc_entry(const char *sensor_name)
{
    if (!sensor_name) {
        return NULL;
    }
    for (size_t i = 0; i < CONFIG_NAPHOME_MATTER_BRIDGE_MAX_SENSORS; ++i) {
        if (!s_registry[i].in_use) {
            s_registry[i].in_use = true;
            s_registry[i].kind = MATTER_BRIDGE_SENSOR_KIND_GENERIC;
            matter_bridge_copy_string(s_registry[i].name,
                                      sizeof(s_registry[i].name),
                                      sensor_name);
            s_registry[i].label[0] = '\0';
#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
            s_registry[i].endpoint_handle = NULL;
#endif
            return &s_registry[i];
        }
    }
    return NULL;
}

static void matter_bridge_log_state(const matter_bridge_sensor_entry_t *entry, const cJSON *sensor_state)
{
    const cJSON *child = sensor_state ? sensor_state->child : NULL;
    while (child) {
        const char *key = child->string ? child->string : "(unnamed)";
        if (cJSON_IsNumber(child)) {
            ESP_LOGI(TAG, "[%s] %s=%.3f", entry->name, key, child->valuedouble);
        } else if (cJSON_IsString(child)) {
            ESP_LOGI(TAG, "[%s] %s=\"%s\"", entry->name, key, child->valuestring);
        } else if (cJSON_IsBool(child)) {
            ESP_LOGI(TAG, "[%s] %s=%s", entry->name, key, cJSON_IsTrue(child) ? "true" : "false");
        } else {
            ESP_LOGD(TAG, "[%s] %s (type=%d) skipped", entry->name, key, child->type);
        }
        child = child->next;
    }
}

esp_err_t matter_bridge_init(const matter_bridge_config_t *config)
{
    if (s_initialized) {
        return ESP_OK;
    }

    memset(&s_cfg, 0, sizeof(s_cfg));
    if (config) {
        s_cfg = *config;
    }

    s_registry_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_registry_lock, ESP_ERR_NO_MEM, TAG, "Failed to create registry mutex");

    memset(s_registry, 0, sizeof(s_registry));
    memset(s_device_registry, 0, sizeof(s_device_registry));
    s_initialized = true;
    ESP_LOGI(TAG, "Matter bridge initialised");
    return ESP_OK;
}

esp_err_t matter_bridge_start(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "Bridge not initialized");
    if (s_started) {
        return ESP_OK;
    }

#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
    ESP_LOGI(TAG, "Matter bridge starting with esp-matter integration");
    // TODO: Add esp-matter node/endpoint bring-up
#else
    ESP_LOGI(TAG, "Matter bridge running in stub mode (logging only)");
#endif

    s_started = true;
    return ESP_OK;
}

esp_err_t matter_bridge_register_sensor(const matter_bridge_sensor_registration_t *registration)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "Bridge not initialized");
    ESP_RETURN_ON_FALSE(registration, ESP_ERR_INVALID_ARG, TAG, "Registration pointer is NULL");
    ESP_RETURN_ON_FALSE(registration->sensor_name,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "Sensor name is NULL");

    if (xSemaphoreTake(s_registry_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    matter_bridge_sensor_entry_t *entry = matter_bridge_find_entry(registration->sensor_name);
    if (!entry) {
        entry = matter_bridge_alloc_entry(registration->sensor_name);
        if (!entry) {
            xSemaphoreGive(s_registry_lock);
            ESP_LOGE(TAG, "Sensor registry is full");
            return ESP_ERR_NO_MEM;
        }
    }

    entry->kind = registration->sensor_kind;
    matter_bridge_copy_string(entry->label, sizeof(entry->label), registration->endpoint_label);

#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
    // TODO: Allocate or update esp-matter endpoints as required
#endif

    xSemaphoreGive(s_registry_lock);
    ESP_LOGI(TAG, "Registered sensor '%s' (kind=%d)", registration->sensor_name, entry->kind);
    return ESP_OK;
}

void matter_bridge_sensor_observer(const char *sensor_name, const cJSON *sensor_state, void *user_ctx)
{
    (void)user_ctx;
    if (!sensor_name || !sensor_state) {
        return;
    }
    if (!s_started) {
        return;
    }

    if (xSemaphoreTake(s_registry_lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire registry lock");
        return;
    }

    matter_bridge_sensor_entry_t *entry = matter_bridge_find_entry(sensor_name);
    if (!entry) {
        entry = matter_bridge_alloc_entry(sensor_name);
        if (!entry) {
            ESP_LOGE(TAG, "Sensor registry capacity exceeded");
            xSemaphoreGive(s_registry_lock);
            return;
        }
        ESP_LOGI(TAG, "Auto-registered sensor '%s' with default kind", sensor_name);
    }

#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
    // TODO: Update esp-matter clusters/endpoints here
    // Map sensor data to Matter clusters:
    // - MATTER_BRIDGE_SENSOR_KIND_ENVIRONMENT -> Temperature Measurement (0x0402), Relative Humidity (0x0405)
    // - MATTER_BRIDGE_SENSOR_KIND_IAQ -> Air Quality (0x042C), Carbon Dioxide (0x040D)
    // - MATTER_BRIDGE_SENSOR_KIND_LIGHT -> Illuminance Measurement (0x0400)
    // - MATTER_BRIDGE_SENSOR_KIND_PM -> Air Quality (0x042C) with PM2.5/PM10 attributes
#else
    matter_bridge_log_state(entry, sensor_state);
#endif

    xSemaphoreGive(s_registry_lock);
}

static matter_bridge_device_entry_t *matter_bridge_find_device(matter_bridge_device_kind_t kind)
{
    for (size_t i = 0; i < MATTER_BRIDGE_MAX_DEVICES; ++i) {
        if (s_device_registry[i].in_use && s_device_registry[i].kind == kind) {
            return &s_device_registry[i];
        }
    }
    return NULL;
}

static matter_bridge_device_entry_t *matter_bridge_alloc_device(void)
{
    for (size_t i = 0; i < MATTER_BRIDGE_MAX_DEVICES; ++i) {
        if (!s_device_registry[i].in_use) {
            s_device_registry[i].in_use = true;
            s_device_registry[i].device_handle = NULL;
#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
            s_device_registry[i].endpoint_handle = NULL;
#endif
            return &s_device_registry[i];
        }
    }
    return NULL;
}

esp_err_t matter_bridge_register_device(const matter_bridge_device_registration_t *registration)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "Bridge not initialized");
    ESP_RETURN_ON_FALSE(registration, ESP_ERR_INVALID_ARG, TAG, "Registration pointer is NULL");

    if (xSemaphoreTake(s_registry_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    matter_bridge_device_entry_t *entry = matter_bridge_find_device(registration->device_kind);
    if (!entry) {
        entry = matter_bridge_alloc_device();
        if (!entry) {
            xSemaphoreGive(s_registry_lock);
            ESP_LOGE(TAG, "Device registry is full");
            return ESP_ERR_NO_MEM;
        }
    }

    entry->kind = registration->device_kind;
    entry->device_handle = registration->device_handle;
    matter_bridge_copy_string(entry->label, sizeof(entry->label), registration->endpoint_label);

#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
    // TODO: Create Matter endpoint for device
    // For Spotify: Create Media Playback cluster (0x0506) endpoint
    // - PlaybackState (0x0000): Playing, Paused, NotPlaying
    // - PlaybackSpeed (0x0001): float
    // - PlaybackSeekRangeEnd (0x0002): uint64_t
    // - PlaybackSeekRangeStart (0x0003): uint64_t
    // - PlaybackSupportedPlaybackSpeeds (0x0004): array
    // - PlaybackSupportedPlaybackCommands (0x0005): array
    // - PlaybackPositionUpdatedAt (0x0006): uint64_t
    // - PlaybackPosition (0x0007): uint64_t
    // - PlaybackDuration (0x0008): uint64_t
    // - PlaybackSampledPosition (0x0009): struct
    // - PlaybackPlaybackSpeed (0x000A): float
    // - PlaybackSeekedAt (0x000B): uint64_t
    // - PlaybackCurrentActiveAudioTrack (0x000C): struct
    // - PlaybackNextTrack (0x000D): struct
    // - PlaybackPreviousTrack (0x000E): struct
    // - PlaybackCurrentTrack (0x000F): struct
    // - PlaybackPlaybackRepeatMode (0x0010): enum
    // - PlaybackPlaybackSpeedRange (0x0011): struct
    // - PlaybackActiveAudioTrack (0x0012): struct
    // Commands:
    // - Play (0x00)
    // - Pause (0x01)
    // - Stop (0x02)
    // - StartOver (0x03)
    // - Previous (0x04)
    // - Next (0x05)
    // - Rewind (0x06)
    // - FastForward (0x07)
    // - SkipForward (0x08)
    // - SkipBackward (0x09)
    // - Seek (0x0A)
    // Also add Level Control cluster (0x0008) for volume:
    // - CurrentLevel (0x0000): uint8_t (0-100)
    // Commands:
    // - MoveToLevel (0x00)
    // - Move (0x01)
    // - Step (0x02)
    // - Stop (0x03)
    // - MoveToLevelWithOnOff (0x04)
    // - MoveWithOnOff (0x05)
    // - StepWithOnOff (0x06)
    // - StopWithOnOff (0x07)
#else
    ESP_LOGI(TAG, "Registered device '%s' (kind=%d) - stub mode", entry->label, entry->kind);
#endif

    xSemaphoreGive(s_registry_lock);
    ESP_LOGI(TAG, "Registered device '%s' (kind=%d)", entry->label, entry->kind);
    return ESP_OK;
}

esp_err_t matter_bridge_spotify_play(void *context)
{
    ESP_LOGI(TAG, "Matter: Spotify Play command");
#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
    // TODO: Call spotify_client_play() via context
    // spotify_client_t *spotify = (spotify_client_t *)context;
    // return spotify_client_play(spotify, NULL);
#else
    ESP_LOGI(TAG, "Spotify Play (stub mode)");
#endif
    return ESP_OK;
}

esp_err_t matter_bridge_spotify_pause(void *context)
{
    ESP_LOGI(TAG, "Matter: Spotify Pause command");
#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
    // TODO: Call spotify_client_pause() via context
    // spotify_client_t *spotify = (spotify_client_t *)context;
    // return spotify_client_pause(spotify);
#else
    ESP_LOGI(TAG, "Spotify Pause (stub mode)");
#endif
    return ESP_OK;
}

esp_err_t matter_bridge_spotify_resume(void *context)
{
    ESP_LOGI(TAG, "Matter: Spotify Resume command");
#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
    // TODO: Call spotify_client_resume() via context
    // spotify_client_t *spotify = (spotify_client_t *)context;
    // return spotify_client_resume(spotify);
#else
    ESP_LOGI(TAG, "Spotify Resume (stub mode)");
#endif
    return ESP_OK;
}

esp_err_t matter_bridge_spotify_volume_set(void *context, uint8_t volume_percent)
{
    ESP_LOGI(TAG, "Matter: Spotify Volume Set to %d%%", volume_percent);
#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
    // TODO: Call spotify_player_set_volume_percent() via context
    // spotify_client_t *spotify = (spotify_client_t *)context;
    // int current_volume = ...; // Get current volume
    // int delta = volume_percent - current_volume;
    // return spotify_client_volume_delta(spotify, delta);
#else
    ESP_LOGI(TAG, "Spotify Volume Set %d%% (stub mode)", volume_percent);
#endif
    return ESP_OK;
}

esp_err_t matter_bridge_spotify_volume_delta(void *context, int8_t delta_percent)
{
    ESP_LOGI(TAG, "Matter: Spotify Volume Delta %+d%%", delta_percent);
#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
    // TODO: Call spotify_client_volume_delta() via context
    // spotify_client_t *spotify = (spotify_client_t *)context;
    // return spotify_client_volume_delta(spotify, delta_percent);
#else
    ESP_LOGI(TAG, "Spotify Volume Delta %+d%% (stub mode)", delta_percent);
#endif
    return ESP_OK;
}

#endif /* CONFIG_NAPHOME_MATTER_BRIDGE_ENABLE */

