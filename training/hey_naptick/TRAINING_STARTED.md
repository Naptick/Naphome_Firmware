# Training Status: Ready to Train! 🚀

## Current Status

✅ **Training data prepared:**
   - 100 real positive samples (TTS-generated "Hey, Naptick")
   - 30 real negative samples
   - Total: 130 validated samples ready for training

✅ **Dependencies installed:**
   - OpenWakeWord ✓
   - TensorFlow ✓
   - librosa ✓

## Important Note

OpenWakeWord's Python library (`pip install openwakeword`) is primarily for **inference** (using pre-trained models), not training.

For **training a custom wake word**, you need to:

### Option 1: Use OpenWakeWord Repository (Recommended)

1. **Clone the repository:**
   ```bash
   git clone https://github.com/dscripka/openWakeWord.git
   cd openWakeWord
   ```

2. **Copy your training data:**
   ```bash
   cp -r /Users/danielmcshan/GitHub/Naphome-Firmware/training/hey_naptick/data/positive /path/to/openWakeWord/training_data/positive_hey_naptick
   cp -r /Users/danielmcshan/GitHub/Naphome-Firmware/training/hey_naptick/data/negative /path/to/openWakeWord/training_data/negative
   ```

3. **Follow OpenWakeWord's training guide:**
   - See: https://github.com/dscripka/openWakeWord/blob/main/docs/TRAINING.md
   - Or check their README for training instructions

### Option 2: Use Pre-trained Model + Fine-tuning

If OpenWakeWord supports fine-tuning, you can:
1. Start with their base model
2. Fine-tune with your "Hey, Naptick" data
3. Export to TFLite

### Option 3: Manual Training

Build a custom TensorFlow model and train from scratch.

## Your Training Data

- **Location**: `training/hey_naptick/data/`
- **Positive**: `data/positive/hey_naptick_*.wav` (100 real files)
- **Negative**: `data/negative/negative_*.wav` (30 real files)
- **Format**: 16kHz, mono, 16-bit WAV ✓

## Next Steps

1. **Choose training method** (Option 1 recommended)
2. **Train your model**
3. **Export to TFLite** format
4. **Save as**: `hey_naptick.tflite` in project root
5. **Flash to ESP32** (see main docs)

## Quick Links

- Training guide: `output/TRAINING_INSTRUCTIONS.md`
- Data prep script: `output/prepare_data.py`
- Main docs: `docs/TRAIN_HEY_NAPTICK_GUIDE.md`
- OpenWakeWord repo: https://github.com/dscripka/openWakeWord

## Status Summary

✅ Data ready (100 positive + 30 negative)  
✅ Dependencies installed  
⏳ Training pending (need OpenWakeWord repo)  
⏳ Model export pending  

**You're ready to train!** Follow Option 1 above to get started.