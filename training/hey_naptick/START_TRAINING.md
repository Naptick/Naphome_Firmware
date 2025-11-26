# Start Training "Hey, Naptick" - Step by Step

Follow these steps to train your "Hey, Naptick" wake word model.

## Step 1: Setup Environment ✅

Run the setup script:
```bash
cd /path/to/Naphome-Firmware
./scripts/setup_training_environment.sh
```

This installs:
- OpenWakeWord Python library
- Dependencies (numpy, scipy, librosa, tensorflow)

## Step 2: Choose Training Data Method

### Option A: Generate Synthetic Data (Recommended for First Try)

**Fastest way to get started:**

```bash
cd training/hey_naptick

# Install TTS library (choose one)
pip3 install --user gtts        # Google TTS (requires internet)
# OR
pip3 install --user pyttsx3     # Offline TTS

# Generate synthetic data
python3 generate_synthetic_data.py
```

This will:
- Generate 200 "Hey, Naptick" samples using TTS
- Generate 300 negative samples
- Save to `data/positive/` and `data/negative/`

**Time**: ~30 minutes (depends on TTS service)

### Option B: Record Real Samples (Better Accuracy)

**Best quality, more effort:**

1. Record "Hey, Naptick" 100-200 times
2. Various conditions:
   - Different distances (close, medium, far)
   - Different environments (quiet, noisy)
   - Different speakers (if possible)
   - Different tones (normal, excited, whispered)

3. Save as WAV files:
   - Format: 16kHz, mono, 16-bit
   - Naming: `hey_naptick_001.wav`, `hey_naptick_002.wav`, etc.
   - Location: `training/hey_naptick/data/positive/`

4. Record negative samples:
   - Other wake words ("Hey Google", "Alexa")
   - Background noise
   - Other speech
   - Location: `training/hey_naptick/data/negative/`

**Time**: 1-2 days

**See**: `COLLECT_DATA.md` for detailed instructions

## Step 3: Train the Model

Once you have training data:

```bash
# From project root
./scripts/train_hey_naptick.sh

# Or from training directory
cd training/hey_naptick
python3 train_hey_naptick.py
```

**Note**: The training script will guide you. Full training requires OpenWakeWord's training pipeline.

## Step 4: Export Model

After training, you'll have:
- `hey_naptick.tflite` in project root
- Or in `training/hey_naptick/output/`

## Step 5: Flash to ESP32

```bash
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 \
    --port /dev/ttyUSB0 \
    write_flash 0x140000 hey_naptick.tflite
```

## Step 6: Test

```bash
cd samples/korvo_openwakeword_demo
idf.py menuconfig
# Set: CONFIG_OPENWAKEWORD_MODEL_PATH="/spiffs/hey_naptick.tflite"
# Set: CONFIG_OPENWAKEWORD_TEST_MODE=n
idf.py build flash monitor
```

Say "Hey, Naptick" and verify detection!

## Quick Commands

```bash
# Complete workflow
./scripts/setup_training_environment.sh                    # Setup
cd training/hey_naptick && python3 generate_synthetic_data.py  # Generate data
./scripts/train_hey_naptick.sh                            # Train
# Flash model (see Step 5)
# Test (see Step 6)
```

## Need Help?

- **Full Guide**: `../../docs/TRAIN_HEY_NAPTICK_GUIDE.md`
- **Quick Start**: `../../docs/HEY_NAPTICK_TRAINING_QUICK_START.md`
- **Data Collection**: `COLLECT_DATA.md`
- **OpenWakeWord Docs**: https://github.com/dscripka/openWakeWord

Let's get started! 🚀