# Communication Modes

The firmware and Flutter app support multiple communication modes for development and production use.

## Overview

**Production:** Uses Backend REST API (secure, scalable, user management)

**Development:** Four options available:
1. **Backend** - REST API proxy (same as production)
2. **Direct AWS IoT** - Direct MQTT connection with certificates
3. **Matter** - Matter protocol (if Matter component enabled)
4. **BLE** - Bluetooth Low Energy direct connection

## Communication Mode Configuration

### Firmware Configuration

The firmware can be configured via Kconfig or runtime settings to use different communication modes. The BLE component now supports device commands in addition to WiFi provisioning.

### Flutter App Configuration

The Flutter app can be configured to use different communication modes via:
- SharedPreferences key: `communication_mode`
- Values: `backend`, `aws_iot`, `matter`, `ble`

## Mode Details

### 1. Backend (Production)

**Flow:** `Flutter App → REST API → Backend Server → AWS IoT MQTT → Device`

**Data Support:**
- ✅ **Historical data** - Backend stores sensor history
- ✅ **Real-time telemetry** - Live sensor updates
- ✅ **Time-series queries** - Query historical ranges

**Pros:**
- ✅ Secure (certificates on backend)
- ✅ User authentication & device association
- ✅ Scalable
- ✅ No certificates needed in app
- ✅ Historical data storage and retrieval

**Cons:**
- ❌ Requires backend server
- ❌ Additional latency

**Use Case:** Production deployments, historical analysis

### 2. Direct AWS IoT

**Flow:** `Flutter App → AWS IoT MQTT (with certs) → Device`

**Data Support:**
- ✅ **Historical data** - AWS IoT Core can store messages
- ✅ **Real-time telemetry** - Live sensor updates via MQTT subscription
- ✅ **Time-series queries** - Query AWS IoT Analytics or Timestream

**Pros:**
- ✅ Real-time sensor telemetry
- ✅ Direct connection
- ✅ Low latency
- ✅ Historical data via AWS services

**Cons:**
- ❌ Requires certificates in app
- ❌ No user management
- ❌ Certificate distribution complexity

**Use Case:** Development, sensor monitoring, historical analysis

### 3. Matter

**Flow:** `Flutter App → Matter Protocol → Device`

**Data Support:**
- ✅ **Instantaneous values only** - Current sensor readings
- ❌ **No history** - Matter protocol doesn't store historical data

**Pros:**
- ✅ Standard protocol
- ✅ Interoperability
- ✅ Local network
- ✅ Low latency (local)

**Cons:**
- ❌ Requires Matter component
- ❌ Limited to local network
- ❌ Setup complexity
- ❌ No historical data storage

**Use Case:** Matter ecosystem integration, real-time local control

### 4. BLE

**Flow:** `Flutter App → BLE GATT → Device`

**Data Support:**
- ✅ **Instantaneous values only** - Current sensor readings on request
- ❌ **No history** - BLE doesn't store historical data

**Pros:**
- ✅ Direct connection (no network needed)
- ✅ Low power
- ✅ Simple setup
- ✅ Works during onboarding

**Cons:**
- ❌ Limited range (~10m)
- ❌ Lower throughput
- ❌ Requires device proximity
- ❌ No historical data storage

**Use Case:** Development, testing, offline scenarios, real-time readings

## BLE Device Commands

The BLE component now supports device commands in addition to WiFi provisioning:

**Supported Commands:**
- `LED` - Control LED patterns and colors
- `SongChange` - Play audio files
- `SetVolume` - Adjust audio volume
- `SetLEDIntensity` - Adjust LED brightness
- `Pause` / `Play` - Pause/resume audio and LEDs
- `Speech` - Text-to-speech

**Command Format:**
```json
{
  "Action": "LED",
  "Data": {
    "Pattern": "fade",
    "Intensity": 1,
    "TotalDuration": 120,
    "LEDRange": [0, 40],
    "FromColor": [26, 26, 46],
    "ToColor": [22, 33, 62]
  }
}
```

Or array of actions:
```json
[
  {
    "Action": "LED",
    "Data": {...}
  },
  {
    "Action": "SongChange",
    "Data": {
      "SongName": "song.mp3",
      "Volume": 1,
      "Duration": 900
    }
  }
]
```

**BLE-Specific Actions (lowercase "action"):**
- `SCAN` - WiFi scan
- `CONNECT_WIFI` - WiFi connection
- `READ_SENSORS` - Read current sensor values (instantaneous only)

## Data Access Patterns

### Historical Data (Backend & Direct AWS IoT)

**Backend:**
- Query historical sensor data via REST API endpoints
- Time-range queries: `/api/sensors/history?deviceId=X&start=...&end=...`
- Aggregated data: `/api/sensors/averages?deviceId=X&period=day`

**Direct AWS IoT:**
- Query AWS IoT Analytics or Timestream for historical data
- MQTT subscription for real-time updates
- Can combine with AWS services for time-series analysis

### Instantaneous Values (Matter & BLE)

**Matter:**
- Read current sensor values via Matter attributes
- No historical data available
- Real-time polling or subscription to attribute changes

**BLE:**
- Request current sensor readings via BLE action: `{"action": "READ_SENSORS"}`
- Device responds with current values in JSON format (matches AWS IoT telemetry format)
- No historical data stored or accessible
- Response format:
  ```json
  {
    "timestamp_ms": 1234567890,
    "sensors": {
      "sht45": {"temperature_c": 24.1, "humidity_rh": 48.3, "synthetic": false},
      "sgp40": {"voc_index": 125, "voc_ticks": 1250, "synthetic": false},
      "scd40": {"co2_ppm": 450.0, "temperature_c": 24.0, "humidity_rh": 48.0, "synthetic": false},
      "vcnl4040": {"ambient_lux": 200, "proximity": 0, "synthetic": false},
      "ec10": {"pm2_5_ug_m3": 15.0, "pm1_0_ug_m3": 7.5, "pm10_ug_m3": 22.5, "synthetic": false}
    }
  }
  ```

## Implementation

### Firmware

The BLE component automatically detects device commands by checking for:
1. `"Action"` field (capital A) - Device command
2. Array format - Array of device commands
3. `"action"` field (lowercase) - BLE-specific action

Device commands are routed to the registered `device_command_cb` callback, which should call `somnus_action_handler_process()`.

For sensor data:
- **Backend/AWS IoT:** Device publishes telemetry to MQTT topics (stored for history)
- **Matter:** Device exposes sensor attributes via Matter cluster
- **BLE:** Device responds to sensor read requests with current values

### Flutter App

The app should:
1. Check `communication_mode` from SharedPreferences
2. Route commands to appropriate service:
   - `backend` → `ApiService.postRequest(ApiUrlConstants.iotPublish, ...)`
   - `aws_iot` → `AwsIotMqttService.publish(...)`
   - `matter` → Matter client (if implemented)
   - `ble` → BLE service (write to RX characteristic)

3. For sensor data:
   - **Backend:** Query REST API for historical data + subscribe to real-time updates
   - **Direct AWS IoT:** Query AWS services for history + MQTT subscription for real-time
   - **Matter:** Read current attributes (no history)
   - **BLE:** Request current values via BLE read (no history)

## Migration Path

1. **Development:** Start with BLE for quick testing
2. **Local Testing:** Use Direct AWS IoT for sensor monitoring
3. **Integration:** Use Backend for full feature testing
4. **Production:** Use Backend exclusively
