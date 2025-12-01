# AWS IoT Connection Validation Guide

This guide helps you validate that your device is properly connected to AWS IoT and publishing sensor data.

## Quick Validation

Run the validation script:

```bash
cd scripts
./validate_aws_iot_simple.sh SOMNUS_F09E9E3263A4
```

Or use Python script:

```bash
cd scripts
python3 validate_aws_iot_connection.py --device-id SOMNUS_F09E9E3263A4 --check-shadow --check-policy --list-devices
```

## Validation Checklist

### ✅ Step 1: Device Registration

**Check:** Device is registered in AWS IoT

```bash
aws iot describe-thing --thing-name SOMNUS_F09E9E3263A4 --region ap-south-1
```

**Expected:** Returns device information

**If fails:** Device needs to be registered in AWS IoT Console

### ✅ Step 2: Certificates Attached

**Check:** Device has certificates attached

```bash
aws iot list-thing-principals --thing-name SOMNUS_F09E9E3263A4 --region ap-south-1
```

**Expected:** Returns at least one certificate ARN

**If fails:** Attach certificate to device in AWS IoT Console

### ✅ Step 3: IoT Policy Permissions

**Check:** Certificate has policy allowing publish/subscribe

The policy should allow:
- `iot:Publish` on `device/telemetry/*`
- `iot:Subscribe` on `device/telemetry/*`
- `iot:Connect` with client ID matching device ID

**Check policy:**
```bash
# Get certificate ID from list-thing-principals
CERT_ID="your-cert-id"
aws iot list-principal-policies --principal "arn:aws:iot:ap-south-1:ACCOUNT:cert/${CERT_ID}" --region ap-south-1
```

### ✅ Step 4: Device Connection Status

**Check:** Device has connected (has shadow)

```bash
aws iot-data get-thing-shadow \
  --thing-name SOMNUS_F09E9E3263A4 \
  --region ap-south-1 \
  --endpoint-url https://a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com
```

**Expected:** Returns shadow document (even if empty)

**If fails:** Device hasn't connected yet. Check:
- Device is powered on
- WiFi is connected
- Device firmware is running
- MQTT connection is established

### ✅ Step 5: Live Data Validation

**Option A: AWS IoT Test Client (Recommended)**

1. Go to: https://console.aws.amazon.com/iot/home?region=ap-south-1#/test
2. Click "Subscribe to a topic"
3. Enter topic: `device/telemetry/SOMNUS_F09E9E3263A4`
4. Click "Subscribe"
5. Wait for messages (should arrive every ~1 second)

**Expected:** See JSON messages with sensor data

**Option B: Web Test Page**

1. Open: https://naptick.github.io/Naphome_Firmware/aws-iot-test.html
2. Select device: `SOMNUS_F09E9E3263A4`
3. Click "Connect"
4. Should see sensor cards appearing within 1-2 seconds

**Expected:** Sensor cards with values, message count increasing

**Option C: Playwright Test**

```bash
cd scripts
node test-aws-iot-page.js
```

**Expected:** Test shows messages received and sensor cards displayed

## Troubleshooting

### Device Not Connecting

**Symptoms:**
- No device shadow
- No messages received
- Connection status shows "Disconnected"

**Possible causes:**

1. **WiFi not connected**
   - Check device logs for WiFi connection status
   - Verify WiFi credentials are correct

2. **AWS IoT endpoint incorrect**
   - Verify endpoint: `a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com`
   - Check region matches: `ap-south-1`

3. **Certificates invalid or expired**
   - Check certificate expiration date
   - Verify certificates are correctly embedded in firmware
   - Check certificate matches device ID

4. **IoT Policy missing or incorrect**
   - Verify policy allows `iot:Connect` with device ID
   - Verify policy allows `iot:Publish` on telemetry topic
   - Check policy is attached to certificate

5. **Firmware not running**
   - Check device logs
   - Verify `somnus_mqtt_start()` is called
   - Check for MQTT connection errors

### Messages Not Received

**Symptoms:**
- Device shows connected
- But no telemetry messages received

**Possible causes:**

1. **Sensor manager not started**
   - Check logs for: `[sensor_integration] Sensor integration started`
   - Verify `sensor_integration_start()` is called in main app

2. **Publishing errors**
   - Check logs for: `[sensor_manager] Telemetry publish failed`
   - Verify MQTT client is connected when publishing

3. **Wrong topic**
   - Verify topic: `device/telemetry/{DEVICE_ID}`
   - Device ID must match exactly (case-sensitive)

4. **Subscription on wrong topic**
   - Verify subscribing to: `device/telemetry/SOMNUS_F09E9E3263A4`
   - Check for typos in device ID

### Data Format Issues

**Symptoms:**
- Messages received but web page doesn't display data

**Check payload format:**

Expected structure:
```json
{
  "deviceId": "SOMNUS_F09E9E3263A4",
  "timestamp_ms": 1234567890,
  "sht45": {
    "temperature_c": 22.5,
    "humidity_rh": 50.0
  },
  "sgp40": {
    "voc_index": 120,
    "voc_ticks": 1200
  },
  ...
}
```

**Verify:**
- Sensor names match: `sht45`, `sgp40`, `scd40`, `vcnl4040`, `ec10`
- Field names match expected (see `EXPECTED_SENSORS` in test page)
- Values are numbers (not strings)

## Validation Scripts

### Simple Bash Script

```bash
./scripts/validate_aws_iot_simple.sh [DEVICE_ID]
```

Checks:
- AWS credentials
- Device registration
- Certificates attached
- Device shadow

### Python Script

```bash
python3 scripts/validate_aws_iot_connection.py \
  --device-id SOMNUS_F09E9E3263A4 \
  --check-shadow \
  --check-policy \
  --list-devices
```

Checks:
- All of the above
- IoT policies
- Can subscribe to MQTT (with proper setup)

### Playwright Test

```bash
node scripts/test-aws-iot-page.js
```

Tests:
- Web page functionality
- MQTT connection via proxy
- Message reception
- Data display

## Expected Behavior

### When Device is Online and Publishing

1. **Device logs show:**
   ```
   [SOMNUS_MQTT] Connected to AWS IoT as SOMNUS_F09E9E3263A4
   [sensor_integration] Sensor integration started (1Hz sampling)
   [sensor_manager] (publishing every second)
   ```

2. **AWS IoT Test Client shows:**
   - Messages arriving every ~1 second
   - JSON payload with sensor data

3. **Web test page shows:**
   - Connection status: "Connected"
   - Message count increasing
   - Sensor cards with values
   - Charts updating

4. **Validation script shows:**
   - ✅ Device registered
   - ✅ Certificates attached
   - ✅ Device shadow exists
   - ✅ Messages received (if subscribing)

## Next Steps

If validation fails:

1. **Check device logs** - Look for connection errors
2. **Verify AWS IoT setup** - Certificates, policies, endpoint
3. **Test with AWS IoT Test Client** - Direct MQTT subscription
4. **Check firmware** - Ensure sensor_manager is started
5. **Verify network** - Device can reach AWS IoT endpoint

For detailed troubleshooting, see:
- `docs/SENSOR_DATA_PUBLISHING.md` - How publishing works
- `docs/AWS_IOT_TEST_README.md` - Web test page setup
