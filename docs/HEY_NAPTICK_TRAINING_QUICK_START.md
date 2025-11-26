# Train "Hey, Naptick" - Quick Start

**Fastest path to a trained "Hey, Naptick" model**

## 3-Step Process

### Step 1: Setup (2 minutes)
```bash
./scripts/setup_training_environment.sh
```

### Step 2: Collect Data (1-2 days)
```bash
# Option A: Record yourself
# Record "Hey, Naptick" 100+ times in various conditions
# Save to: training/hey_naptick/data/positive/

# Option B: Use TTS (faster)
# Generate samples using text-to-speech
# See: training/hey_naptick/COLLECT_DATA.md
```

### Step 3: Train (10-30 minutes)
```bash
./scripts/train_hey_naptick.sh
# Output: hey_naptick.tflite
```

## That's It!

Your model will be at: `hey_naptick.tflite`

Flash it and test:
```bash
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 --port /dev/ttyUSB0 \
    write_flash 0x140000 hey_naptick.tflite
```

## Need More Details?

- **Full guide**: [TRAIN_HEY_NAPTICK_GUIDE.md](TRAIN_HEY_NAPTICK_GUIDE.md)
- **Data collection**: `training/hey_naptick/COLLECT_DATA.md`
- **General training**: [openwakeword_training_guide.md](openwakeword_training_guide.md)