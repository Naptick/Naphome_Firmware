# IoT MQTT Sensor Data Publishing Update

## Summary

Updated the IoT MQTT system to send sensor data at a more frequent interval (2 seconds) for app monitoring. This document summarizes the changes made and what needs to be updated in the PDF documentation.

## Changes Made

### 1. Default Sensor Publish Interval
- **Previous**: 10 seconds (10000ms)
- **New**: 2 seconds (2000ms)
- **Reason**: More responsive updates for mobile and web applications monitoring sensor data in real-time

### 2. Configuration
- Updated `CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS` default value from 10000 to 2000
- Updated Kconfig help text to clarify the interval is for app monitoring
- Added configuration to `config/sdkconfig.defaults`

### 3. Documentation Updates
- Updated `docs/aws_iot_interface.md` with detailed sensor data publishing information
- Updated `docs/specifications.md` to reflect the new default interval
- Added comprehensive telemetry payload examples showing all sensor types

## PDF Update Required

The following information should be updated in `docs/somnus_iot_overview.pdf`:

### Section: Sensor Data Publishing
- **Update**: Change default publish interval from 10 seconds to 2 seconds
- **Add**: Explanation that sensor data is automatically published to MQTT for app monitoring
- **Add**: Configuration details:
  - Default: 2000ms (2 seconds)
  - Configurable via `CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS`
  - Range: 1000ms to 600000ms

### Section: Telemetry Payload Format
- **Update**: Include complete example showing all sensor types (SHT45, SGP40, SCD40, VCNL4040, EC10)
- **Add**: Note that telemetry is automatically published at the configured interval

### Section: MQTT Topics
- **Confirm**: Telemetry topic is `device/telemetry/{DEVICE_ID}`
- **Add**: Note that sensor data is published automatically without requiring manual calls

## Technical Details

### Sensor Data Published
The following sensor data is automatically published to MQTT:
- **SHT45**: Temperature (°C), Humidity (%)
- **SGP40**: VOC Index
- **SCD40**: CO₂ (ppm), Temperature (°C), Humidity (%)
- **VCNL4040**: Ambient Light (lux), Proximity
- **EC10**: PM1.0, PM2.5, PM10 (μg/m³)

### Payload Structure
```json
{
  "deviceId": "SOMNUS_112233445566",
  "timestamp_ms": 1721165305123,
  "sht45": { ... },
  "sgp40": { ... },
  "scd40": { ... },
  "vcnl4040": { ... },
  "ec10": { ... }
}
```

### Configuration Location
- Menuconfig: Component config → Sensor Manager → Sensor telemetry publish interval (ms)
- Config file: `config/sdkconfig.defaults`
- Default value: 2000ms

## Testing

To verify the sensor data publishing:
1. Monitor MQTT telemetry topic: `device/telemetry/{DEVICE_ID}`
2. Verify messages are received every 2 seconds (or configured interval)
3. Check payload contains all sensor data fields
4. Confirm timestamp updates with each message

## Related Documentation
- `docs/aws_iot_interface.md` - Complete AWS IoT MQTT interface documentation
- `docs/specifications.md` - System specifications
- `components/sensor_manager/Kconfig` - Configuration options
