# Troubleshooting: No Messages Received

## Current Status

✅ **Web Page Connection**: Working  
✅ **MQTT Subscription**: Successful  
✅ **Proxy Endpoint**: Working  
❌ **Messages Received**: 0  

## Root Cause

The web page and MQTT connection are working correctly. The issue is that **the device is not publishing telemetry data** to AWS IoT.

## Diagnostic Steps

### Step 1: Verify Device is Online

Check if the device has connected to AWS IoT:

```bash
# Check device shadow (indicates device has connected)
aws iot-data get-thing-shadow \
  --thing-name SOMNUS_F09E9E3263A4 \
  --region ap-south-1 \
  --endpoint-url https://a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com
```

**Expected**: Returns shadow document (even if empty)  
**If fails**: Device has never connected to AWS IoT

### Step 2: Check Device Logs

On the device, look for these log messages:

**Connection:**
```
[SOMNUS_MQTT] Connected to AWS IoT as SOMNUS_F09E9E3263A4
```

**Sensor Manager:**
```
[sensor_integration] Sensor integration started (1Hz sampling)
[sensor_manager] (should see periodic publish messages)
```

**Publishing:**
```
[sensor_manager] Published telemetry (or similar)
```

**Errors to watch for:**
```
[sensor_manager] Telemetry publish failed (ESP_ERR_INVALID_STATE)
[SOMNUS_MQTT] Failed to start AWS IoT service
```

### Step 3: Verify Sensor Manager is Running

The device should be publishing every 1 second. Check:

1. **Is sensor_integration started?**
   - Look for: `sensor_integration_start()` called in main app
   - Should see: `[sensor_integration] Sensor integration started`

2. **Is sensor_manager started?**
   - Called automatically by `sensor_integration_start()`
   - Should see periodic log messages

3. **Is MQTT connected when publishing?**
   - `somnus_mqtt_publish_telemetry()` requires MQTT to be connected
   - If not connected, publishes will fail silently

### Step 4: Test with AWS IoT Console

1. Go to: https://console.aws.amazon.com/iot/home?region=ap-south-1#/test
2. Click "Subscribe to a topic"
3. Enter: `device/telemetry/SOMNUS_F09E9E3263A4`
4. Click "Subscribe"
5. Wait 30 seconds

**Expected**: See JSON messages arriving every ~1 second  
**If no messages**: Device is definitely not publishing

### Step 5: Check Other Devices

Test with other device IDs to see if any are publishing:

```bash
# List all devices
./scripts/validate_aws_iot_simple.sh SOMNUS_7A356722B383
```

Or test in web page with different device ID.

## Common Issues and Solutions

### Issue 1: Device Not Connected to AWS IoT

**Symptoms:**
- No device shadow
- No connection logs
- MQTT start fails

**Solutions:**
1. Check WiFi connection
2. Verify AWS IoT endpoint is correct: `a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com`
3. Check certificates are valid and not expired
4. Verify IoT policy allows connection

### Issue 2: Sensor Manager Not Started

**Symptoms:**
- Device connected but no publish logs
- No `[sensor_manager]` messages

**Solutions:**
1. Verify `sensor_integration_start()` is called in `naphome_voice_assistant_main.c`
2. Check for initialization errors
3. Verify sensors are registered

### Issue 3: MQTT Not Connected When Publishing

**Symptoms:**
- `Telemetry publish failed (ESP_ERR_INVALID_STATE)`
- Device connects but disconnects quickly

**Solutions:**
1. Check MQTT connection is stable
2. Verify `somnus_mqtt_start()` succeeded before sensor_manager starts
3. Check for connection errors in logs

### Issue 4: Wrong Topic

**Symptoms:**
- Device publishing but messages not received
- Different topic in logs

**Solutions:**
1. Verify topic format: `device/telemetry/{DEVICE_ID}`
2. Check device ID matches exactly (case-sensitive)
3. Verify subscription topic matches

## Verification Checklist

- [ ] Device is powered on
- [ ] WiFi is connected
- [ ] Device logs show AWS IoT connection
- [ ] Device logs show sensor_integration started
- [ ] Device logs show sensor_manager publishing
- [ ] AWS IoT Console test shows messages
- [ ] Device shadow exists
- [ ] Certificates are valid
- [ ] IoT policy allows publish

## Quick Test Commands

```bash
# 1. Check device registration
aws iot describe-thing --thing-name SOMNUS_F09E9E3263A4 --region ap-south-1

# 2. Check device shadow (connection status)
aws iot-data get-thing-shadow \
  --thing-name SOMNUS_F09E9E3263A4 \
  --region ap-south-1 \
  --endpoint-url https://a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com

# 3. Run validation script
./scripts/validate_aws_iot_simple.sh SOMNUS_F09E9E3263A4

# 4. Run Playwright test
node scripts/test-aws-iot-page.js
```

## Expected Behavior When Working

When everything is working correctly:

1. **Device logs show:**
   ```
   [SOMNUS_MQTT] Connected to AWS IoT as SOMNUS_F09E9E3263A4
   [sensor_integration] Sensor integration started (1Hz sampling)
   [sensor_manager] (publishing every second)
   ```

2. **AWS IoT Console shows:**
   - Messages arriving every ~1 second
   - JSON payload with sensor data

3. **Web test page shows:**
   - Connection: "Connected"
   - Messages: Count increasing
   - Sensor cards: Displaying values
   - Charts: Updating

4. **Playwright test shows:**
   - ✅ Messages received: Yes (X messages)
   - ✅ Sensor data received: Yes (X cards)

## Next Steps

1. **Check device logs** - Most important step
2. **Verify device is online** - Check AWS IoT Console
3. **Test with AWS IoT Test Client** - Direct subscription
4. **Check firmware** - Ensure sensor_manager is started
5. **Verify network** - Device can reach AWS IoT endpoint

If device logs show everything is working but still no messages, check:
- AWS IoT policy permissions
- Topic name matches exactly
- Network/firewall blocking messages
- Device time sync (for message ordering)
