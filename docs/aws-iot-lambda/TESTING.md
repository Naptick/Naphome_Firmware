# Testing the AWS Lambda IoT Proxy

## Current Status

✅ **Lambda Function**: `aws-iot-mqtt-proxy` (Active)  
✅ **API Gateway**: `aws-iot-proxy-api` (Deployed)  
⚠️ **Environment Variables**: Need to be set manually

## API Gateway URL

```
https://2ushw6qnzf.execute-api.ap-south-1.amazonaws.com/prod/connect
```

## Setup Environment Variables

The Lambda function needs AWS credentials to sign IoT WebSocket URLs. Set them using:

```bash
aws lambda update-function-configuration \
  --function-name aws-iot-mqtt-proxy \
  --region ap-south-1 \
  --environment "Variables={
    IOT_ACCESS_KEY_ID=YOUR_ACCESS_KEY_ID,
    IOT_SECRET_ACCESS_KEY=YOUR_SECRET_ACCESS_KEY,
    AWS_IOT_ENDPOINT=a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com
  }"
```

Or get from GitHub Secrets:

```bash
# Get secrets
ACCESS_KEY=$(gh secret get AWS_ACCESS_KEY_ID)
SECRET_KEY=$(gh secret get AWS_SECRET_ACCESS_KEY)

# Update Lambda
aws lambda update-function-configuration \
  --function-name aws-iot-mqtt-proxy \
  --region ap-south-1 \
  --environment "Variables={
    IOT_ACCESS_KEY_ID=$ACCESS_KEY,
    IOT_SECRET_ACCESS_KEY=$SECRET_KEY,
    AWS_IOT_ENDPOINT=a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com
  }"
```

## Test the Lambda

### Via API Gateway (Recommended)

```bash
curl -X POST https://2ushw6qnzf.execute-api.ap-south-1.amazonaws.com/prod/connect \
  -H "Content-Type: application/json" \
  -d '{
    "endpoint": "a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com",
    "region": "ap-south-1",
    "device": "SOMNUS_F09E9E3263A4",
    "topic": "device/telemetry/SOMNUS_F09E9E3263A4"
  }'
```

Expected response:
```json
{
  "websocketUrl": "wss://a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com/mqtt?X-Amz-Algorithm=...",
  "topic": "device/telemetry/SOMNUS_F09E9E3263A4",
  "device": "SOMNUS_F09E9E3263A4",
  "endpoint": "a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com",
  "region": "ap-south-1"
}
```

### Via Test Script

```bash
cd docs/aws-iot-lambda
./test-lambda.sh
```

## Test in Web App

1. Open: https://naptick.github.io/Naphome_Firmware/aws-iot-test.html
2. Select "Backend Proxy" authentication method
3. The API Gateway URL should be pre-filled
4. Select device and click "Connect"
5. Should receive signed WebSocket URL and connect to MQTT

## Troubleshooting

### 500 Internal Server Error
- Check Lambda environment variables are set
- Check CloudWatch logs: `aws logs tail /aws/lambda/aws-iot-mqtt-proxy --follow`
- Verify credentials have IoT permissions

### CORS Errors
- API Gateway CORS is configured
- Check browser console for specific error

### Connection Failed
- Verify WebSocket URL is valid
- Check device ID is correct
- Verify topic format: `device/telemetry/{DEVICE_ID}`

## View Logs

```bash
# Create log group if needed
aws logs create-log-group --log-group-name /aws/lambda/aws-iot-mqtt-proxy --region ap-south-1

# View logs
aws logs tail /aws/lambda/aws-iot-mqtt-proxy --follow --region ap-south-1
```
