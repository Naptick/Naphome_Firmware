# Korvo LED Demo

This standalone ESP-IDF sample focuses exclusively on the Korvo-1’s built-in WS2812
ring so you can verify the wiring, LED count, and brightness settings without
bringing up the full voice assistant stack.

## What it does

- Drives the first three pixels as “status” indicators (Wi-Fi, Spotify, AWS) and
  cycles through a handful of example states (blinking cyan for Wi-Fi connect,
  amber while services boot, green on success, red on failure, etc.).
- Animates the remaining LEDs with a slow rainbow chase so you can confirm the
  rest of the ring is wired correctly.
- Exposes tunable parameters (GPIO, LED count, brightness, timing) via
  `menuconfig` so you can adapt it to other Korvo revisions or external strips.

## Build & flash

```bash
cd samples/korvo_led_demo
idf.py set-target esp32s3  # only required the first time
idf.py build
idf.py -p /dev/cu.usbserial-10 flash monitor
```

The defaults in `sdkconfig.defaults` target the production Korvo-1 ring:

- `GPIO19` data pin (labelled `WS2812_CTRL` in the schematic)
- `12` pixels
- Brightness scaled to `32/255`

You can tweak these under **`menuconfig → Korvo LED Demo`** if you have a
different hardware revision or want to exercise the LEDs at a different speed.

While the demo runs you should see log messages announcing each pattern change
along with the LED animation described above. Use this sample to confirm the
hardware behaves as expected before integrating the LED feedback into other
applications.
