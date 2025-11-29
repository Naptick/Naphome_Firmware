# Somnus-Device BLE Protocols

This document describes the BLE protocols implemented for Somnus-Device compatibility.

## Overview

The firmware implements a Nordic UART Service (NUS) over BLE that matches the Somnus-Device protocol specification. All communication uses JSON messages over the BLE UART characteristics.

## Service Details

- **Service UUID**: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- **TX Characteristic** (Notify): `6e400003-b5a3-f393-e0a9-e50e24dcca9e`
- **RX Characteristic** (Write): `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- **Device Name**: `rpi-gatt-server`

## Protocol Commands

### 1. WiFi Scan (`SCAN`)

**Request:**
```json
{
  "action": "SCAN"
}
```

**Response:**
- `WIFI_LIST_START` (notification)
- JSON array of WiFi networks (chunked if needed)
- `WIFI_LIST_END` (notification)

**WiFi Network Format:**
```json
[
  {
    "ssid": "NetworkName",
    "mac": "AA:BB:CC:DD:EE:FF",
    "rssi": -45,
    "auth": "WPA2"
  }
]
```

**Status**: ✅ Implemented

### 2. Connect WiFi (`CONNECT_WIFI`)

**Request:**
```json
{
  "action": "CONNECT_WIFI",
  "ssid": "NetworkName",
  "password": "password123",
  "user_token": "user_token_string",
  "is_production": false
}
```

**Response:**
- `Connecting to {ssid}...` (notification)
- `Connected to {ssid}` or `Wi-Fi connection failed` (notification)

**Status**: ✅ Implemented (requires callback handler)

### 3. Read Sensors (`READ_SENSORS`)

**Request:**
```json
{
  "action": "READ_SENSORS"
}
```

**Response:**
- `SENSOR_DATA_START` (notification)
- JSON sensor data (chunked if needed)
- `SENSOR_DATA_END` (notification)

**Sensor Data Format:**
```json
{
  "timestamp_ms": 1234567890,
  "sensors": {
    "sht45": {
      "temperature_c": 23.5,
      "humidity_rh": 45.2,
      "synthetic": false
    },
    "sgp40": {
      "voc_index": 150,
      "voc_ticks": 1500,
      "synthetic": false
    },
    "scd40": {
      "co2_ppm": 420,
      "temperature_c": 23.4,
      "humidity_rh": 45.1,
      "synthetic": false
    },
    "vcnl4040": {
      "ambient_lux": 250.5,
      "proximity": 0,
      "synthetic": false
    },
    "ec10": {
      "pm2_5_ug_m3": 10.5,
      "pm1_0_ug_m3": 5.25,
      "pm10_ug_m3": 15.75,
      "synthetic": false
    }
  }
}
```

**Status**: ✅ Implemented (requires sensor_manager component)

### 4. Device Commands

Device commands use a different format with capital `Action` field or array format.

**Single Action:**
```json
{
  "Action": "LED",
  "Data": {
    "Color": "#FF0000",
    "Brightness": 50
  }
}
```

**Array of Actions:**
```json
[
  {
    "Action": "LED",
    "Data": {"Color": "#00FF00", "Brightness": 30}
  },
  {
    "Action": "SetVolume",
    "Data": {"Volume": 50}
  }
]
```

**Response:**
- `Command executed` or `Command failed: {error}` (notification)

**Status**: ✅ Implemented (requires device command callback handler)

## Error Handling

### Invalid JSON
**Response:** `Bad JSON format`

### Unknown Action
**Response:** `Unknown action`

### Missing Fields
**Response:** `Missing ssid/password/token` (for CONNECT_WIFI)

## Implementation Details

### Message Chunking

Large messages are automatically chunked into 20-byte fragments to remain compatible with BLE MTU limitations. Chunks are sent with a 50ms delay between them.

### Notification Queue

Notifications are queued and processed by the NimBLE host task to ensure thread-safe operation.

### WiFi Initialization

The SCAN action automatically initializes WiFi if not already initialized, but checks for existing netif to avoid crashes.

## Testing

Use the test scripts in `scripts/`:

- `test_somnus_simple.py` - Basic protocol testing
- `test_somnus_protocols.py` - Comprehensive test suite

## Known Issues Fixed

1. ✅ Notification queue initialization - Fixed in commit
2. ✅ WiFi netif creation crash - Fixed by checking for existing netif
3. ✅ Handle-based characteristic access - Using handles like Somnus-Flutter

## Next Steps

1. Implement device command handlers in main application
2. Add WiFi connection callback in main application
3. Test with actual Somnus mobile app
4. Add more device command types as needed
