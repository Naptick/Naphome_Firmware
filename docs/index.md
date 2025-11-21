# Naphome Firmware Documentation

Welcome to the Naphome firmware documentation space. Use this site to find guides on architecture, integrations, and deployment workflows.

## Quick Start

### IoT & Cloud Integration

- **AWS IoT**: Connect to AWS IoT Core for cloud telemetry and remote control
  - See [AWS IoT & Somnus MQTT Interface](aws_iot_interface.md) for setup, provisioning, and usage
  - Requires ESP-IDF v5.0+, AWS IoT Device SDK v3.0.0+
  
- **Matter**: Expose sensors to Matter fabric for Home automation
  - See [Matter Interface Bridge](matter_interface.md) for sensor registration and esp-matter integration
  - Supports stub mode for testing without Matter stack
  - Requires ESP-IDF v5.0+, esp-matter (optional)

### Architecture

- [Firmware Architecture](../ARCHITECTURE.md) — overall system design, driver patterns, and component structure
- [Firmware Specification](specifications.md) — high-level requirements for sensors, voice, media, and connectivity features

## Integration Guides

### [AWS IoT & Somnus MQTT Interface](aws_iot_interface.md)

Complete guide to AWS IoT Core integration:
- **Architecture**: Three-layer design (SDK → Wrapper → Application)
- **Configuration**: All Kconfig options with defaults and purposes
- **Provisioning**: Certificate management and Thing setup scripts
- **Usage Examples**: Complete code examples with error handling
- **Troubleshooting**: Common issues and solutions
- **Integration**: Working alongside Matter bridge and sensor_manager

### [Matter Interface Bridge](matter_interface.md)

Complete guide to Matter integration:
- **Architecture**: Observer pattern with sensor_manager
- **Configuration**: Kconfig options for stub and esp-matter modes
- **Sensor Registration**: Explicit registration with sensor kinds
- **Stub vs. esp-matter**: When to use each mode
- **Integration**: Working alongside AWS IoT
- **Troubleshooting**: Common issues and debugging

## Additional Documentation

- [Firmware Specification](specifications.md) — high-level requirements for sensors, voice, media, and connectivity features
- [Voice Assistant](naphome_voice_assistant.md) — Korvo-1 voice assistant implementation
- [I2S Farfield Analysis](i2s_farfield_analysis.md) — microphone array analysis

## Publishing

Enable GitHub Pages (`Settings → Pages`) and point it to the `/docs` folder on the `main` branch. Pages rebuilds automatically whenever documentation files change.
