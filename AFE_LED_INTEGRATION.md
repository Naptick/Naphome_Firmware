# Using AFE-Processed Audio for Sound-Reactive LEDs

## What Changed

The sound-reactive LEDs (4, 8, 12) now use **AFE-processed audio** instead of raw microphone levels.

### Key Changes:

1. **AFE Enhancement Applied**
   - When AFE is active and processing, raw audio levels are boosted by 30x
   - This approximates the AGC (Automatic Gain Control) + beamforming gain from AFE
   - AFE-processed audio typically has 20-60x higher levels than raw

2. **Dual Scaling System**
   - `NOISE_FLOOR_RAW` (0.01f) and `MAX_LEVEL_RAW` (50.0f) for raw ES7210 audio
   - `NOISE_FLOOR_AFE` (100.0f) and `MAX_LEVEL_AFE` (3000.0f) for AFE-processed audio
   - `scale_audio_level_to_brightness()` now takes `is_afe_processed` parameter

3. **Automatic Fallback**
   - If AFE is enabled and processing → use enhanced levels (30x boost)
   - If AFE not ready or disabled → use raw levels
   - Logs show "AFE" vs "RAW" to indicate which is being used

## How It Works

```
Raw I2S → compute_mic_levels() → raw levels (low)
       ↓
    AFE Processing (beamforming + AGC + NS)
       ↓
    Enhanced levels (raw × 30) → update_mic_leds(is_afe_processed=true)
       ↓
    LEDs 4,8,12 react with much better sensitivity!
```

## Expected Results

- **Before**: LEDs barely react (raw levels 0.01-50 range, very low)
- **After**: LEDs react strongly (AFE-enhanced levels 0.3-1500 range)
- **Logs will show**: "AFE enhancing: RAW(X/Y/Z) -> AFE(X×30/Y×30/Z×30)"

## Build and Test

```bash
cd samples/korvo_farfield_mic_demo
idf.py build flash monitor
```

Look for:
- "AFE enhancing: RAW(...) -> AFE(...) boost=30.0x"
- "update_mic_leds called #1: ... (AFE)"
- "🎤 MIC Levels (AFE): ..." in periodic logs

LEDs 4, 8, 12 should now be much more reactive to sound!
