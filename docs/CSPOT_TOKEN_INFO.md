# cspot Token/Credentials Information

## How cspot Handles Credentials

**Important**: The cspot example in `../cspot/targets/esp32` does **NOT** use a pre-saved token file. It always waits for zeroconf pairing from the Spotify app.

## Our Implementation

Our implementation in `spotify_player.cpp`:
1. **First boot**: Tries to load credentials from `/spiffs/spotify_blob.json`
2. **If no credentials found**: Enters zeroconf pairing mode (waits for Spotify app)
3. **After pairing**: Saves credentials to `/spiffs/spotify_blob.json` for future boots

## Why Device Doesn't Appear in Spotify

For the device to appear in Spotify Connect, it must be **waiting for pairing** (zeroconf mode). This happens when:
- No credentials file exists at `/spiffs/spotify_blob.json`
- The credentials file is invalid or corrupted
- The device is in pairing mode

## Checking if cspot is Starting

Look for these log messages:
- `"Starting Spotify Connect (cspot) player..."`
- `"Creating SpotifyPlayerTask (device: Korvo Naphome, port: 8080)"`
- `"SpotifyPlayerTask created successfully"`
- `"cspot mDNS initialized with hostname: korvo-naphome"`
- `"No saved credentials found, starting zeroconf pairing..."`
- `"Waiting for Spotify app to provision credentials via zeroconf..."`

If you see these messages, cspot is running and waiting for pairing.

## Using a Saved Token (Advanced)

If you have a credentials file from another cspot device:
1. Copy the JSON blob file to `/spiffs/spotify_blob.json` on the device
2. The device will load it on next boot
3. The device will connect directly without waiting for pairing

**Note**: Credentials are device-specific and tied to the device ID. You cannot simply copy credentials between different devices.

## Troubleshooting

### Device Not Appearing
1. **Check logs** for cspot initialization messages
2. **Verify WiFi** is connected (device and phone must be on same network)
3. **Check mDNS** - some routers block multicast DNS
4. **Wait 10-30 seconds** after device boots for mDNS to register
5. **Restart Spotify app** to refresh device list

### No cspot Logs
- Check `CONFIG_KVA_SPOTIFY_USE_CSPOT=y` in `sdkconfig.defaults`
- Verify cspot repository exists at `../cspot`
- Rebuild firmware: `idf.py build flash`
