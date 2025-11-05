# Naphome Phase 0.9 Firmware

ESP32-S3 firmware for Naphome Phase 0.9 with clean architecture, isolated drivers, and comprehensive unit testing.

## Architecture

### Project Structure

```
naphome-firmware/
├── drivers/              # Hardware drivers (isolated, testable)
│   ├── sensor/
│   │   ├── sht45/       # SHT45 temperature/humidity driver
│   │   ├── opt3002/     # OPT3002 ambient light driver
│   │   ├── sgp40/       # SGP40 VOC driver
│   │   ├── sps30/       # SPS30 PM2.5 driver
│   │   ├── scd41/       # SCD41 CO2 driver
│   │   └── bmp581/      # BMP581 barometric pressure driver
│   ├── audio/
│   │   ├── pcm5102/     # PCM5102 I2S DAC driver
│   │   ├── tas5805/     # TAS5805M amplifier driver (legacy)
│   │   └── i2s_mic/     # I2S microphone driver (ICS-43434)
│   ├── led/
│   │   └── ws2812b/     # WS2812B RGB LED driver
│   ├── ir/
│   │   └── ir_tx/       # IR transmitter driver
│   └── power/
│       ├── battery/      # Battery management
│       └── usb_pd/       # USB-C PD controller
├── components/          # Higher-level components (use drivers)
│   ├── sensor_manager/  # Sensor aggregation and management
│   ├── audio_manager/   # Audio system management
│   ├── wifi_manager/    # WiFi connectivity
│   ├── aws_iot/         # AWS IoT Core integration
│   └── task_manager/    # Task scheduling and management
├── test/                # Unit tests (mirror driver structure)
│   ├── drivers/
│   │   ├── sensor/
│   │   ├── audio/
│   │   ├── led/
│   │   └── ...
│   └── components/
├── main/                 # Main application code
├── config/              # Configuration files
│   ├── sdkconfig.defaults
│   └── partitions.csv
└── scripts/             # Build and deployment scripts
```

## Design Principles

1. **Driver Isolation**: Each driver is self-contained with its own directory, header, and implementation
2. **Unit Testing**: Every driver has corresponding unit tests for isolated development
3. **Dependency Injection**: Drivers accept interfaces to allow mocking in tests
4. **Clean Interfaces**: Drivers expose simple, well-defined APIs
5. **No Scripts in Drivers**: Keep drivers pure C/C++ code

## Driver Architecture

Each driver follows this structure:

```
driver_name/
├── include/
│   └── driver_name.h    # Public API
├── src/
│   └── driver_name.c    # Implementation
├── test/
│   ├── test_driver_name.c
│   └── CMakeLists.txt
└── CMakeLists.txt
```

## Testing Framework

- **Unity**: Unit testing framework
- **CMock**: Mocking framework for dependencies
- **ESP-IDF Test Framework**: Integration with ESP-IDF test runner

## Getting Started

### Prerequisites

- ESP-IDF v5.0+
- Python 3.8+
- CMake 3.16+

### Setup

```bash
# Clone repository
git clone https://github.com/Naphome/Naphome_Firmware.git
cd Naphome_Firmware

# Set up ESP-IDF
. $IDF_PATH/export.sh

# Configure project
idf.py menuconfig

# Build
idf.py build

# Flash
idf.py flash

# Monitor
idf.py monitor
```

### Running Tests

```bash
# Run all tests
idf.py test

# Run specific driver tests
idf.py test -E driver_name

# Run with verbose output
idf.py test -v
```

## Development Workflow

1. **Create Driver**: Add driver in `drivers/` directory
2. **Write Tests**: Create unit tests in `test/drivers/`
3. **Develop in Isolation**: Test driver independently
4. **Integrate**: Use driver in components
5. **Integration Tests**: Test full system integration

## Phase 0.9 Hardware

- **MCU**: M5Stack Atom S3R (ESP32-S3)
- **Sensors**: SHT45, OPT3002, SGP40, SPS30, SCD41, BMP581 (via Qwiic/STEMMA)
- **Audio**: PCM5102 I2S DAC + TPA3118 Amplifier + JL7034+ Speaker
- **Microphones**: 2x Adafruit I2S MEMS (ICS-43434)
- **LEDs**: 16x WS2812B RGB LEDs
- **IR**: IR LED transmitter

## License

Proprietary - Naphome/Syzygyx, Inc.
