#pragma once

#include <stdbool.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MATTER_BRIDGE_SENSOR_KIND_GENERIC = 0,
    MATTER_BRIDGE_SENSOR_KIND_ENVIRONMENT,
    MATTER_BRIDGE_SENSOR_KIND_IAQ,
    MATTER_BRIDGE_SENSOR_KIND_LIGHT,
    MATTER_BRIDGE_SENSOR_KIND_PM,  // Particulate matter (PM2.5, PM10)
} matter_bridge_sensor_kind_t;

typedef enum {
    MATTER_BRIDGE_DEVICE_KIND_SPOTIFY = 0,  // Spotify Media Playback device
} matter_bridge_device_kind_t;

typedef struct {
    const char *sensor_name;
    matter_bridge_sensor_kind_t sensor_kind;
    const char *endpoint_label; /**< Optional friendly label surfaced in Matter fabric */
} matter_bridge_sensor_registration_t;

typedef struct {
    bool enable_matter_console;
} matter_bridge_config_t;

esp_err_t matter_bridge_init(const matter_bridge_config_t *config);
esp_err_t matter_bridge_start(void);
esp_err_t matter_bridge_register_sensor(const matter_bridge_sensor_registration_t *registration);
void matter_bridge_sensor_observer(const char *sensor_name, const cJSON *sensor_state, void *user_ctx);

// Spotify control registration
typedef struct {
    matter_bridge_device_kind_t device_kind;
    const char *endpoint_label;  // Friendly name (e.g., "Spotify Player")
    void *device_handle;  // Pointer to spotify_client_t or similar
} matter_bridge_device_registration_t;

esp_err_t matter_bridge_register_device(const matter_bridge_device_registration_t *registration);

// Matter attribute write callbacks (called from Matter controllers)
typedef struct {
    void *device_handle;  // Device context (e.g., spotify_client_t*)
} matter_bridge_spotify_context_t;

// Callback for Matter Media Playback commands
esp_err_t matter_bridge_spotify_play(void *context);
esp_err_t matter_bridge_spotify_pause(void *context);
esp_err_t matter_bridge_spotify_resume(void *context);
esp_err_t matter_bridge_spotify_volume_set(void *context, uint8_t volume_percent);
esp_err_t matter_bridge_spotify_volume_delta(void *context, int8_t delta_percent);

#ifdef __cplusplus
}
#endif
