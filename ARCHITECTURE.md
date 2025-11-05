# Naphome Firmware Architecture

## Design Goals

1. **Clean Separation**: Drivers isolated from application logic
2. **Testability**: Every driver has unit tests
3. **Maintainability**: Clear structure, well-documented
4. **Extensibility**: Easy to add new drivers and components

## Directory Structure

```
naphome-firmware/
├── drivers/              # Hardware abstraction layer
│   ├── sensor/          # Sensor drivers (I2C/UART)
│   ├── audio/           # Audio drivers (I2S)
│   ├── led/             # LED drivers (GPIO/PWM)
│   ├── ir/               # IR transmitter driver
│   └── power/            # Power management drivers
├── components/           # Application-level components
│   ├── sensor_manager/  # Aggregates sensor readings
│   ├── audio_manager/    # Audio system control
│   ├── wifi_manager/    # WiFi connectivity
│   ├── aws_iot/         # AWS IoT Core integration
│   └── task_manager/    # Task scheduling
├── test/                # Unit and integration tests
│   └── drivers/         # Driver unit tests
├── main/                 # Application entry point
└── config/              # Configuration files
```

## Driver Architecture

### Driver Interface Pattern

Each driver follows this pattern:

```c
// driver_name.h - Public API
typedef struct {
    // Driver-specific handle data
} driver_name_handle_t;

bool driver_name_init(driver_name_handle_t *handle, ...);
void driver_name_deinit(driver_name_handle_t *handle);
bool driver_name_read(driver_name_handle_t *handle, ...);
```

### Example: SHT45 Driver

```c
// Clean interface
bool sht45_init(sht45_handle_t *handle, i2c_bus_t bus, uint8_t addr);
bool sht45_read(sht45_handle_t *handle, sht45_data_t *data);
```

### Driver Dependencies

- **I2C Drivers**: Depend on `driver/i2c_master`
- **UART Drivers**: Depend on `driver/uart`
- **I2S Drivers**: Depend on `driver/i2s`
- **GPIO Drivers**: Depend on `driver/gpio`

## Testing Strategy

### Unit Tests

Each driver has comprehensive unit tests:

```
drivers/sensor/sht45/
├── include/sht45.h
├── src/sht45.c
└── test/
    ├── test_sht45.c      # Unit tests
    └── CMakeLists.txt
```

### Test Framework

- **Unity**: Unit testing framework (provided by ESP-IDF)
- **CMock**: Mocking framework for I2C/UART operations
- **Test Isolation**: Each driver tested independently

### Running Tests

```bash
# Run all tests
idf.py test

# Run specific driver tests
idf.py test -E sht45

# Run with coverage
idf.py test --coverage
```

## Component Architecture

Components use drivers but don't depend on specific implementations:

```c
// sensor_manager uses sensor drivers
sensor_manager_t *manager = sensor_manager_init();
sensor_manager_add_sensor(manager, &sht45_driver);
sensor_manager_add_sensor(manager, &opt3002_driver);
```

## Development Workflow

1. **Create Driver Stub**
   ```bash
   mkdir -p drivers/sensor/new_sensor/{include,src,test}
   ```

2. **Write Driver Interface** (`include/new_sensor.h`)
   - Define handle structure
   - Define public API functions
   - Document parameters and return values

3. **Write Unit Tests** (`test/test_new_sensor.c`)
   - Test initialization
   - Test read operations
   - Test error handling
   - Test edge cases

4. **Implement Driver** (`src/new_sensor.c`)
   - Implement to pass tests
   - Follow TDD approach

5. **Integrate with Component**
   - Add driver to component
   - Test integration

## Driver Guidelines

1. **No Global State**: All state in handle structure
2. **Error Handling**: Return bool/esp_err_t with clear error codes
3. **Documentation**: Doxygen comments for all public functions
4. **Thread Safety**: Document thread safety requirements
5. **Resource Management**: Proper init/deinit pairs

## Example Driver Template

See `drivers/sensor/sht45/` for a complete example driver with:
- Clean interface
- Full implementation
- Comprehensive unit tests
- CMakeLists.txt configuration
