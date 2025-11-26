# Reboot and Verification Script

Python script to reboot the ESP32-S3 device, monitor the boot process, and verify all systems are properly initialized.

## Features

- **Automatic device reset** via RTS/DTR
- **Boot sequence monitoring** with timeout
- **Initialization verification** for:
  - Bootloader
  - Wi-Fi connection and IP address
  - LED controller
  - Audio system
  - Voice pipeline
  - OpenAI Realtime API
  - WebSocket connection
  - Session creation
  - WakeNet
  - Optional services (Spotify, AWS IoT)
- **Error and warning tracking**
- **Color-coded output** with status indicators
- **Timing information** (boot time, ready time)
- **Verbose mode** for detailed log output

## Usage

### Basic Usage

```bash
# Reboot device and verify initialization
python3 scripts/reboot_and_verify.py

# Monitor without resetting
python3 scripts/reboot_and_verify.py --no-reset

# Verbose mode (show all logs)
python3 scripts/reboot_and_verify.py --verbose

# Specify serial port
python3 scripts/reboot_and_verify.py --port /dev/cu.usbserial-110

# Different reset method
python3 scripts/reboot_and_verify.py --reset dtr

# Longer timeout
python3 scripts/reboot_and_verify.py --timeout 90
```

### Command Line Options

```
--port, -p PORT       Serial port (default: auto-detect)
--baud, -b BAUD       Baud rate (default: 115200)
--reset, -r METHOD    Reset method: rts, dtr, or both (default: rts)
--timeout, -t SECONDS Monitoring timeout in seconds (default: 60)
--no-reset            Skip device reset, just monitor
--verbose, -v         Show all logs (verbose mode)
--help, -h            Show help message
```

## What It Detects

### Core Systems
- ✓ Bootloader (ESP-IDF version, 2nd stage bootloader)
- ✓ Wi-Fi connection
- ✓ IP address assignment
- ✓ LED controller initialization
- ✓ Audio system initialization

### Voice Pipeline
- ✓ Voice pipeline startup
- ✓ OpenAI Realtime API started
- ✓ WebSocket connected to OpenAI
- ✓ Session created
- ✓ WakeNet enabled

### Optional Services
- ○ Spotify initialization
- ○ AWS IoT connection

### Issues
- ✗ Errors (logged and counted)
- ⚠ Warnings (logged and counted)

## Example Output

```
================================================================================
ESP32-S3 Reboot and Verification Test
================================================================================

Connected to /dev/cu.usbserial-110 at 115200 baud
Resetting device via RTS...
Reset complete, waiting for boot...
Monitoring boot process (timeout: 60.0s)...
================================================================================

[2.1s] Bootloader detected
[5.3s] Wi-Fi connected
[8.7s] IP address: 192.168.86.32
[9.1s] LED controller initialized
[9.5s] Audio system initialized
[12.3s] Voice pipeline started
[15.8s] OpenAI Realtime API started
[16.2s] WebSocket connected
[16.5s] Session created: sess_abc123...
[18.1s] Assistant ready!

================================================================================
Initialization Summary
================================================================================

Core Systems:
  ✓ Bootloader: Yes
  ✓ Wi-Fi: Yes
  ✓ IP Address: 192.168.86.32
  ✓ LED Controller: Yes
  ✓ Audio System: Yes

Voice Pipeline:
  ✓ Voice Pipeline: Yes
  ✓ OpenAI Realtime: Yes
  ✓ WebSocket Connected: Yes
  ✓ Session Created: Yes
  ✓ WakeNet: Yes

Timing:
  Boot detected: 2.1s
  Assistant ready: 18.1s
  Total boot time: 18.1s

================================================================================
✓ Device fully initialized and ready!
================================================================================
```

## Troubleshooting

### No logs detected
- Check serial port connection
- Try different reset method: `--reset both`
- Use `--verbose` to see all logs
- Check if device is already running: `--no-reset`

### Device not resetting
- Try `--reset dtr` or `--reset both`
- Manually power cycle the device
- Check USB cable connection

### Initialization incomplete
- Check for errors in the summary
- Review verbose logs for failure points
- Verify configuration (Wi-Fi credentials, API keys, etc.)
- Check if all required components are enabled in menuconfig