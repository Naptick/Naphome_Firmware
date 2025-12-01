# Firmware Sensor Streaming Analysis

## Code Flow Analysis

### Initialization Order

1. **Line 753**: `somnus_mqtt_start(NULL)` - Starts MQTT service
   - Connection is **asynchronous** (takes time to establish)
   - Returns immediately, connection happens in background

2. **Line 842**: `sensor_integration_start()` - Starts sensor integration
   - Calls `sensor_manager_start()` (line 160 in sensor_integration.c)
   - Starts publishing task **immediately**
   - Publishes every 1 second (1000ms interval)

### Publishing Flow

```
sensor_manager_task (runs every 1 second)
  ↓
sensor_manager_collect_and_publish()
  ↓
somnus_mqtt_publish_telemetry()
  ↓
Checks: aws_iot_client_is_connected()
  ↓
If connected: Publishes to AWS IoT
If not connected: Returns ESP_ERR_INVALID_STATE (fails silently)
```

### Critical Code Check

In `somnus_mqtt_publish_telemetry()` (line 220-229):

```c
aws_iot_client_t *client = aws_iot_service_get_client();
if (!client || !aws_iot_client_is_connected(client)) {
    return ESP_ERR_INVALID_STATE;  // Fails if not connected
}
```

In `sensor_manager_collect_and_publish()` (line 214-219):

```c
esp_err_t err = somnus_mqtt_publish_telemetry(payload);
if (err != ESP_OK) {
    ESP_LOGW(SENSOR_MANAGER_TAG,
             "Telemetry publish failed (%s)",
             esp_err_to_name(err));
}
```

## Potential Issues

### Issue 1: Timing Race Condition

**Problem**: `sensor_integration_start()` is called immediately after `somnus_mqtt_start()`, but MQTT connection is asynchronous.

**Result**: 
- Sensor manager starts publishing before MQTT is connected
- All publishes fail with `ESP_ERR_INVALID_STATE`
- Only logs warning, doesn't retry

**Evidence**: Device shadow exists (device connected at some point), but no messages received now.

### Issue 2: No Retry Logic

**Problem**: If publish fails, sensor_manager doesn't retry. It just logs a warning and continues.

**Result**: If MQTT disconnects temporarily, publishing stops until next connection.

### Issue 3: No Connection Status Check

**Problem**: Sensor manager doesn't check if MQTT is connected before attempting to publish.

**Result**: Wastes CPU cycles trying to publish when not connected.

## Is Firmware Streaming?

### Code Analysis: ✅ YES (when MQTT is connected)

The firmware **IS configured to stream** sensor data:
- ✅ Sensor manager task runs every 1 second
- ✅ Collects data from all sensors
- ✅ Publishes via `somnus_mqtt_publish_telemetry()`
- ✅ Topic: `device/telemetry/{DEVICE_ID}`

### Runtime Status: ❓ UNKNOWN

**If MQTT is connected**: ✅ Streaming (every 1 second)  
**If MQTT is NOT connected**: ❌ Not streaming (publishes fail silently)

## How to Verify

### Check Device Logs

Look for these patterns:

**If streaming successfully:**
```
[SOMNUS_MQTT] Connected to AWS IoT as SOMNUS_F09E9E3263A4
[sensor_integration] Sensor integration started (1Hz sampling)
[sensor_manager] (no errors, means publishing is working)
```

**If streaming but failing:**
```
[SOMNUS_MQTT] Connected to AWS IoT as SOMNUS_F09E9E3263A4
[sensor_integration] Sensor integration started (1Hz sampling)
[sensor_manager] Telemetry publish failed (ESP_ERR_INVALID_STATE)
[sensor_manager] Telemetry publish failed (ESP_ERR_INVALID_STATE)
... (repeating)
```

**If not streaming:**
```
[SOMNUS_MQTT] Failed to start AWS IoT service
OR
[sensor_integration] Sensor integration start failed
```

### Check AWS IoT Console

1. Go to: https://console.aws.amazon.com/iot/home?region=ap-south-1#/test
2. Subscribe to: `device/telemetry/SOMNUS_F09E9E3263A4`
3. Wait 30 seconds

**If streaming**: Messages arrive every ~1 second  
**If not streaming**: No messages

## Recommendations

### Fix 1: Wait for MQTT Connection

Add a check to ensure MQTT is connected before starting sensor_manager:

```c
// Wait for MQTT connection before starting sensors
while (!aws_iot_client_is_connected(aws_iot_service_get_client())) {
    vTaskDelay(pdMS_TO_TICKS(100));
}
sensor_integration_start();
```

### Fix 2: Add Retry Logic

Modify `sensor_manager_collect_and_publish()` to retry on failure:

```c
esp_err_t err = somnus_mqtt_publish_telemetry(payload);
if (err != ESP_OK) {
    // Retry once after short delay
    vTaskDelay(pdMS_TO_TICKS(100));
    err = somnus_mqtt_publish_telemetry(payload);
    if (err != ESP_OK) {
        ESP_LOGW(SENSOR_MANAGER_TAG, "Telemetry publish failed (%s)", esp_err_to_name(err));
    }
}
```

### Fix 3: Check Connection Before Publishing

Add connection check in sensor_manager:

```c
aws_iot_client_t *client = aws_iot_service_get_client();
if (!client || !aws_iot_client_is_connected(client)) {
    // Skip publishing if not connected
    return;
}
esp_err_t err = somnus_mqtt_publish_telemetry(payload);
```

## Summary

**Firmware Code**: ✅ Configured to stream sensor data every 1 second

**Actual Streaming**: ❓ Depends on MQTT connection status

**Most Likely Issue**: Sensor manager starts publishing before MQTT connection is established, causing initial publishes to fail. Once MQTT connects, streaming should work, but if connection drops, streaming stops.

**To Verify**: Check device logs for publish errors or use AWS IoT Console to see if messages are arriving.
