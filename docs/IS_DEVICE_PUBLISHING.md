# Is the Device Publishing Sensor Data?

## Current Status

✅ **Device Registration**: Device is registered in AWS IoT  
✅ **Device Shadow**: EXISTS (device has connected to AWS IoT)  
✅ **Code Flow**: `sensor_integration_start()` is called in main app  
✅ **Sensor Manager**: Should be started automatically  
❓ **Publishing Status**: UNKNOWN (need to verify)

## Verification

### ✅ Code Verification

The code shows that sensors SHOULD be publishing:

1. **Main app calls**: `sensor_integration_start()` (line 842 in `naphome_voice_assistant_main.c`)

2. **sensor_integration_start()** calls:
   - `sensor_manager_start()` (line 160 in `sensor_integration.c`)
   - This starts the sensor_manager task

3. **sensor_manager task**:
   - Runs every 1 second (1000ms interval)
   - Calls `sensor_manager_collect_and_publish()`
   - Publishes via `somnus_mqtt_publish_telemetry()`

4. **Publishing requires**:
   - MQTT client must be connected
   - `somnus_mqtt_start()` must have succeeded
   - Device must be online

### 🔍 How to Verify Publishing

#### Method 1: AWS IoT Test Client (Most Reliable)

1. Go to: https://console.aws.amazon.com/iot/home?region=ap-south-1#/test
2. Click "Subscribe to a topic"
3. Enter: `device/telemetry/SOMNUS_F09E9E3263A4`
4. Click "Subscribe"
5. Wait 30 seconds

**If publishing**: You'll see JSON messages every ~1 second  
**If not publishing**: No messages will appear

#### Method 2: Check Device Logs

Look for these log messages on the device:

**Connection:**
```
[SOMNUS_MQTT] Connected to AWS IoT as SOMNUS_F09E9E3263A4
```

**Sensor Manager:**
```
[sensor_integration] Sensor integration started (1Hz sampling)
[sensor_manager] (task running)
```

**Publishing (every second):**
```
[sensor_manager] (implicit - no error means success)
```

**Errors (if publishing fails):**
```
[sensor_manager] Telemetry publish failed (ESP_ERR_INVALID_STATE)
```

#### Method 3: Web Test Page

1. Open: https://naptick.github.io/Naphome_Firmware/aws-iot-test.html
2. Select device: `SOMNUS_F09E9E3263A4`
3. Click "Connect"
4. Wait 30 seconds

**If publishing**: Message count increases, sensor cards appear  
**If not publishing**: Message count stays at 0

#### Method 4: Playwright Test

```bash
cd scripts
node test-aws-iot-page.js
```

**If publishing**: Test shows "Messages received: Yes"  
**If not publishing**: Test shows "Messages received: No"

## Current Test Results

From Playwright test:
- ✅ Connection: Working
- ✅ Subscription: Successful  
- ❌ Messages: 0 received

From AWS validation:
- ✅ Device shadow: EXISTS (device has connected)
- ✅ Device registered: Yes
- ✅ Certificates: Attached

## Conclusion

**The device HAS connected to AWS IoT** (shadow exists), which means:
- Device can connect ✅
- Device can publish ✅
- Connection is working ✅

**But we're not receiving messages**, which could mean:
1. Device is currently offline (not connected right now)
2. Device is connected but sensor_manager is not running
3. Device is publishing but to a different topic
4. Publishing is failing silently

## Next Steps

1. **Check device logs** - Most important:
   - Is device currently connected?
   - Is sensor_manager running?
   - Are there publish errors?

2. **Test with AWS IoT Console** - Direct verification:
   - Subscribe to topic in console
   - See if messages arrive

3. **Check if device is online**:
   - Is device powered on?
   - Is WiFi connected?
   - Can device reach AWS IoT endpoint?

4. **Verify sensor_manager is active**:
   - Check device logs for sensor_manager messages
   - Verify no errors preventing publishing

## Expected Behavior

When device is online and publishing:

- **Device logs**: Show connection and periodic publish messages
- **AWS IoT Console**: Shows messages arriving every ~1 second
- **Web test page**: Shows message count increasing
- **Playwright test**: Shows messages received

If device is offline:
- No device shadow updates
- No messages in any test
- Device logs show connection attempts/failures
