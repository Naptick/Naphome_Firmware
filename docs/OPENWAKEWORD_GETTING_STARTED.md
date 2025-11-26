# OpenWakeWord - Getting Started Checklist

**Complete step-by-step guide to get OpenWakeWord working on your ESP32-S3**

## Prerequisites

- [ ] ESP-IDF v5.0+ installed and configured
- [ ] ESP32-S3 development board (Korvo-1 or similar)
- [ ] Python 3.8+ for training (optional, for model training)
- [ ] Basic familiarity with ESP-IDF build system

## Step-by-Step Setup

### Step 1: Setup Dependencies (5 minutes)

```bash
# Run the automated setup script
./scripts/setup_openwakeword.sh

# This will:
# - Add esp-tflite-micro git submodule
# - Validate component structure
# - Check for optional dependencies
```

**Expected output**: "Setup Complete!" with next steps

**If errors**: Check IDF_PATH is set, run `. $IDF_PATH/export.sh`

### Step 2: Validate Build (2 minutes)

```bash
# Validate your build configuration
./scripts/validate_openwakeword_build.sh

# This checks:
# - IDF environment
# - Required components
# - Optional optimizations
# - Demo application
```

**Expected output**: "✓ Validation passed!" or specific errors to fix

### Step 3: Test Without Model (10 minutes)

Test the entire integration immediately without training a model:

```bash
cd samples/korvo_openwakeword_demo

# Configure for test mode
idf.py menuconfig
# Navigate to: Component config → OpenWakeWord
# Enable: [*] Enable OpenWakeWord
# Enable: [*] Test mode (no model required)
# Save and exit

# Build
idf.py set-target esp32s3
idf.py build

# Flash and monitor
idf.py flash monitor
```

**What to expect**:
- Audio processing logs
- Melspectrogram extraction working
- Test mode detections on loud sounds
- Callbacks firing

**Success indicators**:
```
I (1234) openwakeword: Initializing OpenWakeWord
I (1235) openwakeword: Test mode enabled - no model required
I (1236) oww_test: Test mode melspectrogram: size=40, avg=0.123, max=0.456
I (5678) openwakeword: *** TEST MODE: WAKE WORD DETECTED ***
```

### Step 4: Train Model (1-2 days)

Once test mode works, train your "Hey, Naptick" model:

```bash
# Install OpenWakeWord Python library
pip install openwakeword

# Run training script
./scripts/train_openwakeword.sh

# Or follow detailed guide
# See: docs/openwakeword_training_guide.md
```

**Training steps**:
1. Collect or generate audio samples
2. Train model with OpenWakeWord
3. Convert to TFLite
4. Optimize for ESP32 (quantization)

**Expected output**: `hey_naptick.tflite` model file (50-200KB)

### Step 5: Flash Model (5 minutes)

Flash the trained model to your device:

```bash
# Option 1: Flash to SPIFFS partition
# (Requires SPIFFS image creation)

# Option 2: Flash directly to partition
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 \
    --port /dev/ttyUSB0 \
    write_flash 0x140000 hey_naptick.tflite

# Adjust offset based on your partition table
```

### Step 6: Deploy with Model (10 minutes)

Disable test mode and deploy with real model:

```bash
cd samples/korvo_openwakeword_demo

# Configure for production
idf.py menuconfig
# Component config → OpenWakeWord
# Disable: [ ] Test mode
# Set model path: /spiffs/hey_naptick.tflite
# Set threshold: 0.5
# Save and exit

# Build and flash
idf.py build flash monitor
```

**What to expect**:
- Model loads successfully
- Real "Hey, Naptick" detections
- Confidence scores
- Callbacks on detection

**Success indicators**:
```
I (1234) model_loader: Loaded model: 45678 bytes
I (1235) tflite_wrapper: TFLite wrapper initialized
I (1236) openwakeword: TFLite wrapper initialized
I (5678) openwakeword: *** WAKE WORD DETECTED *** Confidence: 0.85
```

### Step 7: Tune Threshold (30 minutes)

Adjust threshold based on testing:

```bash
idf.py menuconfig
# Component config → OpenWakeWord → Threshold

# Too many false positives? Increase (0.6-0.8)
# Misses detections? Decrease (0.3-0.4)
# Start at 0.5, adjust based on results
```

