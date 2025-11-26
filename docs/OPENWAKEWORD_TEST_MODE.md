# OpenWakeWord Test Mode

## Overview

Test Mode allows you to test OpenWakeWord integration **without a trained TFLite model**. This is useful for:

- Testing audio preprocessing (melspectrogram extraction)
- Validating integration with your application
- Debugging audio pipeline issues
- Development before model training is complete

## How It Works

Test Mode uses a simple energy-based detector as a placeholder:

1. **Audio Preprocessing**: Full melspectrogram extraction (same as production)
2. **Detection**: Simple RMS energy threshold (NOT a real wake word detector)
3. **Callbacks**: Triggers the same callbacks as real detection

**⚠️ WARNING**: Test Mode is NOT suitable for production. It's a crude energy detector that will trigger on any loud sound.

## Enabling Test Mode

### Option 1: Via menuconfig

```bash
idf.py menuconfig
# Component config -> OpenWakeWord -> Enable test mode
```

### Option 2: Via sdkconfig.defaults

```ini
CONFIG_OPENWAKEWORD_TEST_MODE=y
```

### Option 3: Build flag

```bash
idf.py build -DCONFIG_OPENWAKEWORD_TEST_MODE=y
```

## Usage

With test mode enabled, OpenWakeWord will:

1. **Always initialize** (even without a model)
2. **Process audio** through melspectrogram extraction
3. **Simulate detections** based on audio energy
4. **Trigger callbacks** when energy exceeds threshold

### Example Output

```
I (1234) openwakeword: Initializing OpenWakeWord
I (1235) openwakeword: Test mode enabled - no model required
I (1236) openwakeword: Audio preprocessing ready
...
I (5678) oww_test: Test mode: RMS=0.0234, dB=-32.6, confidence=0.45
I (5679) openwakeword: *** TEST MODE: WAKE WORD DETECTED *** Confidence: 0.65 (threshold: 0.50)
```

## Tuning Test Mode

Test mode uses these parameters (in `openwakeword_test_mode.c`):

```c
const float ENERGY_THRESHOLD_DB = -30.0f;  // Adjust for your mic
const float MIN_CONFIDENCE = 0.3f;
const float MAX_CONFIDENCE = 0.9f;
```

Adjust `ENERGY_THRESHOLD_DB` based on your microphone sensitivity:
- **-40dB to -30dB**: Very sensitive (will trigger on quiet sounds)
- **-30dB to -20dB**: Normal sensitivity
- **-20dB to -10dB**: Less sensitive (needs louder sounds)

## Testing Workflow

### 1. Enable Test Mode
```bash
# In sdkconfig.defaults or menuconfig
CONFIG_OPENWAKEWORD_TEST_MODE=y
```

### 2. Build and Flash
```bash
cd samples/korvo_openwakeword_demo
idf.py build flash monitor
```

### 3. Test Audio Pipeline
- Speak into microphone
- Watch for "TEST MODE: WAKE WORD DETECTED" messages
- Verify melspectrogram logs show reasonable values
- Check that callbacks are triggered

### 4. Disable for Production
```bash
CONFIG_OPENWAKEWORD_TEST_MODE=n
idf.py build flash
```

## What Gets Tested

✅ **Audio preprocessing**: Melspectrogram extraction works  
✅ **Integration**: Callbacks fire correctly  
✅ **Audio pipeline**: Audio reaches OpenWakeWord  
✅ **Configuration**: Thresholds and settings work  
✅ **Memory**: No leaks or crashes  

❌ **NOT tested**: Actual wake word detection accuracy (needs real model)

## Limitations

- **False positives**: Will trigger on any loud sound
- **No wake word specificity**: Doesn't detect "Hey, Naptick" specifically
- **Energy-based only**: No ML inference
- **Not production-ready**: For development/testing only

## Next Steps

Once test mode validates your integration:

1. **Disable test mode**: `CONFIG_OPENWAKEWORD_TEST_MODE=n`
2. **Train model**: `./scripts/train_openwakeword.sh`
3. **Flash model**: Add to SPIFFS or partition
4. **Test real detection**: With trained model

## Troubleshooting

**No detections in test mode?**
- Check `ENERGY_THRESHOLD_DB` is appropriate for your mic
- Verify audio is reaching OpenWakeWord (check logs)
- Lower the threshold temporarily for testing

**Too many false positives?**
- Raise `ENERGY_THRESHOLD_DB`
- Increase `CONFIG_OPENWAKEWORD_THRESHOLD`
- This is expected - test mode is crude

**Melspectrogram values look wrong?**
- Check sample rate matches (16kHz)
- Verify audio format (16-bit PCM)
- Check FFT is working (ESP-DSP or fallback)