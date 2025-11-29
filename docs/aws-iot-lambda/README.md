# AWS Lambda IoT Proxy

This Lambda function provides a secure backend proxy for AWS IoT MQTT WebSocket connections, using GitHub Secrets for authentication.

## Architecture

```
Browser → API Gateway → Lambda → AWS IoT (signed WebSocket URL) → Browser → AWS IoT MQTT
```

## Quick Deploy

### Option 1: GitHub Actions (Recommended)

1. **Secrets are already configured** ✅
2. **Push to trigger deployment:**
   ```bash
   git add docs/aws-iot-lambda/
   git commit -m "Add Lambda function"
   git push
   ```
3. **Or manually trigger:**
   - Go to Actions → "Deploy AWS IoT Proxy Lambda" → Run workflow

### Option 2: Manual Deploy

```bash
cd docs/aws-iot-lambda
export AWS_ACCESS_KEY_ID="your-key"
export AWS_SECRET_ACCESS_KEY="your-secret"
export AWS_REGION="ap-south-1"
export AWS_IOT_ENDPOINT="your-endpoint"
./deploy.sh
```

## API Endpoint

After deployment, you'll get an API Gateway URL like:
```
https://xxxxxxxxxx.execute-api.ap-south-1.amazonaws.com/prod/connect
```

## Usage in Test Page

1. Open: https://naptick.github.io/Naphome_Firmware/aws-iot-test.html
2. Select "Backend Proxy" authentication method
3. Enter your API Gateway URL
4. Select device and connect

## API

### POST /connect

Request:
```json
{
  "endpoint": "xxx-ats.iot.region.amazonaws.com",
  "region": "ap-south-1",
  "device": "SOMNUS_XXXXXXXXXXXX",
  "topic": "device/telemetry/SOMNUS_XXXXXXXXXXXX"
}
```

Response:
```json
{
  "websocketUrl": "wss://xxx-ats.iot.region.amazonaws.com/mqtt?X-Amz-Algorithm=...",
  "topic": "device/telemetry/SOMNUS_XXXXXXXXXXXX",
  "device": "SOMNUS_XXXXXXXXXXXX",
  "endpoint": "xxx-ats.iot.region.amazonaws.com",
  "region": "ap-south-1"
}
```

## Cost

- **Lambda**: Free tier includes 1M requests/month
- **API Gateway**: Free tier includes 1M requests/month
- **Total**: Effectively free for most use cases

## Security

- ✅ Credentials stored in GitHub Secrets (encrypted)
- ✅ Never exposed to browser
- ✅ CORS enabled for web access
- ✅ IAM role with least privilege
- ✅ Uses custom environment variable names (IOT_ACCESS_KEY_ID, IOT_SECRET_ACCESS_KEY) to avoid Lambda reserved vars

## Troubleshooting

### Lambda function not found
- Check function name: `aws-iot-mqtt-proxy`
- Verify region matches your AWS_REGION secret

### API Gateway 403
- Check Lambda permissions for API Gateway
- Verify API Gateway deployment completed

### CORS errors
- Verify OPTIONS method is configured
- Check API Gateway CORS settings
