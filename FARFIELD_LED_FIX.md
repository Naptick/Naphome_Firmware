# Far-Field Processing and LED Issue

## Current Situation

**You ARE using far-field processing, but NOT for the LEDs:**

### ✅ Far-Field (AFE) is Active:
- `CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE=y` 
- AFE initialized with `AFE_MODE_HIGH_PERF`
- Includes: Beamforming (SE), Noise Suppression (NS), AGC
- **Used for wake word detection only**

### ❌ LEDs Use RAW Audio:
- Sound-reactive LEDs use `compute_mic_levels()` on **raw I2S samples**
- **NOT using AFE-processed/beamformed audio**
- Raw mic levels are very low (hence LEDs not reacting)

## The Root Cause

The LEDs 4, 8, 12 aren't sound reactive because:

1. **Raw audio levels are too low** - ES7210 raw output is very quiet
2. **AFE processing boosts the signal** - Beamforming + AGC + NS makes audio much louder
3. **LEDs should use AFE output** - But currently using raw input

## What's Happening

```
Raw I2S → compute_mic_levels() → update_mic_leds()
         ↓ (very low levels, LEDs don't react)
         
Raw I2S → AFE (beamforming/NS/AGC) → Wake word detection
         ↑ (processed, much better levels, but not used for LEDs)
```

## Solution Options

### Option 1: Use AFE-Processed Audio for LEDs (Recommended)
Extract audio levels from `afe_fetch_result_t` instead of raw samples.
AFE-processed audio has much better levels due to beamforming and AGC.

### Option 2: Keep Raw Audio but Increase Sensitivity (Current Fix)
- Lowered NOISE_FLOOR to 0.01f
- More aggressive scaling
- Minimum brightness guarantees
- This helps but AFE-processed would be better

### Option 3: Disable Far-Field
Set `CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE=n` if you don't need wake word.
Simpler but loses far-field benefits.

## Check Your Logs

Look for:
- "AFE initialized" - Far-field is running
- "AFE ready for far-field processing" - Wake word using AFE
- "MIC Levels: MIC1=X.XX" - These are RAW (low), not AFE-processed

If you see "Failed to initialize AFE", far-field isn't working and LEDs use raw audio only.

## Recommendation

The fixes I made (lower NOISE_FLOOR, better scaling) will help, but the **best solution** is to use AFE-processed audio levels for the LEDs instead of raw mic levels. This would give much better LED reactivity.
