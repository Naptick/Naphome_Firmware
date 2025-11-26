# Spotify Connect Pairing Guide

## How to Pair Your Device with Spotify

Your Naphome device can act as a Spotify Connect speaker, allowing you to stream music directly from the Spotify app.

## Prerequisites

1. **Device is powered on and connected to WiFi**
   - Check the dashboard to confirm WiFi connection status
   - Device must be on the same WiFi network as your phone/computer

2. **cspot is enabled** (configured in firmware)
   - Device name: "Korvo Naphome" (default)

## Pairing Process

### First-Time Pairing (No Saved Credentials)

1. **Power on the device**
   - The device will boot and connect to WiFi
   - cspot will initialize and check for saved credentials

2. **Device enters pairing mode**
   - If no credentials are found, the device will log:
     ```
     "No saved credentials found, starting zeroconf pairing..."
     "Waiting for Spotify app to provision credentials via zeroconf..."
     ```
   - The device is now waiting for the Spotify app to discover it

3. **Open Spotify app** (Mobile or Desktop)
   - **Mobile (iOS/Android)**:
     - Open the Spotify app
     - Tap the "Devices Available" button (usually at the bottom of the Now Playing screen)
     - Or go to Settings → Connect to a device
   - **Desktop (Windows/Mac/Linux)**:
     - Click the "Devices Available" button (bottom-right of the Spotify window)
     - Or go to Settings → Show Available Devices

4. **Look for your device**
   - The device should appear in the list as **"Korvo Naphome"**
   - It may take 10-30 seconds to appear after the device boots
   - If it doesn't appear, see Troubleshooting below

5. **Select the device**
   - Tap/click on "Korvo Naphome" in the device list
   - Spotify will automatically provision credentials to the device
   - The device will log: `"Received Spotify login blob over zeroconf"`

6. **Pairing complete**
   - Credentials are saved to `/spiffs/spotify_blob.json`
   - The device will log: `"Spotify Connect session started as Korvo Naphome"`
   - You can now play music to the device from Spotify!

### Subsequent Connections (Already Paired)

Once paired, the device will automatically:
- Load saved credentials on boot
- Connect to Spotify servers
- Appear in your Spotify app's device list whenever it's powered on

You don't need to pair again unless:
- You reset the device's storage (SPIFFS)
- You delete the credentials file
- The credentials expire (rare)

## How It Works

1. **mDNS/Zeroconf Discovery**
   - Device registers as `_spotify-connect._tcp` service on port 8080
   - Spotify app discovers the device via mDNS (multicast DNS)
   - Both must be on the same local network

2. **Credential Provisioning**
   - Spotify app sends your account credentials to the device via HTTP POST
   - Device receives and stores credentials securely
   - Credentials are encrypted and stored in SPIFFS

3. **Spotify Connect Session**
   - Device authenticates with Spotify servers
   - Establishes a persistent connection
   - Appears as a playback device in your Spotify app

## Troubleshooting

### Device Not Appearing in Spotify App

**Check 1: Same WiFi Network**
- ✅ Device and phone/computer must be on the **same WiFi network**
- ❌ Different networks (e.g., device on 2.4GHz, phone on 5GHz) may not work
- ❌ Guest networks or isolated networks won't work

**Check 2: mDNS/Bonjour Support**
- Some routers block mDNS (multicast DNS)
- Try disabling "AP Isolation" or "Client Isolation" in router settings
- Ensure mDNS/Bonjour is enabled on your router
- Some enterprise networks block mDNS for security

**Check 3: Firewall/Port Blocking**
- Device uses port **8080** for credential provisioning
- Ensure your router/firewall allows traffic on port 8080
- Some corporate networks block this port

**Check 4: Device Status**
- Check the dashboard logs for:
  - `"cspot mDNS initialized with hostname: korvo-naphome"`
  - `"Waiting for Spotify app to provision credentials via zeroconf..."`
- If you see errors, check:
  - WiFi connection status
  - SPIFFS mount status
  - mDNS initialization errors

**Check 5: Wait Time**
- It can take 10-30 seconds for the device to appear
- Try refreshing the device list in Spotify app
- Restart the Spotify app if it doesn't appear

### "Unable to obtain Spotify credentials"

- Zeroconf pairing failed or timed out
- **Solution**: 
  - Ensure device and Spotify app are on same network
  - Check router mDNS settings
  - Try restarting both device and Spotify app

### "Spotify authentication failed"

- Credentials were received but authentication with Spotify servers failed
- **Solution**:
  - Check internet connectivity
  - Verify Spotify service is available
  - Try re-pairing (delete credentials and start over)

### Re-pairing (Reset Credentials)

If you need to pair again with a different account:

1. **Delete credentials file** (via serial command or SPIFFS reset):
   ```bash
   # Via serial monitor or dashboard
   # Delete /spiffs/spotify_blob.json
   ```

2. **Reboot the device**
   - Device will enter pairing mode again
   - Follow the pairing steps above

## Device Configuration

The device name can be changed in `sdkconfig.defaults`:
```
CONFIG_KVA_SPOTIFY_DEVICE_NAME="Your Custom Name"
```

After changing, rebuild and flash:
```bash
cd samples/korvo_voice_assistant
idf.py build flash
```

## Technical Details

- **Service Type**: `_spotify-connect._tcp`
- **Port**: 8080 (HTTP server for credential provisioning)
- **Protocol**: mDNS/Zeroconf for discovery, HTTP for provisioning
- **Storage**: Credentials saved to `/spiffs/spotify_blob.json`
- **Library**: cspot (C++ Spotify Connect implementation)

## Related Documentation

- [Spotify Connect Investigation](./SPOTIFY_CONNECT_INVESTIGATION.md) - Technical details
- [cspot Repository](https://github.com/librespot-org/librespot) - Library documentation
