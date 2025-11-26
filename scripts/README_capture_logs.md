# Capture Logs Script

The `capture_logs.py` script resets the ESP32-S3 device and captures serial logs to a file for review.

## Usage

### Basic Usage (30 seconds, auto-detect port)
```bash
python3 scripts/capture_logs.py
```

### Capture for 60 seconds
```bash
python3 scripts/capture_logs.py -d 60
```

### Specify port and output file
```bash
python3 scripts/capture_logs.py -p /dev/cu.usbserial-110 -o logs/my_capture.log
```

### Capture without resetting device
```bash
python3 scripts/capture_logs.py --no-reset
```

### Different reset methods
```bash
python3 scripts/capture_logs.py --reset-method rts    # RTS pin (default)
python3 scripts/capture_logs.py --reset-method dtr    # DTR pin
python3 scripts/capture_logs.py --reset-method both   # Both RTS and DTR
```

## Options

- `-p, --port`: Serial port (default: auto-detect)
- `-d, --duration`: Capture duration in seconds (default: 30)
- `-o, --output`: Output file (default: `logs/capture_TIMESTAMP.log`)
- `-b, --baudrate`: Serial baudrate (default: 115200)
- `--no-reset`: Do not reset the device before capturing
- `--reset-method`: Reset method: rts, dtr, or both (default: rts)

## Output

The script will:
1. Reset the device using RTS/DTR pins
2. Wait for boot
3. Capture all serial output
4. Save to a timestamped log file in `logs/` directory
5. Print important lines to console (errors, mic levels, LEDs, wake word, etc.)

## Example for Far Field Demo Debugging

To capture logs and review mic levels and LED behavior:

```bash
# Capture 45 seconds of logs after reset
python3 scripts/capture_logs.py -d 45 -o logs/farfield_debug.log

# Then review the log file
cat logs/farfield_debug.log | grep -i "mic level\|led\|wake word\|es7210"
```

## Requirements

- Python 3
- pyserial: `pip install pyserial`
