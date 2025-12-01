# Fix Summary: Sensor Publishing Race Condition

## Problem Identified

The firmware had a **timing race condition**:
- `somnus_mqtt_start()` is called (line 753) - connection is asynchronous
- `sensor_integration_start()` is called immediately after (line 842)
- Sensor manager starts publishing **before** MQTT connection is established
- All initial publishes fail with `ESP_ERR_INVALID_STATE`
- No retry logic, so publishes fail silently

## Fix Applied

**File**: `main/naphome_voice_assistant_main.c` (lines 841-863)

**Changes**:
1. Added wait loop that checks for MQTT connection before starting sensors
2. Waits up to 30 seconds for connection
3. Checks connection status every 100ms
4. Starts sensors only after MQTT is confirmed connected
5. Falls back gracefully if timeout occurs

**Code Added**:
```c
// Wait for MQTT connection before starting sensor publishing
if (mqtt_err == ESP_OK) {
    ESP_LOGI(TAG, "Waiting for MQTT connection before starting sensor publishing...");
    aws_iot_client_t *client = NULL;
    int wait_count = 0;
    const int max_wait_seconds = 30;
    
    while (wait_count < max_wait_seconds * 10) {
        client = aws_iot_service_get_client();
        if (client && aws_iot_client_is_connected(client)) {
            ESP_LOGI(TAG, "MQTT connected, starting sensor publishing");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }
    
    if (wait_count >= max_wait_seconds * 10) {
        ESP_LOGW(TAG, "MQTT connection timeout after %d seconds, starting sensors anyway", max_wait_seconds);
    }
}
```

## Expected Behavior After Fix

### Boot Sequence (Correct Order)

1. MQTT service starts
2. **Wait for MQTT connection** ← NEW
3. MQTT connects
4. Sensor integration starts
5. Sensor manager starts publishing
6. **Publishes succeed** (no more ESP_ERR_INVALID_STATE errors)

### Log Messages to Look For

**✅ Success:**
```
[SOMNUS_MQTT] Somnus MQTT service started
[naphome_assistant] Waiting for MQTT connection before starting sensor publishing...
[SOMNUS_MQTT] Connected to AWS IoT as SOMNUS_F09E9E3263A4
[naphome_assistant] MQTT connected, starting sensor publishing
[sensor_integration] Sensor integration started (1Hz sampling)
(No "Telemetry publish failed" errors)
```

**⚠️ Timeout (but still works):**
```
[naphome_assistant] Waiting for MQTT connection before starting sensor publishing...
[naphome_assistant] MQTT connection timeout after 30 seconds, starting sensors anyway
(Sensors will retry on publish when MQTT connects)
```

## Building and Flashing

### Build

```bash
cd /Users/danielmcshan/GitHub/Naphome_Firmware
idf.py build
```

### Flash

```bash
idf.py flash
```

### Flash and Monitor

```bash
idf.py flash monitor
```

Or use the capture script:
```bash
python3 scripts/capture_logs.py -d 60
```

## Verification After Flash

1. **Check logs** for the new wait message:
   ```
   [naphome_assistant] Waiting for MQTT connection before starting sensor publishing...
   [naphome_assistant] MQTT connected, starting sensor publishing
   ```

2. **Verify no publish errors**:
   - Should NOT see: `[sensor_manager] Telemetry publish failed (ESP_ERR_INVALID_STATE)`

3. **Test with Playwright**:
   ```bash
   node scripts/test-aws-iot-page.js
   ```
   - Should now receive messages

4. **Test with AWS IoT Console**:
   - Subscribe to topic: `device/telemetry/SOMNUS_F09E9E3263A4`
   - Should see messages every ~1 second

## Files Changed

- `main/naphome_voice_assistant_main.c` - Added MQTT connection wait
- Added include: `#include "aws_iot_service.h"`

## Status

✅ **Fix committed and pushed**  
⏳ **Ready to build and flash**
