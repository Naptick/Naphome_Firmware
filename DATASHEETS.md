# Component Datasheets

This document tracks datasheet availability for each driver component.

## Datasheet Status

### Sensors

| Component | Datasheet | Status | Location |
|-----------|-----------|--------|----------|
| SHT45 | ✅ Available | Copied | `drivers/sensor/sht45/datasheet.pdf` |
| OPT3002 | ✅ Available | Downloaded | `drivers/sensor/opt3002/datasheet.pdf` |
| SGP40 | ✅ Available | Copied | `drivers/sensor/sgp40/datasheet.pdf` |
| SPS30 | ✅ Available | Copied | `drivers/sensor/sps30/datasheet.pdf` |
| SCD41 | ⚠️ Manual Download | See note | `drivers/sensor/scd41/DATASHEET_NOTE.txt` |
| BMP581 | ⚠️ Manual Download | See note | `drivers/sensor/bmp581/DATASHEET_NOTE.txt` |

### Audio

| Component | Datasheet | Status | Location |
|-----------|-----------|--------|----------|
| PCM5102 | ✅ Available | Downloaded | `drivers/audio/pcm5102/datasheet.pdf` |
| TPA3118 | ✅ Available | Downloaded | `drivers/audio/tpa3118/datasheet.pdf` |
| ICS-43434 | ⚠️ Manual Download | See note | `drivers/audio/i2s_mic/DATASHEET_NOTE.txt` |
| Speaker (TEBM35C10-4) | ✅ Available | Copied | `drivers/audio/speaker/datasheet.pdf` |

### LEDs

| Component | Datasheet | Status | Location |
|-----------|-----------|--------|----------|
| WS2812B | ✅ Available | Copied | `drivers/led/ws2812b/datasheet.pdf` |

### IR

| Component | Datasheet | Status | Location |
|-----------|-----------|--------|----------|
| TSAL6100 | ✅ Available | Copied | `drivers/ir/ir_tx/datasheet.pdf` |

## Manual Download Instructions

### SCD41
1. Visit: https://www.digikey.com/product-detail/en/1649-SCD41-D-R2TR-ND
2. Click "View Datasheet" link
3. Save to: `drivers/sensor/scd41/datasheet.pdf`

### BMP581
1. Visit: https://www.sparkfun.com/products/20170
2. Click "Documents" tab
3. Download datasheet to: `drivers/sensor/bmp581/datasheet.pdf`

### ICS-43434
1. Visit: https://learn.adafruit.com/adafruit-i2s-mems-microphone-breakout
2. Or register with TDK InvenSense for official datasheet
3. Save to: `drivers/audio/i2s_mic/datasheet.pdf`

## Notes

- All datasheets are stored in their respective driver directories
- Each driver directory contains either `datasheet.pdf` or `DATASHEET_NOTE.txt`
- Datasheets should be referenced during driver development and testing
