# Training "Hey, Naptick" - Quick Reference

## Current Status

✅ **100 positive samples** (TTS-generated "Hey, Naptick")  
✅ **30 negative samples**  
✅ **Dependencies installed** (OpenWakeWord, TensorFlow, librosa)

## Important: Training Approach

**OpenWakeWord's pip package is for INFERENCE, not training.**

To train a custom wake word model, you need:

### Option 1: OpenWakeWord Repository (Recommended)

```bash
# 1. Clone repository
git clone https://github.com/dscripka/openWakeWord.git
cd openWakeWord

# 2. Copy your training data
cp -r /path/to/Naphome-Firmware/training/hey_naptick/data/positive ./training_data/positive_hey_naptick
cp -r /path/to/Naphome-Firmware/training/hey_naptick/data/negative ./training_data/negative

# 3. Follow their training guide
# See: docs/TRAINING.md in the repository
```

### Option 2: Use Pre-trained + Fine-tune

If available, fine-tune an existing model with your data.

## Your Data Location

- **Positive**: `training/hey_naptick/data/positive/`
- **Negative**: `training/hey_naptick/data/negative/`
- **Format**: 16kHz, mono, 16-bit WAV ✓

## Scripts Available

- `train_model.py` - Validates data and creates training guide
- `run_training.py` - Attempts verifier training (not main model)
- `generate_with_gtts_ffmpeg.py` - Generate more TTS samples

## Next Steps

1. **Clone OpenWakeWord repository**
2. **Copy your data** to their training structure
3. **Follow their training guide**
4. **Export to TFLite**: Save as `hey_naptick.tflite` in project root
5. **Flash to ESP32**

## Resources

- OpenWakeWord: https://github.com/dscripka/openWakeWord
- Training guide: `output/TRAINING_INSTRUCTIONS.md`
- Main docs: `docs/TRAIN_HEY_NAPTICK_GUIDE.md`

**Your data is ready - time to train!** 🚀