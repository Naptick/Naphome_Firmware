#!/bin/bash
# Test script for Lambda function
# Usage: ./test-lambda.sh

API_URL="https://2ushw6qnzf.execute-api.ap-south-1.amazonaws.com/prod/connect"

echo "Testing Lambda via API Gateway..."
echo "URL: $API_URL"
echo ""

curl -X POST "$API_URL" \
  -H "Content-Type: application/json" \
  -d '{
    "endpoint": "a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com",
    "region": "ap-south-1",
    "device": "SOMNUS_F09E9E3263A4",
    "topic": "device/telemetry/SOMNUS_F09E9E3263A4"
  }' | python3 -m json.tool

echo ""
echo ""
echo "To set environment variables, run:"
echo "aws lambda update-function-configuration --function-name aws-iot-mqtt-proxy --region ap-south-1 \\"
echo "  --environment 'Variables={IOT_ACCESS_KEY_ID=YOUR_KEY,IOT_SECRET_ACCESS_KEY=YOUR_SECRET,AWS_IOT_ENDPOINT=a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com}'"
