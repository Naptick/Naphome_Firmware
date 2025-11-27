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

**Pros:**
- ✅ Secure (certificates on backend)
- ✅ User authentication & device association
- ✅ Scalable
- ✅ No certificates needed in app

**Cons:**
- ❌ Requires backend server
- ❌ Additional latency

**Use Case:** Production deployments

### 2. Direct AWS IoT

**Flow:** `Flutter App → AWS IoT MQTT (with certs) → Device`

**Pros:**
- ✅ Real-time sensor telemetry
- ✅ Direct connection
- ✅ Low latency

**Cons:**
- ❌ Requires certificates in app
- ❌ No user management
- ❌ Certificate distribution complexity

**Use Case:** Development, sensor monitoring

### 3. Matter

**Flow:** `Flutter App → Matter Protocol → Device`

**Pros:**
- ✅ Standard protocol
- ✅ Interoperability
- ✅ Local network

**Cons:**
- ❌ Requires Matter component
- ❌ Limited to local network
- ❌ Setup complexity

**Use Case:** Matter ecosystem integration

### 4. BLE

**Flow:** `Flutter App → BLE GATT → Device`

**Pros:**
- ✅ Direct connection (no network needed)
- ✅ Low power
- ✅ Simple setup
- ✅ Works during onboarding

**Cons:**
- ❌ Limited range (~10m)
- ❌ Lower throughput
- ❌ Requires device proximity

**Use Case:** Development, testing, offline scenarios

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

## Implementation

### Firmware

The BLE component automatically detects device commands by checking for:
1. `"Action"` field (capital A) - Device command
2. Array format - Array of device commands
3. `"action"` field (lowercase) - BLE-specific action

Device commands are routed to the registered `device_command_cb` callback, which should call `somnus_action_handler_process()`.

### Flutter App

The app should:
1. Check `communication_mode` from SharedPreferences
2. Route commands to appropriate service:
   - `backend` → `ApiService.postRequest(ApiUrlConstants.iotPublish, ...)`
   - `aws_iot` → `AwsIotMqttService.publish(...)`
   - `matter` → Matter client (if implemented)
   - `ble` → BLE service (write to RX characteristic)

## Migration Path

1. **Development:** Start with BLE for quick testing
2. **Local Testing:** Use Direct AWS IoT for sensor monitoring
3. **Integration:** Use Backend for full feature testing
4. **Production:** Use Backend exclusively
