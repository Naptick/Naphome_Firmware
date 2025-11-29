# Firmware Flasher Web Tool

This web-based tool allows you to flash Naphome firmware directly from GitHub Releases to your ESP32-S3 device using your browser.

## Features

- **Browser-based**: No need to install esptool or Python
- **GitHub Releases Integration**: Automatically fetches available firmware releases
- **Web Serial API**: Uses Chrome/Edge's built-in serial port support
- **Progress Tracking**: Real-time progress bar and detailed logs
- **Safe Flashing**: Optional flash erase with confirmation

## Requirements

1. **Browser**: Chrome, Edge, or Opera (Web Serial API support required)
2. **Device**: ESP32-S3 connected via USB
3. **GitHub Releases**: Firmware must be published as a release with `.bin` files

## Usage

1. Navigate to the [Firmware Flasher](firmware-flasher.html) page
   - URL: `https://naptick.github.io/Naphome_Firmware/firmware-flasher.html`
2. Select a firmware release from the dropdown
3. Configure device settings (chip type, baud rate, flash size)
4. Click "Connect to Device" and select your serial port
5. Click "Flash Firmware" and wait for completion

## Troubleshooting

### "Web Serial API not supported"
- Use Chrome, Edge, or Opera browser
- Make sure you're using HTTPS or localhost

### "No port selected"
- Make sure your device is connected via USB
- Try unplugging and replugging the USB cable
- On some systems, you may need to put the device in bootloader mode manually

### "No releases found"
- Make sure releases are published on GitHub
- Check that release assets include `.bin` files
- Verify the repository name is correct in the code

### Flash fails
- Try putting device in bootloader mode (hold BOOT, press RESET, release BOOT)
- Lower the baud rate (try 115200)
- Try erasing flash first using the "Erase Flash" button

## Technical Details

- Uses Web Serial API for device communication
- Downloads firmware from GitHub Releases API
- Flashes to factory partition at address 0x10000
- Compatible with ESP32-S3 partition layout

## Alternative: Using esptool.py

If the web flasher doesn't work, you can use the traditional method:

```bash
# Download firmware from release
wget https://github.com/Naptick/Naphome_Firmware/releases/download/v1.0.0/firmware.bin

# Flash using esptool.py
esptool.py --chip esp32s3 --port /dev/ttyUSB0 \
  write_flash 0x10000 firmware.bin
```
