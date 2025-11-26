# Korvo-1 Far-Field Microphone Array Demo

This focused demo demonstrates I2S far-field microphone array processing on the Korvo-1 board with sound-reactive LEDs.

## What It Does

- **Initializes I2S in STEREO mode** to capture from Korvo-1's 3-microphone PDM array
- **Processes audio frames** to extract energy levels from each microphone channel:
  - **MIC1** (Left channel) - First microphone
  - **MIC2** (Right channel) - Second microphone  
  - **MIC3** (Combined) - Averaged signal for far-field beamforming
- **Updates LEDs in real-time** based on audio levels:
  - **LED 4** (Blue) = MIC1 sensitivity
  - **LED 8** (Green) = MIC2 sensitivity
  - **LED 12** (Red) = MIC3 (combined far-field) sensitivity
- **Logs audio levels** periodically to show microphone activity
- **Wake word detection** using ESP-SR WakeNet with "hi esp" wake word (wn9_hiesp)

## Hardware

- **Korvo-1** (ESP32-S3) development board
- **3 PDM microphones** on-board (GPIO19/18/17/0)
- **12 WS2812 LEDs** in a ring (GPIO19)

## Build & Flash

```bash
cd samples/korvo_farfield_mic_demo
idf.py set-target esp32s3
idf.py menuconfig  # Optional: adjust LED GPIO, sample rate, etc.
idf.py build flash monitor
```

## Configuration

The demo can be configured via `menuconfig` under **"Korvo Far-Field Mic Demo"**:

- **LED GPIO**: WS2812 data pin (default: GPIO19)
- **LED Count**: Number of LEDs (default: 12)
- **LED Brightness**: 0-255 (default: 32)
- **Sample Rate**: Audio sample rate in Hz (default: 16000)
- **Frame Size**: Audio frame size in milliseconds (default: 20ms)
- **Wake Word Enable**: Enable ESP-SR WakeNet wake word detection (default: enabled)
- **Wake Word Model**: Wake word model name (default: "wn9_hiesp" for "hi esp")
- **Wake Word Threshold**: Detection sensitivity 0-100 (default: 60, higher = less sensitive)

## How It Works

1. **I2S Configuration**: The microphone is initialized in `I2S_CHANNEL_FMT_RIGHT_LEFT` (STEREO) mode to capture from multiple microphones simultaneously.

2. **Audio Processing**: Each frame of audio is processed to compute the average absolute amplitude for:
   - Left channel (MIC1)
   - Right channel (MIC2)
   - Combined average (MIC3) - provides better far-field pickup

3. **LED Visualization**: Audio levels are scaled to LED brightness (0-255) using a logarithmic-like curve for better visual response. The LEDs update in real-time as you speak.

4. **Far-Field Processing**: The combined channel (MIC3) averages both left and right channels, which helps with:
   - Better signal-to-noise ratio
   - Improved far-field pickup
   - Reduced sensitivity to direction

## Expected Output

When running, you should see:

```
I (1234) farfield_mic: === Korvo-1 Far-Field Microphone Array Demo ===
I (1235) farfield_mic: LED GPIO: 19, LED Count: 12, Brightness: 32
I (1236) farfield_mic: Sample Rate: 16000 Hz, Frame Size: 20 ms (320 samples)
I (1237) farfield_mic: LED strip initialized
I (1238) korvo1: I2S Configuration: mode=PDM_RX, sample_rate=16000, channel_format=STEREO
I (1239) farfield_mic: Microphone initialized and started
I (1240) farfield_mic: Starting audio processing task...
I (1241) farfield_mic: LED 4 (Blue) = MIC1 (Left channel)
I (1242) farfield_mic: LED 8 (Green) = MIC2 (Right channel)
I (1243) farfield_mic: LED 12 (Red) = MIC3 (Combined far-field)
I (1244) farfield_mic: Speak near the device to see the LEDs react!
I (1245) farfield_mic: Wake word detection: wn9_hiesp (hi esp) - threshold=60
I (2345) farfield_mic: 🎤 MIC Levels: MIC1=450 (LED4=45), MIC2=380 (LED8=32), MIC3=415 (LED12=38)
```

## Troubleshooting

**No LED activity:**
- Check that the LED GPIO is correct (default GPIO19)
- Verify the microphone is receiving audio (check logs for non-zero levels)
- Try speaking louder or closer to the device
- Check that LED strip initialized successfully (check logs for "LED strip initialized successfully")
- Verify GPIO19 is connected to WS2812 data line

**LEDs always off:**
- Audio levels may be below the noise floor (100.0)
- Check logs to see actual audio levels
- Adjust `NOISE_FLOOR` and `MAX_LEVEL` constants in code if needed

**Only one LED reacts:**
- Verify I2S is configured in STEREO mode (check logs)
- If only MIC1 reacts, the right channel may not be active
- Check hardware connections for the microphone array

## Next Steps

This demo can be extended to:
- Add beamforming algorithms for direction finding
- Implement noise cancellation between channels
- Add frequency-domain analysis (FFT) for better visualization
- Create more sophisticated LED patterns (rainbow, spectrum analyzer, etc.)
- Integrate with wake word detection
