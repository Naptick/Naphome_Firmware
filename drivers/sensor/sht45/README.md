# SHT45 Temperature & Humidity Sensor Driver

High-precision temperature and humidity sensor driver for Sensirion SHT45.

## Features

- ✅ I2C communication with ESP-IDF I2C master driver
- ✅ CRC-8 data validation for reliability
- ✅ High precision measurements (±0.1°C, ±1% RH typical)
- ✅ Temperature range: -40°C to +125°C
- ✅ Humidity range: 0% to 100% RH
- ✅ Comprehensive error handling
- ✅ Clean, testable driver interface

## Datasheet

- ✅ Datasheet available: [datasheet.pdf](./datasheet.pdf)

## Driver Files

- `include/sht45.h` - Public API
- `src/sht45.c` - Implementation with CRC validation
- `test/test_sht45.c` - Unit tests

## Usage Example

```c
#include "sht45.h"
#include "driver/i2c_master.h"

// Initialize I2C bus (do this once)
i2c_master_bus_config_t bus_config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = GPIO_NUM_21,
    .scl_io_num = GPIO_NUM_22,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
};
i2c_master_bus_handle_t bus_handle;
i2c_new_master_bus(&bus_config, &bus_handle);

// Initialize SHT45 sensor
sht45_handle_t sensor;
if (!sht45_init(&sensor, bus_handle, 0x44)) {
    ESP_LOGE(TAG, "Failed to initialize SHT45");
    return;
}

// Read temperature and humidity
sht45_data_t data;
if (sht45_read(&sensor, &data)) {
    ESP_LOGI(TAG, "Temperature: %.2f°C", data.temperature_c);
    ESP_LOGI(TAG, "Humidity: %.2f%%", data.humidity_rh);
}

// Or read individually
float temp;
if (sht45_read_temperature(&sensor, &temp)) {
    ESP_LOGI(TAG, "Temperature: %.2f°C", temp);
}

// Cleanup when done
sht45_deinit(&sensor);
```

## API Documentation

See [sht45.h](include/sht45.h) for complete API documentation.

### Key Functions

- `sht45_init()` - Initialize sensor with I2C bus
- `sht45_deinit()` - Cleanup and release resources
- `sht45_read()` - Read both temperature and humidity
- `sht45_read_temperature()` - Read temperature only
- `sht45_read_humidity()` - Read humidity only
- `sht45_is_initialized()` - Check initialization status

## Testing

Run unit tests:
```bash
idf.py test -E sht45
```

Tests include:
- Data structure validation
- Parameter validation
- Conversion formula verification
- Boundary condition testing
- Error handling

## Implementation Details

- **I2C Speed**: 100 kHz (standard mode)
- **I2C Address**: 0x44 (default)
- **Measurement Time**: ~15ms for high precision mode
- **CRC Validation**: CRC-8 with polynomial 0x31 (x^8 + x^5 + x^4 + 1)
- **Data Format**: 6 bytes (temp_msb, temp_lsb, temp_crc, hum_msb, hum_lsb, hum_crc)
