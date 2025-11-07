# SGP40 VOC Sensor Driver

## Datasheet

- ✅ Datasheet available: [datasheet.pdf](./datasheet.pdf)

## Driver Files

- `include/sgp40.h` - Public API
- `src/sgp40.c` - Implementation
- `test/test_sgp40.c` - Unit tests

## Usage

### Basic Usage

```c
#include "sgp40.h"

// Initialize I2C bus (handled separately)
i2c_master_bus_handle_t i2c_bus;
// ... I2C bus initialization code ...

// Initialize SGP40 sensor
sgp40_handle_t sgp40;
if (sgp40_init(&sgp40, i2c_bus, 0x59)) {
    ESP_LOGI("app", "SGP40 initialized");
    
    // Read VOC data
    sgp40_data_t voc_data;
    if (sgp40_read(&sgp40, &voc_data)) {
        ESP_LOGI("app", "VOC Raw: %d", voc_data.voc_raw);
    }
    
    // Read with temperature/humidity compensation
    if (sgp40_read_compensated(&sgp40, &voc_data, 25.0f, 50.0f)) {
        ESP_LOGI("app", "VOC Raw (compensated): %d", voc_data.voc_raw);
    }
    
    // Cleanup
    sgp40_deinit(&sgp40);
}
```

See driver header file for complete API documentation.

## Testing

Run unit tests:
```bash
idf.py test -E sgp40
```
