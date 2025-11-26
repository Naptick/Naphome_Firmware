# Spotify Connect Device Not Appearing - Investigation

## Issue
The Spotify device is not appearing in the Spotify app.

## Root Cause
**Spotify Connect (cspot) is disabled** in the firmware configuration.

## Current Status
- `CONFIG_KVA_SPOTIFY_USE_CSPOT` defaults to `n` (disabled) in `Kconfig.projbuild`
- The configuration is not set in `sdkconfig`, so cspot code is not compiled
- Without cspot, the device cannot register as a Spotify Connect endpoint via mDNS/zeroconf

## How Spotify Connect Works
1. **cspot library**: Embeds a Spotify Connect receiver that makes the device appear in the Spotify app
2. **mDNS/Zeroconf**: Registers a `_spotify-connect._tcp` service on the local network
3. **HTTP Server**: Runs on port 8080 to receive credentials from the Spotify app
4. **Initial Pairing**: On first boot, the device waits for the Spotify app to provision credentials via zeroconf
5. **Subsequent Boots**: Loads saved credentials from SPIFFS (`/spiffs/spotify_blob.json`)

## Code Flow
- `naphome_voice_assistant_main.c` line 718-727: Conditionally starts `spotify_player_start()` only if `CONFIG_KVA_SPOTIFY_USE_CSPOT` is enabled
- `spotify_player.cpp` line 200-226: `SpotifyPlayerTask::runTask()`:
  - Initializes mDNS
  - Sets hostname from device name
  - Tries to load saved credentials from SPIFFS
  - If no credentials, runs zeroconf setup (waits for Spotify app)
  - Starts Spotify Connect session

## Fix Applied
1. ✅ Added to `sdkconfig.defaults`:
   ```
   CONFIG_KVA_SPOTIFY_USE_CSPOT=y
   CONFIG_KVA_SPOTIFY_DEVICE_NAME="Korvo Naphome"
   ```

## Next Steps
1. **Rebuild firmware** to apply the configuration:
   ```bash
   cd samples/korvo_voice_assistant
   idf.py build flash
   ```

2. **Verify cspot is enabled**:
   - Check logs for: `"Waiting for Spotify app to provision credentials via zeroconf..."`
   - Check logs for: `"Spotify Connect session started as <device-name>"`

3. **Pair from Spotify app**:
   - Open Spotify app (mobile or desktop)
   - Go to "Devices Available" or "Connect to a device"
   - Look for "Korvo Naphome" in the device list
   - If not visible, check:
     - Device and phone/app are on the same WiFi network
     - mDNS is working (check router/firewall settings)
     - Port 8080 is not blocked

4. **First-time pairing**:
   - Device will show "Waiting for Spotify app to provision credentials via zeroconf..."
   - Spotify app will show "Log in with device code" option
   - Follow the pairing flow in the app
   - Credentials will be saved to `/spiffs/spotify_blob.json`

## Troubleshooting

### Device not appearing in Spotify app
- **Check WiFi**: Device and Spotify app must be on the same network
- **Check mDNS**: Some routers block mDNS (multicast DNS). Try:
  - Disable "AP Isolation" or "Client Isolation" on router
  - Ensure mDNS/Bonjour is enabled
- **Check logs**: Look for mDNS registration errors
- **Check port 8080**: Ensure it's not blocked by firewall

### "cspot support disabled in menuconfig" warning
- Configuration not applied. Run `idf.py reconfigure` or `idf.py build`

### "Unable to obtain Spotify credentials"
- Zeroconf pairing failed or timed out
- Try resetting SPIFFS to force re-pairing
- Check network connectivity

### "Spotify Connect player failed to start"
- Check if cspot repository exists at `../cspot`
- Check CMakeLists.txt for CSPOT_SOURCE_DIR
- Verify cspot dependencies are available

## Related Files
- `samples/korvo_voice_assistant/main/spotify_player.cpp` - cspot integration
- `samples/korvo_voice_assistant/main/naphome_voice_assistant_main.c` - initialization
- `samples/korvo_voice_assistant/Kconfig.projbuild` - configuration options
- `samples/korvo_voice_assistant/sdkconfig.defaults` - default settings
