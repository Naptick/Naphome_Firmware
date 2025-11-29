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

## Security Notes

⚠️ **Important**: This page requires entering AWS credentials in the browser. For production use:

1. **Use a Backend Proxy**: Create a backend service that handles AWS authentication
2. **Use AWS Cognito**: Implement Cognito Identity Pools for temporary credentials
3. **Restrict IAM Policy**: Use least-privilege IAM policies
4. **HTTPS Only**: Always use HTTPS when entering credentials

## Alternative: Backend Proxy

For production, consider creating a backend proxy that:
- Handles AWS authentication server-side
- Exposes a WebSocket endpoint for the frontend
- Manages MQTT subscriptions and forwards messages

Example architecture:
```
Browser → Backend WebSocket → AWS IoT MQTT → Device
```

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