Test with various scenarios:
- Quiet environment
- Noisy environment
- Different speakers
- Different distances

### Step 8: Integrate into Your App (Optional)

If integrating into existing `wake_word_service`:

```bash
# Review integration patch
cat samples/korvo_voice_assistant/main/wake_word_service_oww_patch.diff

# Follow production guide
# See: docs/OPENWAKEWORD_PRODUCTION_GUIDE.md
```

## Quick Reference

### Essential Commands

```bash
# Setup
./scripts/setup_openwakeword.sh

# Validate
./scripts/validate_openwakeword_build.sh

# Build demo
cd samples/korvo_openwakeword_demo
idf.py set-target esp32s3
idf.py menuconfig  # Configure
idf.py build flash monitor

# Train model
./scripts/train_openwakeword.sh

# Check performance
./scripts/check_openwakeword_performance.sh
```

### Key Configuration

| Config | Default | Description |
|--------|---------|-------------|
| `CONFIG_OPENWAKEWORD_ENABLE` | y | Enable component |
| `CONFIG_OPENWAKEWORD_USE_TFLITE` | y | Enable TFLite |
| `CONFIG_OPENWAKEWORD_MODEL_PATH` | `/spiffs/hey_naptick.tflite` | Model path |
| `CONFIG_OPENWAKEWORD_THRESHOLD` | 0.5 | Detection threshold |
| `CONFIG_OPENWAKEWORD_FRAME_SIZE_MS` | 80 | Frame size |
| `CONFIG_OPENWAKEWORD_TEST_MODE` | n | Test without model |

### Troubleshooting

| Issue | Solution |
|-------|----------|
| "esp-tflite-micro not found" | Run `./scripts/setup_openwakeword.sh` |
| "Failed to load model" | Check SPIFFS mounted, model path correct |
| "Failed to allocate tensors" | Increase `CONFIG_OPENWAKEWORD_TENSOR_ARENA_SIZE` |
| No detections | Lower threshold, check audio input |
| Too many false positives | Raise threshold, disable test mode |
| Build errors | Run `./scripts/validate_openwakeword_build.sh` |

## Next Steps After Setup

1. **Test thoroughly** - Various environments and speakers
2. **Tune threshold** - Based on false positive rate
3. **Monitor performance** - Use `openwakeword_get_statistics()`
4. **Integrate into app** - Follow production guide
5. **Optimize** - Enable ESP-DSP, tune frame size
6. **Deploy** - Production deployment

## Documentation Map

- **Just starting?** → [Quick Start](OPENWAKEWORD_QUICK_START.md)
- **Need reference?** → [Quick Reference](OPENWAKEWORD_QUICK_REFERENCE.md)
- **Integrating?** → [Production Guide](OPENWAKEWORD_PRODUCTION_GUIDE.md)
- **Testing?** → [Test Mode Guide](OPENWAKEWORD_TEST_MODE.md)
- **Training?** → [Training Guide](openwakeword_training_guide.md)
- **Troubleshooting?** → [Production Guide - Troubleshooting](OPENWAKEWORD_PRODUCTION_GUIDE.md#troubleshooting)
- **Everything?** → [Complete Index](OPENWAKEWORD_COMPLETE_INDEX.md)

## Success Criteria

You're ready for production when:

- [ ] Test mode works (audio processing validated)
- [ ] Model trains successfully
- [ ] Model loads on device
- [ ] Detections occur on "Hey, Naptick"
- [ ] False positive rate acceptable
- [ ] Performance acceptable (CPU, latency)
- [ ] Integration tested
- [ ] Threshold tuned
- [ ] Error handling verified

## Estimated Timeline

- **Setup & Test Mode**: 15 minutes
- **Model Training**: 1-2 days
- **Integration & Tuning**: 2-4 hours
- **Total**: 1-3 days to production-ready

## Support

- **Documentation**: See [Complete Index](OPENWAKEWORD_COMPLETE_INDEX.md)
- **Examples**: `samples/korvo_openwakeword_demo/`
- **Scripts**: `scripts/` directory
- **Component**: `components/openwakeword/`

You're all set! 🚀