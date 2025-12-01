# Device Log Review Guide

## What to Look For in Device Logs

When reviewing device logs to verify sensor data is being published, look for these key messages:

### 1. MQTT Connection Status

**✅ Good - Connected:**
```
[SOMNUS_MQTT] Connected to AWS IoT as SOMNUS_F09E9E3263A4
[SOMNUS_MQTT] Published Somnus readiness log
```

**❌ Bad - Connection Failed:**
```
[SOMNUS_MQTT] Failed to start AWS IoT service (ESP_ERR_...)
```

### 2. Sensor Integration Status

**✅ Good - Started:**
```
[sensor_integration] Sensor integration initialized successfully
[sensor_integration] Sensor integration started (1Hz sampling)
[sensor_integration] Sensor sampling task started (1Hz)
```

**❌ Bad - Failed:**
```
[sensor_integration] Sensor integration init failed (ESP_ERR_...)
[sensor_integration] Sensor integration start failed (ESP_ERR_...)
```

### 3. MQTT Connection Wait (NEW FIX)

**✅ Good - Waited and Connected:**
```
[naphome_assistant] Waiting for MQTT connection before starting sensor publishing...
[naphome_assistant] MQTT connected, starting sensor publishing
```

**⚠️ Warning - Timeout:**
```
[naphome_assistant] Waiting for MQTT connection before starting sensor publishing...
[naphome_assistant] MQTT connection timeout after 30 seconds, starting sensors anyway (will retry on publish)
```

### 4. Sensor Manager Publishing

**✅ Good - Publishing Successfully:**
```
[sensor_manager] (no errors - means publishing is working)
```

**❌ Bad - Publishing Failed:**
```
[sensor_manager] Telemetry publish failed (ESP_ERR_INVALID_STATE)
[sensor_manager] Telemetry publish failed (ESP_ERR_INVALID_STATE)
... (repeating every second)
```

### 5. Sensor Data Sampling

**✅ Good - Sensors Working:**
```
[sensor_integration] Sensors: T=22.5°C H=50.0% VOC=120 CO2=450ppm Lux=200 Prox=0 PM2.5=15
```

## Expected Log Sequence (After Fix)

When device boots with the fix:

```
1. [naphome_assistant] AWS IoT bridge initialized
2. [SOMNUS_MQTT] Somnus MQTT service started - AWS IoT connection in progress
3. [naphome_assistant] Waiting for MQTT connection before starting sensor publishing...
4. [SOMNUS_MQTT] Connected to AWS IoT as SOMNUS_F09E9E3263A4
5. [naphome_assistant] MQTT connected, starting sensor publishing
6. [sensor_integration] Sensor integration initialized successfully
7. [sensor_integration] Sensor integration started (1Hz sampling)
8. [sensor_integration] Sensor sampling task started (1Hz)
9. [sensor_integration] Sensors: T=22.5°C H=50.0% VOC=120 CO2=450ppm Lux=200 Prox=0 PM2.5=15
10. (No publish errors - means publishing is working)
```

## How to Capture Logs

### Method 1: Using capture_logs.py

```bash
cd /Users/danielmcshan/GitHub/Naphome_Firmware
python3 scripts/capture_logs.py -d 60 -o logs/device_review.log
```

### Method 2: Using monitor_simple.py

```bash
cd /Users/danielmcshan/GitHub/Naphome_Firmware
python3 scripts/monitor_simple.py /dev/cu.usbserial-110
```

### Method 3: Using ESP-IDF Monitor

```bash
idf.py monitor
```

## What the Fix Does

The fix adds a wait loop that:

1. **Waits for MQTT connection** before starting sensor_manager
2. **Checks every 100ms** if MQTT is connected
3. **Times out after 30 seconds** if connection doesn't happen
4. **Starts sensors anyway** after timeout (they'll retry on publish)

This prevents the race condition where sensor_manager tries to publish before MQTT is connected.

## Review Checklist

When reviewing logs, check:

- [ ] MQTT service started successfully
- [ ] "Waiting for MQTT connection" message appears
- [ ] MQTT connected message appears
- [ ] "MQTT connected, starting sensor publishing" message appears
- [ ] Sensor integration started
- [ ] No "Telemetry publish failed" errors
- [ ] Sensor data is being sampled (temperature, humidity, etc.)

If you see "Telemetry publish failed" errors:
- Check if MQTT connection is stable
- Verify the wait loop completed successfully
- Check if connection dropped after initial connect
