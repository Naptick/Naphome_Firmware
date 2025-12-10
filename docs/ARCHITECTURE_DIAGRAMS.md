# Naphome System Architecture Diagrams

This document contains SysML-style architecture diagrams for the Naphome system, created using PlantUML and rendered with Kroki.

## View Online

**🌐 [View Interactive Diagrams on GitHub Pages](architecture.html)**

The diagrams are available as an interactive HTML page that automatically renders using Kroki's API.

## Diagrams

### 1. Internal Block Diagram (IBD)
The IBD shows the internal structure of the Naphome device and its connections to external systems:
- **Naphome Device** components (Voice Pipeline, BLE Service, MQTT Client, etc.)
- **External systems** (Mobile App, AWS IoT, Naptick Server, Spotify API)
- **Data flows** and communication paths

### 2. Sequence Diagram
The sequence diagram illustrates communication flows between system components:
- Initialization and WiFi setup
- Voice command processing
- Sensor data publishing
- Remote commands via AWS IoT
- Mobile app device control

## Viewing the Diagrams

### Option 1: Using Kroki Online
1. Copy the PlantUML code from `naphome_architecture_diagrams.puml`
2. Go to [https://kroki.io/](https://kroki.io/)
3. Select "PlantUML" as the diagram type
4. Paste the code and click "Render"

### Option 2: Using Kroki API in Markdown
You can embed the diagrams in Markdown using Kroki's API:

```markdown
![IBD Diagram](https://kroki.io/plantuml/svg/eNp...)
![Sequence Diagram](https://kroki.io/plantuml/svg/eNp...)
```

### Option 3: Local Rendering
If you have PlantUML installed locally:

```bash
# Install PlantUML (requires Java)
# macOS: brew install plantuml
# Or download from: http://plantuml.com/download

# Render to PNG
plantuml -tpng docs/naphome_architecture_diagrams.puml

# Render to SVG
plantuml -tsvg docs/naphome_architecture_diagrams.puml
```

### Option 4: VS Code Extension
Install the "PlantUML" extension in VS Code to preview diagrams directly in the editor.

## System Components

### Naphome Device Internal Components
- **Voice Pipeline**: Processes audio input and detects wake words
- **Wake Word Service**: Detects "Hey Naptick" wake word
- **BLE Service**: Provides Bluetooth Low Energy interface for mobile app
- **MQTT Client**: Connects to AWS IoT Core for remote commands and telemetry
- **Spotify Client**: Handles music playback via Spotify API
- **Sensor Manager**: Collects data from environmental sensors
- **Audio Player**: Manages audio output
- **LED Controller**: Controls device LED indicators
- **Intent Router**: Routes commands to appropriate handlers
- **Device State**: Manages device state and status

### External Systems
- **Mobile App**: iOS/Android app for device setup and control via BLE
- **AWS IoT Core**: MQTT broker for device commands and telemetry
- **Naptick Server**: HTTP endpoint for sensor data collection
- **Spotify API**: Music streaming service

## Communication Protocols

### BLE (Bluetooth Low Energy)
- **Purpose**: Initial WiFi setup and local device control
- **Service UUID**: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- **Characteristics**:
  - TX (notify): Device → Mobile App
  - RX (write): Mobile App → Device

### MQTT (AWS IoT)
- **Protocol**: MQTT over TLS (port 8883)
- **Topics**:
  - Commands: `somnus/{device_id}/commands`
  - Telemetry: `somnus/{device_id}/telemetry`
  - Logs: `somnus/{device_id}/logs`
- **QoS**: QOS1 (at least once delivery)

### HTTP (Naptick Server)
- **Endpoint**: `https://api-uat.naptick.com/sensor-service/stream`
- **Method**: POST
- **Format**: JSON sensor data

### Spotify API
- **Protocol**: OAuth 2.0 + REST API
- **Operations**: Play, pause, volume control, track selection

## Data Flow Summary

1. **Voice Commands**: User → Voice Pipeline → Intent Router → Action Handler
2. **Remote Commands**: AWS IoT → MQTT Client → Intent Router → Action Handler
3. **Mobile Commands**: Mobile App → BLE → Intent Router → Action Handler
4. **Sensor Data**: Sensors → Sensor Manager → MQTT/AWS IoT & HTTP/Naptick Server
5. **State Updates**: Device State → MQTT Client → AWS IoT

## Updating the Diagrams

To update the diagrams:
1. Edit `docs/naphome_architecture_diagrams.puml`
2. Use PlantUML syntax (see [PlantUML documentation](https://plantuml.com/))
3. Test locally or with Kroki online
4. Update this README if component descriptions change
