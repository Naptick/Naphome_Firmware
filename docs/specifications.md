# Naphome Firmware Specification

This document captures high-level requirements for the Naphome Phase 0.9 firmware. It is intended to guide feature development, validation, and integration with upstream platforms.

## 1. Overview

- **Target hardware:** M5Stack Atom S3R (ESP32-S3), custom carrier with Somnus sensors, audio subsystem, and RGB lighting.
- **Operating environment:** ESP-IDF 5.x, FreeRTOS, C toolchain.
- **Primary roles:**
  - Collect sensor data (environmental, air quality, light, particulate).
  - Surface telemetry to cloud (AWS IoT/Somnus) and local fabrics (Matter).
  - Facilitate BLE onboarding and Somnus mobile integration.
  - Provide audio, voice, and LED control for user feedback and ambient experience.

## 2. Functional Requirements

### 2.1 System Bring-Up
- Initialise NVS, logging, and FreeRTOS scheduler on boot.
- Start Wi-Fi station mode and attempt configured network connection within 15 seconds.
- Support persistent configuration via `sdkconfig.defaults` + menuconfig overrides.

### 2.2 Connectivity
- **Wi-Fi:** Managed by `wifi_manager`; retries connection and emits events on status changes.
- **BLE:** `somnus_ble` exposes Somnus UART service for onboarding, with connect callbacks.
- **MQTT (AWS IoT):**
  - `aws_iot_service` maintains MQTT session with auto reconnect.
  - `somnus_mqtt` publishes telemetry/log payloads and dispatches cloud actions.
- **Matter Bridge:** When enabled, logical sensors appear as Matter endpoints via `matter_bridge`.

### 2.3 Sensors
- `sensor_manager` supports at least eight logical sensors with JSON sample callbacks.
- Sensors provide temperature, humidity, VOC, CO₂, particulate, and ambient light metrics.
- Sensor data automatically published to MQTT at configurable interval (`CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS`; default 2 s for app monitoring).
- Sensor data simultaneously published to MQTT and bridged to Matter observers.

### 2.4 Audio Subsystem
- Audio drivers (PCM5102 DAC, TPA3118 amplifier, speaker, microphones) shall expose APIs for playback and capture.
- `audio_manager` orchestrates playback requests, manages input/output routing, and handles codec configuration.
- Support basic text-to-speech (TTS) playback using locally cached prompts or streaming synthesis once an external provider is integrated.
- Wake word detection engages the speech-to-text (STT) pipeline and LED feedback within 250 ms of detection.

### 2.5 Voice Interface
- Provide configurable wake word detection with adjustable sensitivity.
- After wake word, capture up to 8 seconds of audio, stream to STT service (cloud or on-device model TBD), and obtain transcription.
- Support fallback phrases for offline handling when STT unavailable.
- Text-to-speech response generated locally or via cloud and played through audio subsystem.
- Expose hooks for command routing (e.g., smart home actions, Spotify control).

### 2.6 LED/Lights
- WS2812B driver updates LED strip for status indication (connection states, alerts).
- Animations triggered by events (Wi-Fi connect, wake word detected, Spotify playback, action received) via `task_manager` hooks.
- Expose a palette-driven LED effect engine with preset themes (idle, listening, processing, error).

### 2.7 Cloud Interaction
- Publish telemetry payloads with fields: `deviceId`, `timestamp_ms`, per-sensor JSON objects.
- Publish structured logs on boot, connection events, and notable state changes.
- Consume action payloads with keys `"Action"` or routine lists; dispatch via registered callback.
- Persist action state when required (future expansion with non-volatile storage).

### 2.8 Local Fabric Interaction (Matter)
- When `CONFIG_NAPHOME_MATTER_BRIDGE_ENABLE=y`, register sensors with `matter_bridge`.
- Provide stub logging when esp-matter is disabled; fully update Matter clusters when enabled.
- Maintain endpoint metadata (labels, sensor type) for adoption by controllers.

## 3. Non-Functional Requirements

| Category | Requirement |
| --- | --- |
| Performance | Sensor publish loop shall complete within 50 ms per sensor under nominal load. |
| Reliability | MQTT connection must auto-recover within 30 s of network restoration. |
| Security | Use mutually authenticated TLS with provisioned certificates; refuse placeholder credentials when enabled. |
| Maintainability | Drivers remain isolated with unit tests; components expose minimal public APIs. |
| Observability | Log connection status, subscription actions, and sensor sampling failures at INFO/WARN levels. |

## 4. Interfaces

### 4.1 MQTT Topics
- Telemetry: `device/telemetry/{DEVICE_ID}`
- Logs: `device/receive/uat/{DEVICE_ID}`
- Commands: `device/somnus/{DEVICE_ID}`

### 4.2 Matter Attributes (initial scope)
- Environment sensors map to Temperature Measurement (°C) and Relative Humidity (%).
- Air quality sensors expose VOC index and CO₂ ppm via custom clusters (TBD).
- Light sensors expose Illuminance Measurement lux values.
- Voice activity status and media playback metadata exposed via Matter media clusters (future work).

### 4.3 BLE
- GATT service UUIDs defined in `somnus_profile.h`.
- Provide connect callback for onboarding to supply SSID/password (not yet implemented).

## 5. Configuration Management

- `sdkconfig.defaults` provides baseline values checked into version control.
- Sensitive materials (certificates) stored outside VCS or marked assume-unchanged.
- Matter enablement and esp-matter linkage controlled through Kconfig menu.

## 6. Testing Strategy

- Unit tests located under `test/` mirror driver/component structure.
- Continuous integration should run `idf.py test` and basic `idf.py build`.
- For Matter and AWS IoT features, add integration harnesses (mock Wi-Fi, CLI tests).

## 7. Open Points

- Implement real sensor drivers beyond simulated data in `main/main.c`.
- Finalise Matter attribute mapping and include esp-matter runtime integration.
- Expand action handling to support schedules, firmware updates, and scene control.
- Define OTA update process (ESP-IDF OTA or AWS IoT Jobs).
- Select STT/TTS providers (local vs cloud), licensing compliance, and storage for wake word models.
- Integrate Spotify streaming SDK (librespot/ESP-respot) and manage authentication tokens securely.
- Design LED effect scripting and configuration interface exposed to mobile/cloud clients.

---

Maintainers should update this specification as requirements evolve, especially when new hardware revisions or cloud features are introduced.
