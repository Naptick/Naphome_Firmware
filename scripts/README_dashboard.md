# Naptick Voice Assistant Dashboard

A real-time monitoring dashboard for the Naptick voice assistant that displays status indicators and serial logs.

## Features

- **Status Indicators**: Visual status lights for:
  - Wi-Fi connection (Green=Connected, Orange=Connecting, Red=Error)
  - Spotify service (Green=Ready, Orange=Connecting, Red=Error)
  - AWS IoT connection (Green=Connected, Orange=Reconnecting, Red=Error)
  - Wake word detection (Orange when detected)
  - Mute status (Red when muted)
  - Audio playback (Blue when playing)

- **Real-time Log Viewer**: Scrollable log display with color-coded messages:
  - Red: Errors and failures
  - Orange: Wake word detections
  - Green: Successful connections and ready states
  - Normal: Regular log messages

- **Statistics**: Tracks:
  - Wake word events
  - AWS IoT connection/disconnection counts
  - Wi-Fi connection count
  - Error count

## Usage

```bash
# Default port
python3 scripts/monitor_dashboard.py

# Specify port
python3 scripts/monitor_dashboard.py --port /dev/cu.usbserial-110

# Custom baud rate
python3 scripts/monitor_dashboard.py --port /dev/cu.usbserial-110 --baud 115200
```

## Status Color Meanings

### LED Colors on Device → Dashboard Interpretation

- **Green**: Service is connected and ready
- **Orange/Amber**: Service is connecting or reconnecting
- **Cyan**: Wi-Fi is ready/connected (shown as "Connected" in dashboard)
- **Red**: Service has failed or is disconnected
- **Blue**: Audio is currently playing
- **Gray**: Unknown or idle state

## Requirements

- Python 3.6+
- pyserial: `pip install pyserial`
- tkinter (usually included with Python)

## Finding Your Serial Port

On macOS:
```bash
ls -la /dev/cu.* | grep -i usb
```

On Linux:
```bash
ls -la /dev/ttyUSB* /dev/ttyACM*
```

On Windows:
- Check Device Manager → Ports (COM & LPT)
