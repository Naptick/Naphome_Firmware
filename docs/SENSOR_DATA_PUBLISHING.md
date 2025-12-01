# Sensor Data Publishing to AWS IoT

## Overview

The device publishes sensor telemetry data to AWS IoT Core via MQTT. This document explains how the publishing works and how to verify it's functioning.

## Architecture

```
sensor_integration (1Hz sampling)
    ↓
sensor_manager (collects & publishes)
    ↓
somnus_mqtt_publish_telemetry()
    ↓
AWS IoT Core MQTT
    ↓
Topic: device/telemetry/{DEVICE_ID}
```

## Components

### 1. Sensor Integration (`components/sensor_manager/src/sensor_integration.c`)

- Samples sensors at **1Hz** (every 1000ms)
- Generates synthetic sensor data (real sensors are currently disabled)
- Updates sensor cache with readings

### 2. Sensor Manager (`components/sensor_manager/src/sensor_manager.c`)

- Collects data from all registered sensors
- Publishes telemetry at configured interval (default: 10 seconds, but set to 1 second in sensor_integration)
- Creates JSON payload with all sensor data
- Publishes via `somnus_mqtt_publish_telemetry()`

### 3. MQTT Publishing (`components/somnus_mqtt/src/somnus_mqtt.c`)

- Topic format: `device/telemetry/{DEVICE_ID}`
- Example: `device/telemetry/SOMNUS_F09E9E3263A4`
- QoS: 1 (at least once delivery)
- Payload: JSON with sensor data

## Payload Format

```json
{
  "deviceId": "SOMNUS_F09E9E3263A4",
  "timestamp_ms": 1234567890,
  "sht45": {
    "temperature_c": 22.5,
    "humidity_rh": 50.0,
    "synthetic": true
  },
  "sgp40": {
    "voc_index": 120,
    "voc_ticks": 1200,
    "synthetic": true
  },
  "scd40": {
    "co2_ppm": 450.0,
    "temperature_c": 22.5,
    "humidity_rh": 50.0,
    "synthetic": true
  },
  "vcnl4040": {
    "ambient_lux": 200,
    "proximity": 0,
    "synthetic": true
  },
  "ec10": {
    "pm1_0_ug_m3": 10.5,
    "pm2_5_ug_m3": 15.0,
    "pm10_ug_m3": 22.5,
    "synthetic": true
  }
}
```

## Publishing Frequency

- **Sensor sampling**: 1Hz (1000ms)
- **Telemetry publishing**: 1Hz (1000ms) - configured in `sensor_integration.c`

## Verification Steps

### 1. Check Device is Connected to AWS IoT

Look for these log messages:
```
[SOMNUS_MQTT] Connected to AWS IoT as SOMNUS_XXXXXXXXXXXX
[SOMNUS_MQTT] Published Somnus readiness log
```

### 2. Check Sensor Manager is Running

Look for:
```
[sensor_integration] Sensor integration started (1Hz sampling)
[sensor_manager] (should be publishing every second)
```

### 3. Check for Publishing Errors

Look for warnings:
```
[sensor_manager] Telemetry publish failed (ESP_ERR_INVALID_STATE)
```

This usually means:
- MQTT client is not connected
- `somnus_mqtt_start()` failed
- AWS IoT connection not established

### 4. Verify via AWS IoT Test Client

1. Go to AWS IoT Console → Test → MQTT test client
2. Subscribe to topic: `device/telemetry/SOMNUS_F09E9E3263A4`
3. You should see messages arriving every ~1 second

### 5. Verify via Web Test Page

1. Open: https://naptick.github.io/Naphome_Firmware/aws-iot-test.html
2. Select device: `SOMNUS_F09E9E3263A4`
3. Click Connect
4. Should see sensor cards appearing within 1-2 seconds

## Troubleshooting

### No Data Received

**Possible causes:**

1. **Device not connected to AWS IoT**
   - Check WiFi connection
   - Check AWS IoT certificates are valid
   - Check device is online in AWS IoT Console

2. **Sensor Manager not started**
   - Verify `sensor_integration_start()` is called in main app
   - Check logs for sensor manager initialization

3. **MQTT connection failed**
   - Check `somnus_mqtt_start()` returned ESP_OK
   - Verify AWS IoT endpoint is correct
   - Check certificates are loaded

4. **Wrong topic subscription**
   - Verify subscribing to: `device/telemetry/{DEVICE_ID}`
   - Device ID must match exactly (case-sensitive)

### Data Format Mismatch

The web test page expects these sensor keys:
- `sht45` (temperature_c, humidity_rh)
- `sgp40` (voc_index, voc_ticks)
- `scd40` (co2_ppm, temperature_c, humidity_rh)
- `vcnl4040` (ambient_lux, proximity)
- `ec10` (pm1_0_ug_m3, pm2_5_ug_m3, pm10_ug_m3)

If sensor names or field names don't match, the web page won't display the data.

## Code Locations

- Sensor integration: `components/sensor_manager/src/sensor_integration.c`
- Sensor manager: `components/sensor_manager/src/sensor_manager.c`
- MQTT publishing: `components/somnus_mqtt/src/somnus_mqtt.c`
- Telemetry topic: `components/somnus_profile/src/somnus_profile.c`
- Main initialization: `main/naphome_voice_assistant_main.c` (line ~842)

## Next Steps

To ensure data is being published:

1. **Verify device is online** - Check AWS IoT Console → Devices
2. **Check device logs** - Look for sensor_manager and MQTT publish messages
3. **Test subscription** - Use AWS IoT Test Client to subscribe to the topic
4. **Verify web page** - Use the test page to confirm data flow end-to-end
