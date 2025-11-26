# "Hey, Naptick" Training Setup - Complete! ✅

## What's Been Done

### ✅ esp-tflite-micro Added
The TensorFlow Lite Micro component has been added as a git submodule:
- Location: `components/esp-tflite-micro/`
- Status: Initialized and ready to use
- Next: Will be automatically detected by build system

### ✅ Training Environment Created
Complete training infrastructure set up:
- Training directory: `training/hey_naptick/`
- Training scripts created
- Data collection guides
- Synthetic data generator

### ✅ Training Scripts
- `scripts/setup_training_environment.sh` - Setup Python environment
- `scripts/train_hey_naptick.sh` - Main training script
- `training/hey_naptick/train_hey_naptick.py` - Python training script
- `training/hey_naptick/generate_synthetic_data.py` - TTS data generator

### ✅ Documentation
- `docs/TRAIN_HEY_NAPTICK_GUIDE.md` - Complete training guide
- `docs/HEY_NAPTICK_TRAINING_QUICK_START.md` - Quick start
- `training/hey_naptick/COLLECT_DATA.md` - Data collection guide
- `training/hey_naptick/README.md` - Training directory guide

## Next Steps to Train "Hey, Naptick"

### Step 1: Setup Python Environment (2 minutes)
```bash
./scripts/setup_training_environment.sh
```

This installs:
- OpenWakeWord Python library
- Dependencies (numpy, scipy, librosa, tensorflow)

### Step 2: Collect Training Data (1-2 days)

**Option A: Generate Synthetic Data (Fast - 30 minutes)**
```bash
cd training/hey_naptick
python3 generate_synthetic_data.py
```
Generates 200 positive + 300 negative samples using TTS.

**Option B: Record Real Data (Better - 1-2 days)**
```bash
# Record "Hey, Naptick" samples
# Save to: training/hey_naptick/data/positive/
# See: training/hey_naptick/COLLECT_DATA.md
```

**Recommended**: Start with synthetic, add real data later.

### Step 3: Train Model (10-30 minutes)
```bash
./scripts/train_hey_naptick.sh
# Or
cd training/hey_naptick
python3 train_hey_naptick.py
```

**Note**: Full training requires OpenWakeWord's training pipeline. The script will guide you.

### Step 4: Flash Model (5 minutes)
```bash
# Model will be at: hey_naptick.tflite
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 \
    --port /dev/ttyUSB0 \
    write_flash 0x140000 hey_naptick.tflite
```

### Step 5: Test (10 minutes)
```bash
cd samples/korvo_openwakeword_demo
idf.py menuconfig
# Set: CONFIG_OPENWAKEWORD_MODEL_PATH="/spiffs/hey_naptick.tflite"
# Set: CONFIG_OPENWAKEWORD_TEST_MODE=n
idf.py build flash monitor
```

## Quick Reference

```bash
# Complete workflow
./scripts/setup_training_environment.sh          # Setup
cd training/hey_naptick && python3 generate_synthetic_data.py  # Generate data
./scripts/train_hey_naptick.sh                  # Train
# Flash model (see Step 4 above)
# Test (see Step 5 above)
```

## Training Data Requirements

### Minimum
- Positive: 50 samples
- Negative: 100 samples

### Recommended
- Positive: 100-200 samples
- Negative: 200-500 samples

### Format
- WAV files
- 16kHz sample rate
- Mono channel
- 16-bit depth
- 1-3 seconds long

## Expected Results

With good training:
- **Detection Rate**: 85-95%
- **False Positive Rate**: <5%
- **Model Size**: 50-200KB
- **Training Time**: 10-30 minutes

## Troubleshooting

**"openwakeword not found"**
→ Run `./scripts/setup_training_environment.sh`

**"No training data found"**
→ Generate synthetic data or collect real samples

**"Training fails"**
→ Check OpenWakeWord documentation for training API changes

**"Model too large"**
→ Use quantization, see training guide

## Resources

- **Full Guide**: `docs/TRAIN_HEY_NAPTICK_GUIDE.md`
- **Quick Start**: `docs/HEY_NAPTICK_TRAINING_QUICK_START.md`
- **Data Collection**: `training/hey_naptick/COLLECT_DATA.md`
- **OpenWakeWord**: https://github.com/dscripka/openWakeWord

## Status

✅ **Setup Complete** - Ready to train!

Just follow the steps above to train your "Hey, Naptick" model. 🚀