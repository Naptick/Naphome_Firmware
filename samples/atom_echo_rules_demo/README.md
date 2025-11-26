# Atom Echo Rules Demo

This sample targets the M5Stack Atom S3R seated on the Echo Base. It showcases a rules-driven scene controller that:

- Loads a JSON datafile from SPIFFS and exposes it as a rule store with SHA-256 tracking.
- Accepts full-file rule updates from Somnus (AWS IoT) MQTT and future Matter handlers.
- Drives the Echo Base WS2812 strip for scene feedback.
- Maps the 240×240 ST7789 display to a 10×10 tile grid so each rule can be visualised at a glance.
- Scans the three I²C domains (internal IMU, Atom header, Port A) and identifies connected devices.

## Hardware set-up

The project ships with the pin map from the M5 Atomic Echo Base + Atom S3R schematics. You can override everything in `menuconfig → Atom Echo Rules Demo`, but the baked-in defaults already match the reference boards:

| Function | Atom S3R GPIO | Notes |
| --- | --- | --- |
| LCD backlight (white LED driver) | `GPIO0` | Drives LP5562 “W” channel |
| LCD reset | `GPIO48` | Active high |
| LCD D/C (RS) | `GPIO42` | |
| LCD MOSI | `GPIO21` | Shared SPI bus |
| LCD SCK | `GPIO15` | |
| LCD CS | `GPIO14` | |
| IMU / RGB driver I²C SDA (`SYS_SDA`) | `GPIO38` | BMI270 + LP5562 + codec |
| IMU / RGB driver I²C SCL (`SYS_SCL`) | `GPIO39` | |
| Port (HY2.0-4P) SDA | `GPIO2` | Yellow wire |
| Port (HY2.0-4P) SCL | `GPIO1` | White wire |
| IR LED driver | `GPIO47` | |
| User button | `GPIO41` | |

The WS2812 strip defaults to disabled (`GPIO = -1`, count = 0); enable it only if you wire an external strip.

With those defaults, the 10×10 LCD tile map renders green for enabled rules, red for disabled rules, and yellow when a rule carries rate limits.

## Rule storage

Initial rules live in `spiffs/rules.json` — the file you asked for earlier. On boot the sample mounts the `rules` SPIFFS partition and reads that file into the rule store. Each update recomputes the SHA-256 digest and mirrors the changes to disk. If Somnus MQTT delivers a payload containing a `"rules"` object and optional `"checksum"`, the handler overwrites the file atomically.

## Display + LEDs

`display_matrix.c` wraps ESP-IDF’s `esp_lcd` drivers so you can treat the ST7789 panel as a coarse tile buffer. Every tile is filled with a solid RGB565 colour and flushed immediately. The scene controller module continues to manage the Echo Base WS2812 strip — either by predefined scenes (`warm_dim`) or RGB overrides.

## I²C diagnostics

`i2c_scanner.c` performs a quick address scan on each bus you configure. Known addresses (SHT45, SGP40, VCNL4040, MPU6886, etc.) are annotated in the log to help verify sensor bring-up.

## Next steps

- Hook the Matter bridge so controller writes feed `rule_update_channel_handle_matter()`.
- Tie the Somnus BLE UART into your onboarding experience (the service already starts; you just need to add handlers).
- Replace the simple colour map with richer status (recent trigger, cooldown timers, etc.).

To build:

```bash
cd samples/atom_echo_rules_demo
idf.py set-target esp32s3
idf.py build flash monitor
```

Make sure to update the LCD/I²C pin assignments before flashing; otherwise the display initialiser will skip those subsystems gracefully.
