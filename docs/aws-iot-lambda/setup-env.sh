#!/bin/bash
# Set Lambda environment variables from GitHub Secrets
# Usage: ./setup-env.sh

set -e

FUNCTION_NAME="aws-iot-mqtt-proxy"
REGION="${AWS_REGION:-ap-south-1}"

echo "🔐 Setting Lambda environment variables from GitHub Secrets..."

# Get secrets
ACCESS_KEY=$(gh secret get AWS_ACCESS_KEY_ID 2>/dev/null || echo "")
SECRET_KEY=$(gh secret get AWS_SECRET_ACCESS_KEY 2>/dev/null || echo "")
IOT_ENDPOINT=$(gh secret get AWS_IOT_ENDPOINT 2>/dev/null || echo "a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com")

if [ -z "$ACCESS_KEY" ] || [ -z "$SECRET_KEY" ]; then
    echo "❌ Error: Could not retrieve GitHub secrets"
    echo "Make sure you're authenticated: gh auth login"
    exit 1
fi

echo "✅ Retrieved secrets from GitHub"
echo "📝 Updating Lambda function configuration..."

aws lambda update-function-configuration \
  --function-name $FUNCTION_NAME \
  --region $REGION \
  --environment "Variables={
    IOT_ACCESS_KEY_ID=$ACCESS_KEY,
    IOT_SECRET_ACCESS_KEY=$SECRET_KEY,
    AWS_IOT_ENDPOINT=$IOT_ENDPOINT
  }" > /dev/null

echo "✅ Environment variables updated!"
echo ""
echo "🧪 Testing Lambda..."
sleep 2

curl -X POST https://2ushw6qnzf.execute-api.ap-south-1.amazonaws.com/prod/connect \
  -H "Content-Type: application/json" \
  -d '{
    "endpoint": "a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com",
    "region": "ap-south-1",
    "device": "SOMNUS_F09E9E3263A4",
    "topic": "device/telemetry/SOMNUS_F09E9E3263A4"
  }' | python3 -m json.tool || echo "Test failed - check logs"
