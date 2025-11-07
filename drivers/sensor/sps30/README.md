# SPS30 PM2.5 Air Quality Sensor Driver

## Overview

UART driver for Sensirion SPS30 particulate matter sensor with SHDLC protocol support.

## Datasheet

- ✅ Datasheet available: [datasheet.pdf](./datasheet.pdf)

## Features

- SHDLC (Sensirion HDLC) protocol implementation
- UART communication at 115200 baud
- CRC-8 validation for data integrity
- Reads PM1.0, PM2.5, PM4.0, and PM10 values
- Start/stop measurement control
- Comprehensive error handling

## Driver Files

- `include/sps30.h` - Public API
- `src/sps30.c` - Implementation
- `test/test_sps30.c` - Unit tests

## Usage Example

```c
#include "sps30.h"

// Initialize sensor
sps30_handle_t sensor;
sps30_init(&sensor, UART_NUM_1, GPIO_TX, GPIO_RX);

// Start measurement
sps30_start_measurement(&sensor);

// Wait for sensor to stabilize (1 second)
vTaskDelay(pdMS_TO_TICKS(1000));

// Read PM values
sps30_data_t data;
if (sps30_read(&sensor, &data)) {
    printf("PM2.5: %.2f µg/m³\n", data.pm2_5);
    printf("PM10: %.2f µg/m³\n", data.pm10);
}

// Stop measurement
sps30_stop_measurement(&sensor);

// Clean up
sps30_deinit(&sensor);
```

## API Functions

- `sps30_init()` - Initialize UART and sensor
- `sps30_deinit()` - Clean up resources
- `sps30_start_measurement()` - Start fan and measurement
- `sps30_stop_measurement()` - Stop fan and measurement
- `sps30_read()` - Read all PM values
- `sps30_read_pm25()` - Read PM2.5 only
- `sps30_is_initialized()` - Check initialization
- `sps30_is_measuring()` - Check measurement state

## Testing

Run unit tests:
```bash
idf.py test -E sps30
```

## Hardware Connection

- UART: 115200 baud, 8N1
- Connect sensor SEL pin to GND for UART mode
- Connect sensor RX to ESP32 TX
- Connect sensor TX to ESP32 RX
- Power: 5V (4.5V - 5.5V)

## Technical Specifications

- Protocol: SHDLC (Sensirion HDLC)
- Baud Rate: 115200
- Data Format: IEEE 754 float, big-endian
- CRC: CRC-8, polynomial 0x31
- Measurement Range: 0-1000 µg/m³
- Particle Sizes: PM1.0, PM2.5, PM4.0, PM10
