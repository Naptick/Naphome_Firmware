# AWS IoT Sensor Test Page

This web-based tool allows you to monitor sensor data from Naphome devices connected to AWS IoT Core in real-time, with history graphs for each sensor.

## Features

- **Real-time Monitoring**: Live sensor data updates via MQTT
- **Device Selection**: Dropdown menu to select from known devices or enter custom device ID
- **History Graphs**: Interactive charts showing sensor data over time (last 100 data points)
- **Multiple Sensors**: Supports all Naphome sensors:
  - SHT45 (Temperature, Humidity)
  - SCD40 (CO2, Temperature, Humidity)
  - SGP40 (VOC Index, VOC Ticks)
  - VCNL4040 (Ambient Light, Proximity)
  - EC10 (PM1.0, PM2.5, PM10)

## Requirements

1. **Browser**: Modern browser with WebSocket support
2. **AWS Credentials**: IAM access key and secret key with IoT permissions
3. **AWS IoT Policy**: Policy that allows MQTT subscribe to `device/telemetry/*` topics

## Setup

### Option 1: AWS Cognito Identity Pool Setup (Recommended)

1. **Create Identity Pool:**
   - AWS Console → Cognito → Identity Pools → Create
   - Enable "Enable access to unauthenticated identities"
   - Note the Identity Pool ID (format: `region:uuid`)

2. **Configure IAM Role:**
   - Attach IAM role with IoT permissions:
   ```json
   {
     "Version": "2012-10-17",
     "Statement": [
       {
         "Effect": "Allow",
         "Action": [
           "iot:Connect",
           "iot:Subscribe",
           "iot:Receive"
         ],
         "Resource": [
           "arn:aws:iot:REGION:ACCOUNT:client/*",
           "arn:aws:iot:REGION:ACCOUNT:topicfilter/device/telemetry/*"
         ]
       }
     ]
   }
   ```

3. **Use in Test Page:**
   - Select "AWS Cognito Identity Pool" method
   - Enter Identity Pool ID
   - No credentials needed!

### Option 2: Backend Proxy with GitHub Secrets

1. **Add GitHub Secrets:**
   - Repository Settings → Secrets and variables → Actions
   - Add: `AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`, `AWS_REGION`, `AWS_IOT_ENDPOINT`

2. **Deploy Proxy:**
   - See `aws-iot-proxy-example.js` for implementation
   - Deploy to Heroku, Railway, Render, Vercel, or AWS Lambda
   - Set environment variables from GitHub Secrets

3. **Use in Test Page:**
   - Select "Backend Proxy" method
   - Enter proxy endpoint URL

### Option 3: IAM Access Keys (Testing Only)

### 1. AWS IoT Policy Configuration

Create or update an IAM policy that allows MQTT WebSocket connections:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "iot:Connect",
        "iot:Subscribe",
        "iot:Receive"
      ],
      "Resource": [
        "arn:aws:iot:REGION:ACCOUNT:client/*",
        "arn:aws:iot:REGION:ACCOUNT:topicfilter/device/telemetry/*"
      ]
    }
  ]
}
```

### 2. IAM User Setup

1. Create an IAM user with programmatic access
2. Attach the policy above (or create a custom policy)
3. Save the Access Key ID and Secret Access Key

### 3. Using the Test Page

1. Navigate to: `https://naptick.github.io/Naphome_Firmware/aws-iot-test.html`
2. Enter your AWS IoT endpoint (e.g., `xxxxx-ats.iot.ap-south-1.amazonaws.com`)
3. Enter your AWS region (e.g., `ap-south-1`)
4. Enter your IAM Access Key ID and Secret Access Key
5. Select a device from the dropdown or enter a custom device ID (format: `SOMNUS_XXXXXXXXXXXX`)
6. Click "Connect"
7. Wait for connection and subscription confirmation
8. Sensor data will appear in real-time with history graphs

## Authentication Methods

The page supports three authentication methods:

### 1. AWS Cognito Identity Pool (Recommended) ⭐

**Best for production** - Uses temporary credentials, no long-lived keys.

1. Create a Cognito Identity Pool in AWS Console
2. Configure it to allow unauthenticated access (or use authenticated identities)
3. Attach an IAM role with IoT permissions
4. Enter the Identity Pool ID in the page

**Setup:**
```bash
# In AWS Console:
# 1. Cognito → Identity Pools → Create
# 2. Enable "Enable access to unauthenticated identities"
# 3. Create IAM role with IoT permissions
# 4. Copy Identity Pool ID (format: region:uuid)
```

