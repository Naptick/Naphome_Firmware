#!/bin/bash
# Check if device is publishing messages to AWS IoT

DEVICE_ID="${1:-SOMNUS_F09E9E3263A4}"
REGION="ap-south-1"
TOPIC="device/telemetry/${DEVICE_ID}"

echo "============================================================"
echo "Checking if Device is Publishing"
echo "============================================================"
echo "Device ID: ${DEVICE_ID}"
echo "Topic: ${TOPIC}"
echo "============================================================"
echo ""

# Check device shadow (indicates device has connected)
echo "1. Checking device connection status (shadow)..."
SHADOW=$(aws iot-data get-thing-shadow \
  --thing-name "${DEVICE_ID}" \
  --region "${REGION}" \
  --endpoint-url "https://a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com" \
  2>&1)

if echo "$SHADOW" | grep -q "ResourceNotFoundException"; then
    echo "   ❌ No device shadow found"
    echo "   → Device has NOT connected to AWS IoT yet"
    echo "   → Device cannot be publishing if it hasn't connected"
    echo ""
    echo "   💡 Check device:"
    echo "      - Is device powered on?"
    echo "      - Is WiFi connected?"
    echo "      - Check device logs for connection errors"
else
    echo "   ✅ Device shadow exists"
    echo "   → Device HAS connected to AWS IoT at least once"
    echo ""
    # Extract shadow data
    SHADOW_DATA=$(echo "$SHADOW" | grep -o '"payload":{[^}]*}' | head -1)
    if [ -n "$SHADOW_DATA" ]; then
        echo "   Shadow data: ${SHADOW_DATA:0:200}..."
    fi
fi
echo ""

# Check device connectivity (if available in newer AWS CLI)
echo "2. Checking device connectivity..."
# Note: This requires AWS IoT Device Management, may not be available
echo "   (Connectivity check requires device to be online)"
echo ""

# Instructions for manual check
echo "============================================================"
echo "How to Verify Device is Publishing"
echo "============================================================"
echo ""
echo "Method 1: AWS IoT Test Client (Best)"
echo "-------------------------------------"
echo "1. Go to: https://console.aws.amazon.com/iot/home?region=${REGION}#/test"
echo "2. Click 'Subscribe to a topic'"
echo "3. Enter topic: ${TOPIC}"
echo "4. Click 'Subscribe'"
echo "5. Wait 30 seconds"
echo ""
echo "Expected:"
echo "  ✅ If publishing: See JSON messages every ~1 second"
echo "  ❌ If not publishing: No messages appear"
echo ""

echo "Method 2: Check Device Logs"
echo "---------------------------"
echo "On the device, look for:"
echo "  ✅ [SOMNUS_MQTT] Connected to AWS IoT"
echo "  ✅ [sensor_integration] Sensor integration started"
echo "  ✅ [sensor_manager] (publishing messages)"
echo "  ❌ [sensor_manager] Telemetry publish failed"
echo ""

echo "Method 3: Web Test Page"
echo "-----------------------"
echo "1. Open: https://naptick.github.io/Naphome_Firmware/aws-iot-test.html"
echo "2. Select device: ${DEVICE_ID}"
echo "3. Click Connect"
echo "4. Wait 30 seconds"
echo ""
echo "Expected:"
echo "  ✅ If publishing: Message count increases, sensor cards appear"
echo "  ❌ If not publishing: Message count stays at 0"
echo ""

echo "============================================================"
echo "Diagnosis"
echo "============================================================"

if echo "$SHADOW" | grep -q "ResourceNotFoundException"; then
    echo "❌ Device is NOT connected to AWS IoT"
    echo ""
    echo "This means:"
    echo "  - Device cannot be publishing (not connected)"
    echo "  - Need to check why device isn't connecting"
    echo ""
    echo "Common causes:"
    echo "  1. Device is offline (not powered on)"
    echo "  2. WiFi not connected"
    echo "  3. AWS IoT connection failed (check device logs)"
    echo "  4. Certificate/authentication issues"
else
    echo "✅ Device HAS connected to AWS IoT"
    echo ""
    echo "This means:"
    echo "  - Device can publish (connection established)"
    echo "  - But may not be publishing currently"
    echo ""
    echo "To verify publishing:"
    echo "  - Use AWS IoT Test Client (Method 1 above)"
    echo "  - Check device logs for publish messages"
    echo "  - Check device logs for publish errors"
fi

echo ""
echo "============================================================"
