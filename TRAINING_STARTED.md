# "Hey, Naptick" Training - Setup Complete! ✅

## What's Been Done

### ✅ Environment Setup
- **OpenWakeWord**: Installed (v0.6.0)
- **Dependencies**: Installed (librosa, tensorflow, numpy, scipy)
- **Training Directory**: Created at `training/hey_naptick/`
- **esp-tflite-micro**: Added as git submodule

### ✅ Training Infrastructure
- Training scripts created
- Data collection guides
- Synthetic data generator
- Training templates

## Current Status

**Ready to generate training data!**

Training data directories:
- `training/hey_naptick/data/positive/` - Empty (ready for "Hey, Naptick" samples)
- `training/hey_naptick/data/negative/` - Empty (ready for negative samples)

## Next Step: Generate Training Data

### Option 1: Generate Synthetic Data (Fastest - 30-60 min)

**Install TTS library:**
```bash
pip3 install --user gtts pydub
# OR for offline TTS:
pip3 install --user pyttsx3
```

**Generate data:**
```bash
cd training/hey_naptick
python3 generate_synthetic_data.py
```

This will:
- Generate 200 "Hey, Naptick" samples using TTS
- Generate 300 negative samples
- Save to `data/positive/` and `data/negative/`

### Option 2: Record Real Samples (Better Quality - 1-2 days)

Record "Hey, Naptick" in various conditions:
- Different distances
- Different environments  
- Different speakers
- Different tones

**Requirements:**
- Format: WAV, 16kHz, mono, 16-bit
- Quantity: 100-200 positive, 200-500 negative
- Save to: `training/hey_naptick/data/positive/` and `data/negative/`

**See**: `training/hey_naptick/COLLECT_DATA.md` for detailed guide

### Option 3: Use Quick Start Script

```bash
cd training/hey_naptick
./quick_start_training.sh
```

This will guide you through the process.

## After Generating Data: Train Model

Once you have training data:

```bash
cd training/hey_naptick
python3 train_hey_naptick.py
```

**Note**: Full training requires OpenWakeWord's training pipeline. The script will guide you to the OpenWakeWord repository for complete training instructions.

## Expected Timeline

- **Synthetic Data Generation**: 30-60 minutes
- **Model Training**: 10-30 minutes
- **Total**: ~1-2 hours (with synthetic data)

- **Real Data Collection**: 1-2 days
- **Model Training**: 10-30 minutes  
- **Total**: ~1-3 days (with real data)

## Quick Commands

```bash
# Generate synthetic data
cd training/hey_naptick
pip3 install --user gtts pydub
python3 generate_synthetic_data.py

# Train model
python3 train_hey_naptick.py

# Model will be at: ../../hey_naptick.tflite
```

## Resources

- **Full Training Guide**: `docs/TRAIN_HEY_NAPTICK_GUIDE.md`
- **Quick Start**: `docs/HEY_NAPTICK_TRAINING_QUICK_START.md`
- **Data Collection**: `training/hey_naptick/COLLECT_DATA.md`
- **OpenWakeWord**: https://github.com/dscripka/openWakeWord

## Status Summary

✅ **Setup**: Complete  
✅ **Environment**: Ready  
🚧 **Training Data**: Need to generate/collect  
⏳ **Model Training**: Waiting for data  

**You're ready to generate training data!** 🚀