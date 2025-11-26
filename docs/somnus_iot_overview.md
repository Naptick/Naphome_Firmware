# Somnus IoT Overview

**AWS IoT Core Integration for Naphome Firmware**

Version: 0.9  
Last Updated: 2024

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Sensor Data Publishing](#sensor-data-publishing)
4. [MQTT Topics](#mqtt-topics)
5. [Payload Formats](#payload-formats)
6. [Configuration](#configuration)
7. [Provisioning](#provisioning)
8. [Integration Guide](#integration-guide)

---

## Overview

The Naphome firmware integrates with AWS IoT Core via MQTT to provide:

- **Real-time sensor telemetry** - Environmental, air quality, and particulate data
- **Cloud command reception** - Remote device control and configuration
- **Structured logging** - Device status and diagnostic information
- **Automatic publishing** - Sensor data sent at configurable intervals for app monitoring

### Key Features

- Automatic sensor data publishing every **2 seconds** (configurable)
- Mutual TLS authentication with AWS IoT Core
- Automatic reconnection handling
- Support for multiple sensor types (temperature, humidity, VOC, CO₂, PM, light)
- Integration with Matter bridge for local fabric connectivity

### Compatibility

- **ESP-IDF**: v5.0 or later
- **AWS IoT Device SDK**: v3.0.0+ (embedded via `esp_aws_iot` component)
- **TLS**: mbedTLS (provided by ESP-IDF)
- **MQTT**: AWS IoT Core with mutual TLS authentication

---

## Architecture

The AWS IoT integration uses a three-layer architecture:

```
┌─────────────────────────────────────────────────────────────────┐
│ Application Layer                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐   │
│  │ somnus_mqtt  │  │sensor_manager│  │  Other components │   │
│  └──────┬───────┘  └──────┬───────┘  └───────────────────┘   │
└─────────┼──────────────────┼──────────────────────────────────┘
          │                  │
┌─────────┼──────────────────┼──────────────────────────────────┐
│ Service │                  │                                      │
│  Layer  │  ┌───────────────▼──────────────┐                     │
│         │  │   aws_iot (component)        │                     │
│         │  │  ┌────────────────────────┐  │                     │
│         │  │  │ aws_iot_client         │  │  Client wrapper     │
│         │  │  │ aws_iot_service        │  │  Background task    │
│         │  │  │ aws_iot_config         │  │  Config loader      │
│         │  │  └──────────┬─────────────┘  │                     │
│         └──┼────────────┼────────────────┼────────────────────┘
└────────────┼────────────┼────────────────┼────────────────────┘
             │             │                  │
┌────────────┼────────────┼──────────────────┼──────────────────┐
│   SDK      │             │                  │                   │
│   Layer    │  ┌──────────▼──────────┐      │                   │
│            │  │  esp_aws_iot        │      │                   │
│            │  │  (AWS IoT Device SDK)│      │                   │
│            └──┴────────────────────┴──────┘                   │
└─────────────────────────────────────────────────────────────────┘
```

### Component Layers

1. **AWS IoT Device SDK** (`components/esp_aws_iot`)
   - Low-level MQTT client, TLS, and AWS IoT protocol handling
   - Ported to ESP-IDF with FreeRTOS threading and mbedTLS

2. **AWS IoT Wrapper** (`components/aws_iot`)
   - `aws_iot_client` - Thin wrapper over the AWS IoT Device SDK
   - `aws_iot_service` - Background FreeRTOS task for connection management
   - `aws_iot_config` - Configuration loader from Kconfig settings

3. **Application Layer**
   - `somnus_mqtt` - Somnus-specific MQTT interface
   - `sensor_manager` - Automatic sensor data collection and publishing
   - `somnus_profile` - Device ID and topic name helpers

---

## Sensor Data Publishing

### Automatic Publishing

The sensor manager **automatically publishes sensor data to MQTT** at a configurable interval for app monitoring. This requires no additional code - simply initialize and start the sensor manager.

**Default Configuration:**
- **Publish Interval**: 2 seconds (2000ms)
- **Topic**: `device/telemetry/{DEVICE_ID}`
- **QoS**: 1 (at least once delivery)

### Configuration

The sensor publish interval is configurable via:

**Menuconfig:**
- Navigate to: Component config → Sensor Manager → Sensor telemetry publish interval (ms)
- Default: 2000ms (2 seconds)
- Range: 1000ms (1 second) to 600000ms (10 minutes)

**Config File:**
```ini
# config/sdkconfig.defaults
CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS=2000
```

**Runtime Configuration:**
```c
sensor_manager_config_t cfg = {
    .publish_interval_ms = 2000,  // 2 seconds
};
sensor_manager_init(&cfg);
```

### Sensor Data Types

The following sensor data is automatically published:

| Sensor | Data Provided |
|--------|--------------|
| **SHT45** | Temperature (°C), Humidity (%) |
| **SGP40** | VOC Index |
| **SCD40** | CO₂ (ppm), Temperature (°C), Humidity (%) |
| **VCNL4040** | Ambient Light (lux), Proximity |
| **EC10** | PM1.0, PM2.5, PM10 (μg/m³) |

### Publishing Behavior

- Sensor data is collected at 1Hz (every 1 second)
- Telemetry is published to MQTT at the configured interval (default: every 2 seconds)
- Each telemetry message contains the latest readings from all available sensors
- Publishing continues automatically as long as:
  - Sensor manager is running
  - MQTT connection is established
  - At least one sensor is registered

---

## MQTT Topics

Topics are derived from the Somnus device ID (`SOMNUS_` + station MAC address).

| Purpose | Topic Pattern | Direction | QoS |
|---------|---------------|-----------|-----|
| **Telemetry** | `device/telemetry/{DEVICE_ID}` | Publish | 1 |
| **Logs** | `device/receive/uat/{DEVICE_ID}` | Publish | 1 |
| **Commands** | `device/somnus/{DEVICE_ID}` | Subscribe | 1 |

### Example Topics

For device `SOMNUS_112233445566`:
- Telemetry: `device/telemetry/SOMNUS_112233445566`
- Logs: `device/receive/uat/SOMNUS_112233445566`
- Commands: `device/somnus/SOMNUS_112233445566`

---

## Payload Formats

### Telemetry Payload

The sensor manager automatically creates and publishes JSON telemetry payloads:

```json
{
  "deviceId": "SOMNUS_112233445566",
  "timestamp_ms": 1721165305123,
  "sht45": {
    "temperature_c": 24.1,
    "humidity_rh": 48.3,
    "synthetic": false
  },
  "sgp40": {
    "voc_index": 125,
    "voc_ticks": 1250,
    "synthetic": false
  },
  "scd40": {
    "co2_ppm": 450.0,
    "temperature_c": 24.0,
    "humidity_rh": 48.0,
    "synthetic": false
  },
  "vcnl4040": {
    "ambient_lux": 200,
    "proximity": 0,
    "synthetic": false
  },
  "ec10": {
    "pm1_0_ug_m3": 10,
    "pm2_5_ug_m3": 15,
    "pm10_ug_m3": 22,
    "synthetic": false
  }
}
```

**Payload Fields:**
- `deviceId` - Somnus device identifier
- `timestamp_ms` - Unix timestamp in milliseconds
- `{sensor_name}` - Sensor-specific data object
- `synthetic` - Boolean indicating if data is synthetic (for testing)

**Note:** Only sensors that are available and have valid readings are included in the payload.

### Log Payload

Structured log events published to the log topic:

```json
{
  "deviceId": "SOMNUS_112233445566",
  "timestamp_ms": 1721165305123,
  "level": "INFO",
  "stage": "AfterOnboarding",
  "message": "Device connected and ready for commands"
}
```

**Log Levels:** `INFO`, `WARN`, `ERROR`, `DEBUG`  
**Stages:** `Onboarding`, `AfterOnboarding` (auto-detected from message content)

### Command Payload

Incoming commands from AWS IoT:

```json
{
  "Action": "SetVolume",
  "Data": {
    "Volume": 50
  }
}
```

Or routine lists:

```json
[
  {
    "PreSleepRoutine": [...],
    "SleepRoutine": [...],
    "WakeUpRoutine": [...]
  }
]
```

---

## Configuration

### AWS IoT Core Settings

Navigate to **Component config → Naphome AWS IoT**:

| Option | Default | Purpose |
|--------|---------|---------|
| `CONFIG_NAPHOME_AWS_IOT_ENDPOINT` | `""` | AWS IoT Core endpoint hostname |
| `CONFIG_NAPHOME_AWS_IOT_CLIENT_ID` | `""` | Client ID / Thing name |
| `CONFIG_NAPHOME_AWS_IOT_PORT` | `8883` | MQTT TLS port |
| `CONFIG_NAPHOME_AWS_IOT_KEEPALIVE_SEC` | `60` | Keep-alive interval (10-1200s) |
| `CONFIG_NAPHOME_AWS_IOT_CLEAN_SESSION` | `y` | Request clean session on connect |
| `CONFIG_NAPHOME_AWS_IOT_AUTO_RECONNECT` | `y` | Enable automatic reconnection |

### Somnus MQTT Settings

Navigate to **Component config → Somnus MQTT Integration**:

| Option | Default | Purpose |
|--------|---------|---------|
| `CONFIG_SOMNUS_MQTT_CERT_DISCOVERY` | `y` | Enable runtime certificate discovery |
| `CONFIG_SOMNUS_MQTT_CERT_DIR` | `"/spiffs/Cert"` | Certificate directory path |
| `CONFIG_SOMNUS_MQTT_SUBSCRIBE_QOS` | `1` | QoS for command subscription |

### Sensor Manager Settings

Navigate to **Component config → Sensor Manager**:

| Option | Default | Purpose |
|--------|---------|---------|
| `CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS` | `2000` | Sensor telemetry publish interval (ms) |

---

## Provisioning

### Certificate Provisioning

Use the provisioning script to create AWS IoT Things and certificates:

```bash
python scripts/provision_aws_thing.py \
  --thing-name SOMNUS_ABCDEF123456 \
  --policy-name SomnusDevicePolicy \
  --output-dir components/aws_iot/certs/generated/SOMNUS_ABCDEF123456
```

### Certificate Files

The runtime discovery logic looks for:
- `<THING>-certificate.pem.crt` - Device certificate
- `<THING>-private.pem.key` - Private key
- `AmazonRootCA1.pem` - Root CA certificate

### Certificate Storage

Certificates can be stored in:
1. **SPIFFS filesystem** - Runtime discovery from `CONFIG_SOMNUS_MQTT_CERT_DIR`
2. **Embedded binaries** - Build-time embedded PEM files (fallback)

### Quick Provisioning

For the Korvo Voice Assistant sample:

```bash
python scripts/provision_and_stage_somnus_cert.py \
  --thing-name SOMNUS_ABCDEF123456
```

This script:
1. Provisions the AWS IoT Thing
2. Generates certificates
3. Copies certificates to SPIFFS image
4. Prepares for automatic inclusion in build

---

## Integration Guide

### Basic Initialization

```c
#include "somnus_mqtt.h"
#include "sensor_manager.h"
#include "sensor_integration.h"

void app_main(void)
{
    // Initialize Wi-Fi, NVS, etc.
    
    // Start Somnus MQTT service
    somnus_mqtt_config_t mqtt_cfg = {
        .action_cb = handle_mqtt_action,  // Optional command handler
        .action_ctx = NULL,
    };
    
    esp_err_t err = somnus_mqtt_start(&mqtt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE("app", "Failed to start MQTT: %s", esp_err_to_name(err));
        return;
    }
    
    // Initialize sensor integration
    sensor_integration_init();
    
    // Start sensor integration (begins automatic MQTT publishing)
    sensor_integration_start();
    
    // Sensor data will now be published automatically every 2 seconds
}
```

### Command Handler

```c
static void handle_mqtt_action(const char *payload, void *ctx)
{
    ESP_LOGI("app", "Received MQTT action: %s", payload);
    
    cJSON *json = cJSON_Parse(payload);
    if (json) {
        cJSON *action = cJSON_GetObjectItem(json, "Action");
        if (cJSON_IsString(action)) {
            const char *action_str = action->valuestring;
            if (strcmp(action_str, "SetVolume") == 0) {
                // Handle volume change
            }
        }
        cJSON_Delete(json);
    }
}
```

### Manual Telemetry Publishing

While sensor data is published automatically, you can also publish custom telemetry:

```c
cJSON *root = cJSON_CreateObject();
cJSON_AddStringToObject(root, "deviceId", somnus_mqtt_get_device_id());
cJSON_AddNumberToObject(root, "timestamp_ms", esp_timer_get_time() / 1000);
cJSON_AddNumberToObject(root, "custom_metric", 42.0);

char *json_str = cJSON_PrintUnformatted(root);
if (json_str) {
    somnus_mqtt_publish_telemetry(json_str);
    free(json_str);
}
cJSON_Delete(root);
```

### Log Publishing

```c
// Simple log message
somnus_mqtt_publish_log("INFO", "Device booted successfully");

// Log with automatic stage detection
somnus_mqtt_publish_log("WARN", "Wi-Fi connection failed during onboarding");
// Stage will be inferred as "Onboarding" based on message content
```

---

## Testing & Verification

### Verify Sensor Data Publishing

1. **Monitor MQTT Topic:**
   ```bash
   # Using AWS IoT Console Test client
   # Subscribe to: device/telemetry/{DEVICE_ID}
   ```

2. **Check Message Frequency:**
   - Messages should arrive every 2 seconds (or configured interval)
   - Each message should contain latest sensor readings

3. **Verify Payload:**
   - Check for `deviceId` field
   - Verify `timestamp_ms` updates with each message
   - Confirm all sensor data fields are present

### Common Issues

**Issue: No telemetry messages received**
- Verify MQTT connection is established
- Check sensor manager is started: `sensor_manager_start()`
- Verify at least one sensor is registered
- Check AWS IoT policy allows publish to telemetry topic

**Issue: Messages arrive at wrong interval**
- Verify `CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS` setting
- Check if custom config was provided to `sensor_manager_init()`
- Rebuild firmware after changing config

**Issue: Missing sensor data in payload**
- Verify sensor integration is initialized: `sensor_integration_init()`
- Check sensor availability flags in payload
- Review sensor driver initialization logs

---

## Summary

The Naphome firmware provides automatic sensor data publishing to AWS IoT Core via MQTT:

- ✅ **Automatic publishing** every 2 seconds (configurable)
- ✅ **Multiple sensor types** (temperature, humidity, VOC, CO₂, PM, light)
- ✅ **Structured JSON payloads** with device ID and timestamps
- ✅ **No additional code required** - works automatically
- ✅ **Configurable interval** from 1 second to 10 minutes
- ✅ **Reliable delivery** with QoS 1

For detailed API documentation and troubleshooting, see `docs/aws_iot_interface.md`.

---

**Document Version:** 0.9  
**Last Updated:** 2024  
**Firmware Version:** Naphome Phase 0.9
