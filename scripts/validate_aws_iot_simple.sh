#!/bin/bash
# Simple AWS IoT validation script using AWS CLI

set -e

DEVICE_ID="${1:-SOMNUS_F09E9E3263A4}"
REGION="ap-south-1"
ENDPOINT="a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com"
TOPIC="device/telemetry/${DEVICE_ID}"

echo "============================================================"
echo "AWS IoT Connection Validation"
echo "============================================================"
echo "Device ID: ${DEVICE_ID}"
echo "Region: ${REGION}"
echo "Topic: ${TOPIC}"
echo "============================================================"
echo ""

# Check AWS CLI is installed
if ! command -v aws &> /dev/null; then
    echo "❌ AWS CLI not found. Install from: https://aws.amazon.com/cli/"
    exit 1
fi

# Check AWS credentials
echo "Step 1: Checking AWS credentials..."
if ! aws sts get-caller-identity &> /dev/null; then
    echo "❌ AWS credentials not configured"
    echo "   Run: aws configure"
    exit 1
fi
echo "✅ AWS credentials configured"
echo ""

# List things
echo "Step 2: Listing IoT devices..."
THINGS=$(aws iot list-things --region ${REGION} --query 'things[?starts_with(thingName, `SOMNUS_`)].thingName' --output text)
if [ -z "$THINGS" ]; then
    echo "⚠️  No Somnus devices found"
else
    echo "✅ Found Somnus devices:"
    for thing in $THINGS; do
        echo "   - ${thing}"
    done
fi
echo ""

# Check if specific device exists
echo "Step 3: Checking device ${DEVICE_ID}..."
if aws iot describe-thing --thing-name "${DEVICE_ID}" --region ${REGION} &> /dev/null; then
    echo "✅ Device ${DEVICE_ID} is registered"
    
    # Check thing principals (certificates)
    PRINCIPALS=$(aws iot list-thing-principals --thing-name "${DEVICE_ID}" --region ${REGION} --query 'principals' --output text)
    if [ -z "$PRINCIPALS" ]; then
        echo "⚠️  No certificates attached to device"
    else
        echo "✅ Device has certificates attached"
    fi
else
    echo "❌ Device ${DEVICE_ID} not found"
fi
echo ""

# Check device shadow
echo "Step 4: Checking device shadow..."
if aws iot-data get-thing-shadow --thing-name "${DEVICE_ID}" --region ${REGION} --endpoint-url "https://${ENDPOINT}" &> /dev/null; then
    echo "✅ Device shadow exists (device has connected at least once)"
    SHADOW=$(aws iot-data get-thing-shadow --thing-name "${DEVICE_ID}" --region ${REGION} --endpoint-url "https://${ENDPOINT}" --query 'payload' --output text)
    echo "   Shadow data: ${SHADOW:0:200}..."
else
    echo "⚠️  No device shadow found (device may not have connected yet)"
fi
echo ""

# Instructions for MQTT testing
echo "============================================================"
echo "Next Steps for MQTT Validation"
echo "============================================================"
echo ""
echo "Option 1: AWS IoT Test Client (Recommended)"
echo "  1. Go to: https://console.aws.amazon.com/iot/home?region=${REGION}#/test"
echo "  2. Subscribe to topic: ${TOPIC}"
echo "  3. Wait for messages (should arrive every ~1 second)"
echo ""
echo "Option 2: Web Test Page"
echo "  1. Open: https://naptick.github.io/Naphome_Firmware/aws-iot-test.html"
echo "  2. Select device: ${DEVICE_ID}"
echo "  3. Click Connect"
echo "  4. Should see sensor data within 1-2 seconds"
echo ""
echo "Option 3: MQTT Client (requires credentials)"
echo "  Use AWS IoT SDK or mqtt client with SigV4 signing"
echo "  Subscribe to: ${TOPIC}"
echo ""
echo "============================================================"
