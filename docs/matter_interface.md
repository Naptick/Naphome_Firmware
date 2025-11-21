# Matter Interface Bridge

The Matter bridge component allows the firmware to surface logical sensors
(`environment`, `iaq`, `light`, etc.) to a Matter fabric while keeping the
sensor manager and drivers decoupled from the transport layer.

## Compatibility & Requirements

- **ESP-IDF**: v5.0 or later
- **Matter Specification**: Matter 1.0+ (when using esp-matter)
- **esp-matter**: Required only when `CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER=y`
  - Clone from [Espressif's esp-matter repository](https://github.com/espressif/esp-matter)
  - Add to `EXTRA_COMPONENT_DIRS` in your project
- **Stub Mode**: Default build works without esp-matter for evaluation and CI

## Quick Start

### Stub Mode (No esp-matter required)

1. **Enable Matter bridge** in `idf.py menuconfig`:
   - Component config → Naphome Matter Bridge → Enable Matter bridge

2. **Initialize and register sensors**:
   ```c
   matter_bridge_init(&(matter_bridge_config_t){0});
   matter_bridge_start();
   matter_bridge_register_sensor(&env_registration);
   sensor_manager_set_observer(matter_bridge_sensor_observer, NULL);
   sensor_manager_start();
   ```

3. **Monitor logs** for sensor updates: `[environment] temperature_c=23.951`

### esp-matter Mode

1. Clone esp-matter and add to `EXTRA_COMPONENT_DIRS`
2. Enable `CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER` in menuconfig
3. Implement endpoint creation TODOs in `matter_bridge.c`
4. Flash and pair with Matter controller

See [Registering Sensors](#registering-sensors) and [Testing & Troubleshooting](#testing--troubleshooting) for details.

## Overview

The Matter bridge uses an observer pattern to decouple sensor collection from Matter transport:

- `sensor_manager` collects samples from registered sensors and emits JSON snapshots via an observer
  callback every publish interval (`CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS`).
- `matter_bridge` subscribes to those snapshots as an observer, maintains a registry of logical
  sensors, and converts fields to Matter-friendly attributes.
- When `CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER=y` the bridge allocates
  dynamic endpoints using Espressif's `esp-matter` component and pushes updates to real Matter clusters.
- When `CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER=n` (stub mode), the bridge logs sensor updates
  without interacting with the Matter stack, useful for bring-up and CI testing.

```
┌──────────────────────────────────────────────────────────────────────┐
│                         Sensor Manager                                │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │
│  │ environment  │  │     iaq      │  │    light     │  ...          │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘               │
│         │                 │                  │                        │
│         └─────────────────┼──────────────────┘                        │
│                           │                                           │
│                           ▼                                           │
│              JSON Observer Callback (every publish_interval_ms)      │
└───────────────────────────┼───────────────────────────────────────────┘
                            │
                            │ sensor_manager_set_observer()
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
        ▼                   ▼                   ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│matter_bridge │    │  AWS IoT     │    │  Other        │
│  (observer)  │    │  (observer)  │    │  observers    │
└──────┬───────┘    └──────────────┘    └──────────────┘
       │
       │ CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER?
       │
   ┌───┴───┐
   │  Yes  │  No (stub mode)
   ▼       ▼
┌──────────────┐    ┌──────────────┐
│  esp-matter │    │  Log only    │
│  endpoints  │    │  (no Matter) │
└──────┬──────┘    └──────────────┘
       │
       ▼
Matter fabric / controllers
(Apple Home, Google Home, etc.)
```

## Kconfig Options

Navigate to **Component config → Naphome Matter Bridge** in `idf.py menuconfig`:

| Option | Default | Purpose |
| --- | --- | --- |
| `CONFIG_NAPHOME_MATTER_BRIDGE_ENABLE` | `n` | Master switch. Enables the bridge and registers observer with `sensor_manager`. When disabled, bridge functions return `ESP_ERR_NOT_SUPPORTED`. |
| `CONFIG_NAPHOME_MATTER_BRIDGE_MAX_SENSORS` | `8` | Maximum number of logical sensors in the bridge registry (range 1-32). Increase if you have more than 8 sensor types. |
| `CONFIG_NAPHOME_MATTER_BRIDGE_SENSOR_NAME_MAX_LEN` | `32` | Maximum length of sensor identifier string copied into registry (range 8-96). Must accommodate longest sensor name. |
| `CONFIG_NAPHOME_MATTER_BRIDGE_ENDPOINT_LABEL_MAX_LEN` | `32` | Maximum length of endpoint label surfaced to Matter controllers (range 8-64). Friendly name shown in Home apps. |
| `CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER` | `n` | Link against `esp-matter` and create real Matter endpoints. Requires esp-matter in `EXTRA_COMPONENT_DIRS`. When `n`, runs in stub mode (logs only). |

## Registering Sensors

### Explicit Registration (Recommended)

Components should register sensors explicitly before `sensor_manager` starts to provide metadata:

```c
#include "matter_bridge.h"
#include "sensor_manager.h"

void app_main(void)
{
    // Initialize Matter bridge first
    matter_bridge_config_t bridge_cfg = {
        .enable_matter_console = false,  // Set true for Matter console debugging
    };
    
    esp_err_t err = matter_bridge_init(&bridge_cfg);
    if (err != ESP_OK) {
        ESP_LOGE("app", "Failed to init Matter bridge: %s", esp_err_to_name(err));
        return;
    }
    
    err = matter_bridge_start();
    if (err != ESP_OK) {
        ESP_LOGE("app", "Failed to start Matter bridge: %s", esp_err_to_name(err));
        return;
    }
    
    // Register sensors with Matter bridge
    const sensor_manager_sensor_t env_sensor = {
        .name = "environment",
        .sample_cb = environment_sensor_sample,
    };
    
    matter_bridge_sensor_registration_t env_registration = {
        .sensor_name = env_sensor.name,
        .sensor_kind = MATTER_BRIDGE_SENSOR_KIND_ENVIRONMENT,  // Maps to Matter clusters
        .endpoint_label = "Environment",  // Shown in Home apps
    };
    
    err = matter_bridge_register_sensor(&env_registration);
    if (err != ESP_OK) {
        ESP_LOGE("app", "Failed to register environment sensor: %s", esp_err_to_name(err));
    }
    
    // Register other sensors...
    matter_bridge_sensor_registration_t iaq_registration = {
        .sensor_name = "iaq",
        .sensor_kind = MATTER_BRIDGE_SENSOR_KIND_IAQ,
        .endpoint_label = "Air Quality",
    };
    matter_bridge_register_sensor(&iaq_registration);
    
    // Set Matter bridge as sensor_manager observer
    sensor_manager_set_observer(matter_bridge_sensor_observer, NULL);
    
    // Initialize and start sensor manager
    sensor_manager_init(NULL);
    sensor_manager_register(&env_sensor);
    // Register other sensors...
    sensor_manager_start();
}
```

### Auto-Registration (Fallback)

If a sensor sample arrives for an unregistered sensor, the bridge automatically registers it with `MATTER_BRIDGE_SENSOR_KIND_GENERIC`. This allows telemetry to flow while configuration is refined, but explicit registration is preferred for proper Matter cluster mapping.

### Sensor Kinds

Available sensor kinds map to Matter clusters:

| Kind | Matter Clusters | Typical Sensors |
| --- | --- | --- |
| `MATTER_BRIDGE_SENSOR_KIND_ENVIRONMENT` | Temperature, Relative Humidity | Temperature/humidity sensors (SHT45, etc.) |
| `MATTER_BRIDGE_SENSOR_KIND_IAQ` | Air Quality, CO2, TVOC | Air quality sensors (SCD41, SGP40, etc.) |
| `MATTER_BRIDGE_SENSOR_KIND_LIGHT` | Illuminance | Light sensors (OPT3002, etc.) |
| `MATTER_BRIDGE_SENSOR_KIND_GENERIC` | Generic attributes | Unknown or custom sensors |

## Stub Mode vs. esp-matter Mode

### Stub Mode (Default)

When `CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER=n`, the bridge runs in stub mode:

- ✅ No esp-matter dependency required
- ✅ Logs all sensor updates for debugging
- ✅ Useful for CI/testing without Matter stack
- ❌ No actual Matter endpoints created
- ❌ Cannot pair with Matter controllers

Example stub mode output:
```
I (8723) matter_bridge: [environment] temperature_c=23.951
I (8723) matter_bridge: [environment] humidity_pct=47.821
I (8723) matter_bridge: [iaq] co2_ppm=412
I (8723) matter_bridge: [iaq] tvoc_ppb=125
```

### esp-matter Mode

When `CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER=y`, the bridge creates real Matter endpoints:

#### Setup Steps

1. **Clone esp-matter**:
   ```bash
   git clone --recursive https://github.com/espressif/esp-matter.git
   cd esp-matter
   ./install.sh
   ```

2. **Add to project**: Set `EXTRA_COMPONENT_DIRS` in your `CMakeLists.txt` or `idf.py menuconfig`:
   ```cmake
   set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../esp-matter/components")
   ```

3. **Enable in menuconfig**:
   - Navigate to **Component config → Naphome Matter Bridge**
   - Enable `CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER`
   - Configure esp-matter settings as needed

4. **Implement endpoint creation**: The bridge currently has TODOs in `matter_bridge.c` for:
   - Allocating Matter endpoints per sensor kind
   - Writing attributes to Matter clusters
   - Handling Matter attribute reads

5. **Flash and pair**: 
   - Flash firmware to device
   - Use Matter commissioning (QR code, manual pairing)
   - Pair with Apple Home, Google Home, or CHIP tool

#### Integration Example

```c
#if CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER
#include "esp_matter.h"
#include "esp_matter_endpoint.h"

// In matter_bridge.c, implement endpoint creation:
static void matter_bridge_create_endpoint(matter_bridge_sensor_entry_t *entry)
{
    // Create Matter endpoint for this sensor
    esp_matter_endpoint_t *endpoint = esp_matter_endpoint_create(
        NULL,  // parent endpoint
        endpoint_id,  // unique ID
        ESP_MATTER_ENDPOINT_FLAG_NONE,
        NULL
    );
    
    // Add clusters based on sensor_kind
    if (entry->kind == MATTER_BRIDGE_SENSOR_KIND_ENVIRONMENT) {
        // Add Temperature Measurement cluster
        // Add Relative Humidity Measurement cluster
    }
    
    entry->endpoint_handle = endpoint;
}
#endif
```

## Integration with Sensor Manager and AWS IoT

The Matter bridge works alongside AWS IoT without conflict:

```c
void app_main(void)
{
    // Initialize both AWS IoT and Matter
    somnus_mqtt_start(NULL);  // AWS IoT
    matter_bridge_init(&bridge_cfg);  // Matter
    matter_bridge_start();
    
    // Register Matter observer
    sensor_manager_set_observer(matter_bridge_sensor_observer, NULL);
    
    // You can also set additional observers for AWS IoT
    // Both receive the same sensor snapshots via observer pattern
    sensor_manager_init(NULL);
    sensor_manager_start();
}
```

The observer pattern allows `sensor_manager` to notify multiple consumers (Matter, AWS IoT, logging) simultaneously without coupling.

## Testing & Troubleshooting

### Quick Start Checklist

1. **Enable Matter Bridge**: Verify `CONFIG_NAPHOME_MATTER_BRIDGE_ENABLE=y` in menuconfig
   ```bash
   idf.py menuconfig
   # Component config → Naphome Matter Bridge → Enable Matter bridge
   ```

2. **Check Initialization**: Monitor logs for bridge startup
   ```bash
   idf.py monitor | grep -i "matter_bridge"
   # Success: "Matter bridge initialised"
   # Error: "Failed to initialise Matter bridge" or "Bridge not initialized"
   ```

3. **Verify Sensor Registration**: Check that sensors are registered
   ```bash
   idf.py monitor | grep -i "matter_bridge"
   # Should see sensor updates: "[environment] temperature_c=..."
   ```

### Common Issues

#### Issue: "Matter bridge disabled" or `ESP_ERR_NOT_SUPPORTED`
**Symptoms**: Bridge functions return `ESP_ERR_NOT_SUPPORTED`, logs show "Matter bridge disabled"

**Solutions**:
- Enable `CONFIG_NAPHOME_MATTER_BRIDGE_ENABLE` in menuconfig
- Rebuild: `idf.py build flash monitor`
- Check `#if CONFIG_NAPHOME_MATTER_BRIDGE_ENABLE` guards in your code

#### Issue: No sensor updates in logs (stub mode)
**Symptoms**: Bridge initialized but no sensor data logged

**Solutions**:
- Verify `sensor_manager_set_observer(matter_bridge_sensor_observer, NULL)` is called
- Check `sensor_manager` is started: `sensor_manager_start()`
- Verify sensors are registered with `sensor_manager_register()`
- Check `CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS` is reasonable (default 10s)
- Monitor `sensor_manager` logs to confirm samples are being collected

#### Issue: "Failed to register sensor" or registry full
**Symptoms**: `matter_bridge_register_sensor()` returns `ESP_ERR_NO_MEM` or `ESP_ERR_INVALID_SIZE`

**Solutions**:
- Increase `CONFIG_NAPHOME_MATTER_BRIDGE_MAX_SENSORS` if you have >8 sensors
- Check `CONFIG_NAPHOME_MATTER_BRIDGE_SENSOR_NAME_MAX_LEN` accommodates longest sensor name
- Verify sensor names are not empty and within length limits
- Check logs for "registry full" messages

#### Issue: esp-matter build fails
**Symptoms**: Compilation errors when `CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER=y`

**Solutions**:
- Verify esp-matter is cloned and in `EXTRA_COMPONENT_DIRS`
- Run `./install.sh` in esp-matter directory to install dependencies
- Check ESP-IDF version compatibility (v5.0+)
- Review esp-matter documentation for setup requirements
- Consider using stub mode (`CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER=n`) for development

#### Issue: Sensor updates not reaching Matter controllers
**Symptoms**: Device paired but no sensor data in Home apps

**Solutions** (esp-matter mode only):
- Verify `CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER=y` is enabled
- Check that endpoint creation TODOs in `matter_bridge.c` are implemented
- Monitor Matter logs for attribute write errors
- Verify sensor kinds map to correct Matter clusters
- Test with CHIP tool: `chip-tool read <endpoint-id> <cluster-id> <attribute-id>`
- Check Matter commissioning completed successfully

### Stub Mode Testing

Stub mode is ideal for testing without Matter stack:

```c
// In menuconfig: CONFIG_NAPHOME_MATTER_BRIDGE_USE_ESPMATTER=n
// This enables stub mode - no esp-matter required

void test_sensor_updates(void)
{
    matter_bridge_init(&(matter_bridge_config_t){0});
    matter_bridge_start();
    sensor_manager_set_observer(matter_bridge_sensor_observer, NULL);
    sensor_manager_start();
    
    // Wait for sensor updates
    vTaskDelay(pdMS_TO_TICKS(15000));
    
    // Check logs for: "matter_bridge: [sensor_name] field=value"
}
```

### Debug Logging

Enable verbose logging:
```c
// In menuconfig
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y
```

Key log tags:
- `matter_bridge` - Matter bridge component
- `sensor_manager` - Sensor collection and observer notifications

### Integration Testing

Test Matter bridge alongside AWS IoT:

```c
// Both observers receive sensor updates
sensor_manager_set_observer(matter_bridge_sensor_observer, NULL);
// AWS IoT also sets its own observer via somnus_mqtt

// Verify both work:
// 1. Check Matter logs show sensor updates
// 2. Check AWS IoT publishes telemetry
// 3. Both should receive same sensor snapshots
```
