# Somnus BLE Mock Peripheral

This helper spins up a local BLE peripheral that mirrors the Somnus onboarding
GATT interface so the mobile app can be exercised without ESP32 hardware.
It advertises as `rpi-gatt-server`, exposes the Nordic UART-style RX/TX
characteristics, and implements the `SCAN` / `CONNECT_WIFI` actions expected by
the app.

## Requirements

- macOS with Bluetooth LE hardware capable of peripheral mode (the built-in
  controller works for most Apple Silicon laptops).
- Node.js 16+ (install via Homebrew: `brew install node`).

## Setup

```bash
cd scripts/mock_somnus_ble
npm install
```

## Usage

```bash
# Optional overrides
export SOMNUS_DEVICE_ID=SOMNUS_7A356722B383       # matches provisioned Thing
export SOMNUS_BLE_NAME=rpi-gatt-server           # keep default unless testing variants

npm start
```

While the script is running it will:

- Advertise the Somnus UART service UUID.
- Emit `DeviceId:...` when a client subscribes.
- Respond to `SCAN` with either your local Wi-Fi list (via `airport -s`) or a
  small fallback list.
- Respond to `CONNECT_WIFI` with success (set `SOMNUS_BLE_FAIL_CONNECT=1` to
  force failures).

Press `Ctrl+C` to stop the peripheral.

## Notes

- macOS may prompt for Bluetooth permissions on first launch.
- If the scan fallback is triggered, ensure `/System/.../airport` is available
  (install Xcode CLTs) or provide your own Wi-Fi list handling.
