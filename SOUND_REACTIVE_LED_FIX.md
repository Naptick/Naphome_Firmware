# Sound Reactive LEDs 4, 8, 12 Fix

## Issue
White LEDs work, but LEDs 4, 8, 12 are not sound reactive.

## Changes Made

### 1. Increased Sensitivity
- **NOISE_FLOOR**: Lowered from `0.1f` to `0.01f` (10x more sensitive)
- This catches much quieter audio signals

### 2. More Aggressive Scaling
- Increased low-level boost from 2x to **3x** for levels between 0.001-0.2
- Added **minimum 5% brightness** for any audio above noise floor
- Added **minimum 1 unit brightness** if audio detected but scaled to 0

### 3. Enhanced Debugging
Added logging to help diagnose:
- First 5 calls to `update_mic_leds()` with audio levels
- First 5 audio captures with sample counts
- Wake word blocking state (every 500 frames)
- LED scaling values for first 10 updates

## What to Check

### 1. Monitor Logs
Look for these log messages:
```
update_mic_leds called #1: MIC1=X.XX, MIC2=X.XX, MIC3=X.XX
Audio captured: X samples, levels: MIC1=X.XX, MIC2=X.XX, MIC3=X.XX
LED update: MIC1=X.XX->Y, MIC2=X.XX->Y, MIC3=X.XX->Y (scaled: A,B,C)
```

### 2. Check Audio Levels
- If levels are 0.00, audio isn't being captured
- If levels are < 0.01, they're below old noise floor but should work now
- If levels are > 0.01 but LEDs don't light, check wake word blocking

### 3. Check Wake Word
If `CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE=y`:
- Look for "Wake word active - LEDs 4,8,12 in white mode"
- Wake word keeps LEDs white for 1 second after detection
- After 1 second, should resume sound-reactive mode

### 4. Verify LED Indices
- LED 4 (index 3) = Blue = MIC1
- LED 8 (index 7) = Green = MIC2  
- LED 12 (index 11) = Red = MIC3
- Ensure `LED_COUNT >= 12` in config

## Build and Test

```bash
cd samples/korvo_farfield_mic_demo
idf.py build flash monitor
```

Watch for the debug logs to see:
1. Are audio levels being computed?
2. Is update_mic_leds being called?
3. What are the scaled brightness values?
4. Is wake word blocking?

## Expected Behavior

With these changes:
- LEDs 4, 8, 12 should react to even very quiet audio
- Minimum brightness ensures visibility even at low levels
- More aggressive scaling makes response more noticeable
- Debug logs help identify if issue is audio capture or LED update
