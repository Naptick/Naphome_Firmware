# Training "Hey, Naptick" Wake Word

This directory contains everything needed to train your "Hey, Naptick" wake word model.

## Quick Start

```bash
# 1. Setup training environment
../../scripts/setup_training_environment.sh

# 2. Generate synthetic data (quick) or collect real data (better)
python3 generate_synthetic_data.py

# 3. Train model
python3 train_hey_naptick.py

# 4. Model will be at: ../../hey_naptick.tflite
```

## Directory Structure

```
training/hey_naptick/
├── data/
│   ├── positive/          # "Hey, Naptick" samples (WAV files)
│   └── negative/          # Negative samples (other words, noise)
├── output/                # Training outputs
├── train_hey_naptick.py   # Main training script
├── generate_synthetic_data.py  # Generate TTS samples
├── COLLECT_DATA.md        # Data collection guide
└── README.md              # This file
```

## Training Data Requirements

### Positive Samples ("Hey, Naptick")
- **Format**: WAV, 16kHz, mono, 16-bit
- **Quantity**: 100-200 samples (minimum 50)
- **Variations**: Different distances, environments, speakers, tones

### Negative Samples
- **Format**: Same as positive
- **Quantity**: 200-500 samples (minimum 100)
- **Types**: Other wake words, background noise, other speech

## Training Methods

### Method 1: Synthetic Data (Fast)
```bash
python3 generate_synthetic_data.py
```
Uses TTS to generate samples quickly. Good for testing, less accurate.

### Method 2: Real Recordings (Best)
Record actual "Hey, Naptick" samples:
- Use `COLLECT_DATA.md` guide
- Record in various conditions
- Save to `data/positive/`

### Method 3: Hybrid
- Start with synthetic data
- Add real recordings
- Best of both worlds

## Training Process

1. **Collect Data** → `data/positive/` and `data/negative/`
2. **Train Model** → `python3 train_hey_naptick.py`
3. **Export TFLite** → Model saved to project root
4. **Flash to ESP32** → See main documentation

## Resources

- **Full Guide**: `../../docs/TRAIN_HEY_NAPTICK_GUIDE.md`
- **Quick Start**: `../../docs/HEY_NAPTICK_TRAINING_QUICK_START.md`
- **Data Collection**: `COLLECT_DATA.md`
- **OpenWakeWord**: https://github.com/dscripka/openWakeWord

## Expected Results

With good training data:
- **Detection Rate**: 85-95%
- **False Positive Rate**: <5%
- **Model Size**: 50-200KB (quantized)
- **Training Time**: 10-30 minutes

Good luck! 🚀