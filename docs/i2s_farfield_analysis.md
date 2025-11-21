# I2S Pathway and Far-Field Configuration Analysis

## Current Configuration

### I2S Microphone Setup
- **I2S Port**: `I2S_NUM_0` (microphone capture)
- **Mode**: `I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM`
- **Sample Rate**: 16000 Hz (configurable via `CONFIG_KVA_SAMPLE_RATE`)
- **Channel Format**: `I2S_CHANNEL_FMT_ONLY_LEFT` ⚠️
- **Pins**:
  - DIN (Data In): GPIO 19
  - BCLK (Bit Clock): GPIO 18
  - WS (Word Select): GPIO 17
  - MCLK (Master Clock): GPIO 0
- **DMA**: 4 buffers x 256 samples

### I2S Playback Setup (ES8388 Codec)
- **I2S Port**: `I2S_NUM_1` (codec playback)
- **Mode**: `I2S_MODE_MASTER | I2S_MODE_TX`
- **Channel Format**: `I2S_CHANNEL_FMT_RIGHT_LEFT` (STEREO)

## Potential Issues for Far-Field Wake Word Detection

### 1. Channel Format Limitation
**Current**: `I2S_CHANNEL_FMT_ONLY_LEFT`
- Only reads from one microphone channel
- Korvo-1 has a **PDM microphone array** with multiple microphones
- For far-field pickup, we may need:
  - `I2S_CHANNEL_FMT_STEREO` to read from multiple mics
  - Or beamforming/aggregation of multiple mic channels

### 2. PDM Configuration
- PDM mode is correct for Korvo-1's microphone array
- However, PDM arrays typically have multiple microphones that can be:
  - Combined for better far-field pickup
  - Used for beamforming/direction finding
  - Used for noise cancellation

### 3. Sample Rate
- 16 kHz is standard for voice, but may need higher rates for better far-field detection
- ESP-IDF PDM typically supports up to 48 kHz

## Recommendations

### Option 1: Try STEREO Channel Format
Change `I2S_CHANNEL_FMT_ONLY_LEFT` to `I2S_CHANNEL_FMT_STEREO` to read from both left and right microphone channels, then:
- Sum/average the channels for better signal
- Use the stronger channel
- Implement simple beamforming

### Option 2: Verify Audio Data is Being Captured
The added logging will show:
- First audio sample values
- Min/max/avg levels
- Whether I2S is actually receiving data

### Option 3: Increase Sample Rate
Try 32 kHz or 48 kHz for better far-field sensitivity (if supported by hardware).

### Option 4: Check Hardware Connections
Verify:
- GPIO 19, 18, 17, 0 are correctly connected to the microphone array
- No pin conflicts with other peripherals (LEDs, etc.)

## Next Steps

1. **Flash with diagnostic logging** to confirm:
   - I2S initialization succeeds
   - Audio data is being captured (non-zero samples)
   - Sample values are reasonable (not all zeros or maxed out)

2. **If audio is captured but wake word doesn't trigger**:
   - Check wake word sensitivity/threshold settings
   - Verify noise floor calibration is working
   - Check if audio levels are too low

3. **If no audio is captured**:
   - Verify I2S pin configuration matches hardware
   - Check for pin conflicts
   - Try different channel format (STEREO)

4. **For far-field optimization**:
   - Consider implementing multi-microphone aggregation
   - Add AGC (Automatic Gain Control)
   - Implement noise reduction preprocessing
