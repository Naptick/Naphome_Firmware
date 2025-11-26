
# Training "Hey, Naptick" with OpenWakeWord

## Current Status

✅ Training data ready:
   - 100 positive samples
   - 30 negative samples

## Training Options

### Option 1: Use OpenWakeWord Repository (Recommended)

1. Clone OpenWakeWord repository:
   ```bash
   git clone https://github.com/dscripka/openWakeWord.git
   cd openWakeWord
   ```

2. Copy your training data:
   ```bash
   cp -r /Users/danielmcshan/GitHub/Naphome-Firmware/training/hey_naptick/data/positive /path/to/openWakeWord/training_data/positive_hey_naptick
   cp -r /Users/danielmcshan/GitHub/Naphome-Firmware/training/hey_naptick/data/negative /path/to/openWakeWord/training_data/negative
   ```

3. Follow OpenWakeWord's training documentation:
   - See: https://github.com/dscripka/openWakeWord/blob/main/docs/TRAINING.md
   - Use their training scripts
   - Export model to TFLite

### Option 2: Fine-tune Pre-trained Model

1. Download OpenWakeWord's pre-trained model
2. Fine-tune with your "Hey, Naptick" data
3. Export to TFLite

### Option 3: Manual TensorFlow Training

Create a custom model architecture and train from scratch.

## Model Output

After training, save model as:
  /Users/danielmcshan/GitHub/Naphome-Firmware/hey_naptick.tflite

## Next Steps

1. Choose training option above
2. Train model
3. Convert to TFLite format
4. Flash to ESP32 (see main docs)
