# ICS-43434 I2S MEMS Microphone Driver

## Datasheet

- ⚠️ See [DATASHEET_NOTE.txt](./DATASHEET_NOTE.txt) for download instructions

## Driver Files

- `include/i2s_mic.h` - Public API
- `src/i2s_mic.c` - Implementation
- `test/test_i2s_mic.c` - Unit tests

## Usage

See driver header file for API documentation.

## Testing

Run unit tests:
```bash
idf.py test -E i2s_mic
```
