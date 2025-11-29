#!/bin/bash
# Deploy AWS Lambda function for IoT MQTT Proxy
# Requires: AWS CLI configured, jq installed

set -e

FUNCTION_NAME="aws-iot-mqtt-proxy"
REGION="${AWS_REGION:-ap-south-1}"
ROLE_NAME="aws-iot-proxy-lambda-role"

echo "🚀 Deploying AWS Lambda function: $FUNCTION_NAME"

# Check if function exists
if aws lambda get-function --function-name $FUNCTION_NAME --region $REGION &>/dev/null; then
    echo "📦 Function exists, updating..."
    UPDATE_MODE=true
else
    echo "🆕 Creating new function..."
    UPDATE_MODE=false
fi

# Create IAM role if it doesn't exist
if ! aws iam get-role --role-name $ROLE_NAME &>/dev/null; then
    echo "🔐 Creating IAM role..."
    
    # Create trust policy
    cat > /tmp/trust-policy.json <<EOF
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": {
        "Service": "lambda.amazonaws.com"
      },
      "Action": "sts:AssumeRole"
    }
  ]
}
EOF
    
    aws iam create-role \
        --role-name $ROLE_NAME \
        --assume-role-policy-document file:///tmp/trust-policy.json
    
    # Attach basic Lambda execution policy
    aws iam attach-role-policy \
        --role-name $ROLE_NAME \
        --policy-arn arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole
    
    echo "⏳ Waiting for role to be ready..."
    sleep 5
fi

# Get role ARN
ROLE_ARN=$(aws iam get-role --role-name $ROLE_NAME --query 'Role.Arn' --output text)
echo "✅ Using IAM role: $ROLE_ARN"

# Package function
echo "📦 Packaging Lambda function..."
cd "$(dirname "$0")"
zip -q function.zip index.js package.json 2>/dev/null || zip -q function.zip index.js

# Deploy function
if [ "$UPDATE_MODE" = true ]; then
    echo "🔄 Updating function code..."
    aws lambda update-function-code \
        --function-name $FUNCTION_NAME \
        --zip-file fileb://function.zip \
        --region $REGION
    
    echo "🔄 Updating function configuration..."
    aws lambda update-function-configuration \
        --function-name $FUNCTION_NAME \
        --environment "Variables={AWS_ACCESS_KEY_ID=${AWS_ACCESS_KEY_ID},AWS_SECRET_ACCESS_KEY=${AWS_SECRET_ACCESS_KEY},AWS_REGION=${REGION},AWS_IOT_ENDPOINT=${AWS_IOT_ENDPOINT}}" \
        --region $REGION \
        --timeout 30 \
        --memory-size 128
else
    echo "🆕 Creating function..."
    aws lambda create-function \
        --function-name $FUNCTION_NAME \
        --runtime nodejs18.x \
        --role $ROLE_ARN \
        --handler index.handler \
        --zip-file fileb://function.zip \
        --timeout 30 \
        --memory-size 128 \
        --environment "Variables={AWS_ACCESS_KEY_ID=${AWS_ACCESS_KEY_ID},AWS_SECRET_ACCESS_KEY=${AWS_SECRET_ACCESS_KEY},AWS_REGION=${REGION},AWS_IOT_ENDPOINT=${AWS_IOT_ENDPOINT}}" \
        --region $REGION
fi

# Create/update API Gateway
echo "🌐 Setting up API Gateway..."
./setup-api-gateway.sh $FUNCTION_NAME $REGION

echo "✅ Deployment complete!"
echo ""
echo "📋 Next steps:"
echo "1. Note the API Gateway endpoint URL from the output above"
echo "2. Use it in the AWS IoT test page: https://naptick.github.io/Naphome_Firmware/aws-iot-test.html"
echo "3. Select 'Backend Proxy' method and enter the API Gateway URL"