### 2. IAM Access Keys

**For testing only** - Requires entering credentials in browser.

1. Create IAM user with IoT permissions
2. Generate access key
3. Enter credentials in the page

⚠️ **Security Warning**: Credentials are visible in browser. Use only for testing.

### 3. Backend Proxy (GitHub Secrets) 🔒

**Best for GitHub Pages** - Credentials stored in GitHub Secrets, never exposed to browser.

The proxy server uses GitHub Secrets for AWS authentication and provides signed WebSocket URLs to the client.

**Setup Steps:**

1. **Add GitHub Secrets:**
   - Go to repository Settings → Secrets and variables → Actions
   - Add secrets:
     - `AWS_ACCESS_KEY_ID`
     - `AWS_SECRET_ACCESS_KEY`
     - `AWS_REGION` (optional, defaults to ap-south-1)
     - `AWS_IOT_ENDPOINT` (optional)

2. **Deploy Proxy Server:**
   
   **Option A: Deploy to Heroku/Railway/Render:**
   ```bash
   # Clone and deploy the proxy example
   git clone <repo>
   cd docs
   npm install express aws-sdk mqtt cors
   # Deploy using your platform's CLI
   ```

   **Option B: Use AWS Lambda + API Gateway:**
   - Package the proxy code as a Lambda function
   - Create API Gateway endpoint
   - Configure environment variables from GitHub Secrets

   **Option C: Use Vercel Serverless Functions:**
   - Create `api/iot-proxy.js` in your repo
   - Vercel will automatically deploy as serverless function

3. **Update Proxy URL:**
   - Enter your deployed proxy URL in the test page
   - Example: `https://your-app.herokuapp.com` or `https://your-api.vercel.app/api/iot-proxy`

**Proxy API:**
- `POST /connect` - Returns signed WebSocket URL
  ```json
  {
    "endpoint": "xxx-ats.iot.region.amazonaws.com",
    "region": "ap-south-1",
    "device": "SOMNUS_XXXXXXXXXXXX",
    "topic": "device/telemetry/SOMNUS_XXXXXXXXXXXX"
  }
  ```

See `aws-iot-proxy-example.js` for a complete implementation example.

## Security Notes

⚠️ **Important**: 

1. **Cognito Identity Pools** (Recommended): No credentials in browser, uses temporary tokens
2. **Backend Proxy**: Credentials stored in GitHub Secrets, never exposed
3. **IAM Keys**: Only for testing - credentials visible in browser
4. **HTTPS Only**: Always use HTTPS when entering credentials
5. **Restrict IAM Policy**: Use least-privilege IAM policies

## Troubleshooting

### "Connection error" or "Subscription error"
- Verify AWS credentials are correct
- Check IAM policy allows `iot:Connect`, `iot:Subscribe`, `iot:Receive`
- Verify device ID format is correct (SOMNUS_XXXXXXXXXXXX)
- Check AWS IoT endpoint and region are correct

### "No messages received"
- Verify device is connected to AWS IoT and publishing to `device/telemetry/{DEVICE_ID}`
- Check device is publishing sensor data (not just logs)
- Verify topic subscription succeeded (check browser console)

### Charts not updating
- Check browser console for JavaScript errors
- Verify Chart.js library loaded correctly
- Check that sensor data contains expected fields

## MQTT Topic Format

The page subscribes to: `device/telemetry/{DEVICE_ID}`

Example: `device/telemetry/SOMNUS_F09E9E3263A4`

## Data Format

Expected JSON payload format (matches `mqtt_payload_schema.json`):

```json
{
  "deviceId": "SOMNUS_F09E9E3263A4",
  "timestamp_ms": 1234567890123,
  "sht45": {
    "temperature_c": 23.5,
    "humidity_rh": 45.2,
    "synthetic": false
  },
  "scd40": {
    "co2_ppm": 450,
    "temperature_c": 23.5,
    "humidity_rh": 45.2,
    "synthetic": false
  }
  // ... other sensors
}
```

## Browser Compatibility

- Chrome/Edge: ✅ Full support
- Firefox: ✅ Full support  
- Safari: ✅ Full support
- Mobile browsers: ✅ Supported

## Chart Features

- **Interactive**: Hover to see exact values
- **Time-based X-axis**: Shows data over time
- **Auto-scaling**: Y-axis adjusts to data range
- **Color-coded**: Each sensor has distinct colors
- **Real-time updates**: Charts update as new data arrives
