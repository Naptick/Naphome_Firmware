#!/bin/bash
# Setup API Gateway for Lambda function
# Usage: ./setup-api-gateway.sh <function-name> <region>

set -e

FUNCTION_NAME="${1:-aws-iot-mqtt-proxy}"
REGION="${2:-ap-south-1}"
API_NAME="aws-iot-proxy-api"
STAGE_NAME="prod"

echo "🌐 Setting up API Gateway for $FUNCTION_NAME"

# Get Lambda function ARN
LAMBDA_ARN=$(aws lambda get-function --function-name $FUNCTION_NAME --region $REGION --query 'Configuration.FunctionArn' --output text)
LAMBDA_URI="arn:aws:apigateway:${REGION}:lambda:path/2015-03-31/functions/${LAMBDA_ARN}/invocations"

# Check if API exists
API_ID=$(aws apigateway get-rest-apis --region $REGION --query "items[?name=='${API_NAME}'].id" --output text)

if [ -z "$API_ID" ]; then
    echo "🆕 Creating new API Gateway..."
    API_ID=$(aws apigateway create-rest-api \
        --name $API_NAME \
        --description "AWS IoT MQTT Proxy API" \
        --region $REGION \
        --query 'id' \
        --output text)
    
    echo "✅ Created API: $API_ID"
    
    # Get root resource ID
    ROOT_ID=$(aws apigateway get-resources --rest-api-id $API_ID --region $REGION --query 'items[?path==`/`].id' --output text)
    
    # Create /connect resource
    echo "📝 Creating /connect resource..."
    CONNECT_RESOURCE_ID=$(aws apigateway create-resource \
        --rest-api-id $API_ID \
        --parent-id $ROOT_ID \
        --path-part connect \
        --region $REGION \
        --query 'id' \
        --output text)
    
    # Create POST method
    echo "📝 Creating POST method..."
    aws apigateway put-method \
        --rest-api-id $API_ID \
        --resource-id $CONNECT_RESOURCE_ID \
        --http-method POST \
        --authorization-type NONE \
        --region $REGION
    
    # Create OPTIONS method for CORS
    aws apigateway put-method \
        --rest-api-id $API_ID \
        --resource-id $CONNECT_RESOURCE_ID \
        --http-method OPTIONS \
        --authorization-type NONE \
        --region $REGION
    
    # Set up Lambda integration for POST
    echo "🔗 Setting up Lambda integration..."
    aws apigateway put-integration \
        --rest-api-id $API_ID \
        --resource-id $CONNECT_RESOURCE_ID \
        --http-method POST \
        --type AWS_PROXY \
        --integration-http-method POST \
        --uri $LAMBDA_URI \
        --region $REGION
    
    # Set up mock integration for OPTIONS (CORS)
    aws apigateway put-integration \
        --rest-api-id $API_ID \
        --resource-id $CONNECT_RESOURCE_ID \
        --http-method OPTIONS \
        --type MOCK \
        --request-templates '{"application/json":"{\"statusCode\":200}"}' \
        --region $REGION
    
    # Create OPTIONS method response
    aws apigateway put-method-response \
        --rest-api-id $API_ID \
        --resource-id $CONNECT_RESOURCE_ID \
        --http-method OPTIONS \
        --status-code 200 \
        --response-parameters '{"method.response.header.Access-Control-Allow-Origin":true,"method.response.header.Access-Control-Allow-Headers":true,"method.response.header.Access-Control-Allow-Methods":true}' \
        --region $REGION
    
    # Create OPTIONS integration response
    aws apigateway put-integration-response \
        --rest-api-id $API_ID \
        --resource-id $CONNECT_RESOURCE_ID \
        --http-method OPTIONS \
        --status-code 200 \
        --response-parameters '{"method.response.header.Access-Control-Allow-Origin":"'"'"'*'"'"'","method.response.header.Access-Control-Allow-Headers":"'"'"'Content-Type'"'"'","method.response.header.Access-Control-Allow-Methods":"'"'"'POST,OPTIONS'"'"'"}' \
        --region $REGION
    
    # Deploy API
    echo "🚀 Deploying API..."
    aws apigateway create-deployment \
        --rest-api-id $API_ID \
        --stage-name $STAGE_NAME \
        --region $REGION
    
    # Add Lambda permission for API Gateway
    echo "🔐 Adding Lambda permission..."
    aws lambda add-permission \
        --function-name $FUNCTION_NAME \
        --statement-id apigateway-invoke \
        --action lambda:InvokeFunction \
        --principal apigateway.amazonaws.com \
        --source-arn "arn:aws:execute-api:${REGION}:*:${API_ID}/*/*" \
        --region $REGION || echo "Permission may already exist"
    
else
    echo "✅ API Gateway exists: $API_ID"
    
    # Update deployment
    echo "🔄 Updating deployment..."
    aws apigateway create-deployment \
        --rest-api-id $API_ID \
        --stage-name $STAGE_NAME \
        --region $REGION
fi

# Get API endpoint URL
ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
API_URL="https://${API_ID}.execute-api.${REGION}.amazonaws.com/${STAGE_NAME}/connect"

echo ""
echo "✅ API Gateway setup complete!"
echo "📋 API Endpoint URL: $API_URL"
echo ""
echo "Use this URL in the AWS IoT test page:"
echo "https://naptick.github.io/Naphome_Firmware/aws-iot-test.html"
echo ""
echo "Select 'Backend Proxy' method and enter: $API_URL"
