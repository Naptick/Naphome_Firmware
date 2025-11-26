# Are We Using Far-Field Processing?

## Answer: **PARTIALLY**

### ✅ What IS Using Far-Field (AFE):

1. **Wake Word Detection** - YES, using ESP-SR AFE (Audio Front-End)
   - `CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE=y` in sdkconfig.defaults
   - AFE initialized with `AFE_MODE_HIGH_PERF` for far-field processing
   - Includes:
     - **SE (Speech Enhancement/Beamforming)** - `afe_config->se_init = true`
     - **VAD (Voice Activity Detection)**
     - **NS (Noise Suppression)**
     - **AGC (Automatic Gain Control)**
   - AFE processes audio through `s_afe_handle->feed()` and `s_afe_handle->fetch()`
   - Only used for wake word detection, not for LED visualization

### ❌ What is NOT Using Far-Field:

1. **Sound-Reactive LEDs (4, 8, 12)** - Using RAW audio levels
   - LEDs are driven by `compute_mic_levels()` which processes RAW I2S samples
   - Simple averaging of left/right channels
   - NO beamforming, NO noise suppression, NO AGC
   - This is likely why LEDs aren't reacting well - raw mic levels are very low

## The Problem

The sound-reactive LEDs are using **raw microphone levels** instead of **AFE-processed audio**:

```c
// Current: Raw audio levels
compute_mic_levels(frame_buffer, samples_read, &mic1_level, &mic2_level, &mic3_level);
update_mic_leds(mic1_level, mic2_level, mic3_level);

// AFE is only used for wake word
check_wake_word_afe(frame_buffer, samples_read);
```

AFE-processed audio would have:
- Better signal-to-noise ratio (beamforming)
- Noise suppression applied
- AGC normalization
- Much better levels for LED visualization

## Solution: Use AFE-Processed Audio for LEDs

We should extract audio levels from AFE's processed output instead of raw mic levels. The `afe_fetch_result_t` may contain processed audio data that we can use.

## Check Your Current Setup

Look for these logs to confirm:
- "AFE initialized" - Far-field processing is active
- "AFE ready for far-field processing" - Wake word detection using AFE
- "MIC Levels: MIC1=X.XX" - These are RAW levels, not AFE-processed

If AFE fails to initialize, you'll see:
- "Failed to initialize models from 'model' partition"
- LEDs will still work but with raw audio (very low sensitivity)
