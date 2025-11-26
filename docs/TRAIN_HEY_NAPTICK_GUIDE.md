# Training "Hey, Naptick" Model - Complete Guide

Step-by-step guide to train your "Hey, Naptick" wake word model.

## Quick Start

```bash
# 1. Setup training environment
./scripts/setup_training_environment.sh

# 2. Collect training data (see below)

# 3. Train model
./scripts/train_hey_naptick.sh
```

## Step 1: Setup Training Environment (5 minutes)

```bash
./scripts/setup_training_environment.sh
```

This will:
- Install OpenWakeWord Python library
- Install dependencies (numpy, scipy, librosa, tensorflow)
- Create training directory structure
- Create data collection guide

## Step 2: Collect Training Data (1-2 days)

### Option A: Record Your Own (Best Quality)

Record "Hey, Naptick" in various conditions:

**Recommended Conditions:**
- **Distance**: Close (1-2 ft), Medium (3-5 ft), Far (6-10 ft)
- **Environment**: Quiet, Noisy, Outdoor
- **Speaker**: Different speakers if possible
- **Tone**: Normal, Excited, Whispered
- **Speed**: Normal, Fast, Slow

**File Requirements:**
- Format: WAV
- Sample Rate: 16kHz
- Channels: Mono
- Bit Depth: 16-bit
- Length: 1-3 seconds

**Quantity:**
- Positive samples: 100-200 (minimum 50)
- Negative samples: 200-500 (minimum 100)

**Save to:**
- Positive: `training/hey_naptick/data/positive/hey_naptick_001.wav`
- Negative: `training/hey_naptick/data/negative/negative_001.wav`

### Option B: TTS Generation (Faster)

Use text-to-speech to generate samples:

```python
# Example using gTTS (Google Text-to-Speech)
from gtts import gTTS
import os

for i in range(100):
    tts = gTTS(text="Hey, Naptick", lang='en')
    tts.save(f"training/hey_naptick/data/positive/hey_naptick_{i:03d}.wav")
```

**TTS Services:**
- Google TTS (gTTS)
- Amazon Polly
- Azure TTS
- Local TTS (espeak, festival)

**Variations:**
- Different voices
- Different speeds
- Different pitches
- Add slight noise variations

### Option C: Use OpenWakeWord's Synthetic Data

For quick testing (less accurate):

```bash
cd training/hey_naptick
python3 quick_train.py
# Follow prompts to generate synthetic data
```

## Step 3: Train the Model

### Method 1: Using Training Script

```bash
./scripts/train_hey_naptick.sh
```

The script will:
1. Check for training data
2. Validate data format
3. Train the model
4. Output: `hey_naptick.tflite`

### Method 2: Manual Training

```bash
cd training/hey_naptick
python3 quick_train.py
```

### Method 3: Using OpenWakeWord Directly

```python
from openwakeword import Model
from openwakeword.train import train_model

# Train custom model
train_model(
    positive_dir="training/hey_naptick/data/positive",
    negative_dir="training/hey_naptick/data/negative",
    output_path="hey_naptick.tflite",
    epochs=50,
    batch_size=32,
    learning_rate=0.001
)
```

## Step 4: Validate Model

After training, validate the model:

```python
import tensorflow as tf

# Load model
interpreter = tf.lite.Interpreter(model_path="hey_naptick.tflite")
interpreter.allocate_tensors()

# Check input/output shapes
input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

print(f"Input shape: {input_details[0]['shape']}")
print(f"Output shape: {output_details[0]['shape']}")
```

Expected:
- Input: `[1, 40]` (40 melspectrogram features)
- Output: `[1, 2]` (binary classification) or `[1, 1]` (single output)

## Step 5: Optimize for ESP32

### Quantization

Quantize the model for smaller size:

```python
import tensorflow as tf

# Load model
converter = tf.lite.TFLiteConverter.from_saved_model("model_dir")
converter.optimizations = [tf.lite.Optimize.DEFAULT]

# Convert to quantized TFLite
tflite_quant_model = converter.convert()

# Save
with open("hey_naptick_quantized.tflite", "wb") as f:
    f.write(tflite_quant_model)
```

### Model Size

Target sizes:
- **Small**: <50KB (quantized, minimal features)
- **Medium**: 50-100KB (quantized, standard)
- **Large**: 100-200KB (full precision)

For ESP32-S3, aim for <100KB if possible.

## Step 6: Flash Model to Device

```bash
# Flash to SPIFFS partition
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 \
    --port /dev/ttyUSB0 \
    write_flash 0x140000 hey_naptick.tflite

# Or flash to raw partition
# Adjust offset based on your partition table
```

## Step 7: Test on Device

```bash
cd samples/korvo_openwakeword_demo
idf.py menuconfig
# Set: CONFIG_OPENWAKEWORD_MODEL_PATH="/spiffs/hey_naptick.tflite"
# Set: CONFIG_OPENWAKEWORD_TEST_MODE=n
idf.py build flash monitor
```

Test scenarios:
- Say "Hey, Naptick" clearly
- Say similar phrases (shouldn't trigger)
- Test in different environments
- Test at different distances
- Adjust threshold if needed

## Troubleshooting

### Training Fails

**Issue**: "No training data found"
- **Solution**: Collect data first (see Step 2)

**Issue**: "Model too large"
- **Solution**: Use quantization, reduce model complexity

**Issue**: "Training takes too long"
- **Solution**: Reduce epochs, use smaller dataset for testing

### Model Doesn't Work

**Issue**: "Model not loading"
- **Solution**: Check file path, verify SPIFFS mounted, check partition table

**Issue**: "No detections"
- **Solution**: Lower threshold, check audio input, verify model trained correctly

**Issue**: "Too many false positives"
- **Solution**: Raise threshold, retrain with more negative samples

### Poor Accuracy

**Issue**: Low detection rate
- **Solution**: 
  - Collect more training data
  - Use more diverse conditions
  - Retrain with better data
  - Lower threshold

**Issue**: High false positive rate
- **Solution**:
  - Add more negative samples
  - Retrain with balanced dataset
  - Raise threshold
  - Improve training data quality

## Training Tips

### Data Quality
- **Clear audio**: Avoid clipping, background noise
- **Consistent format**: All files same sample rate/format
- **Diverse conditions**: Different environments, speakers, distances
- **Balanced dataset**: Similar number of positive/negative samples

### Training Parameters
- **Epochs**: Start with 50, increase if underfitting
- **Batch size**: 32 is good starting point
- **Learning rate**: 0.001 is standard, adjust if needed
- **Validation split**: Use 20% for validation

### Model Optimization
- **Quantization**: Always quantize for ESP32
- **Feature reduction**: Can reduce mel bands if needed
- **Model architecture**: Start simple, add complexity if needed

## Expected Results

With good training data:
- **Detection rate**: 85-95%
- **False positive rate**: <5%
- **Model size**: 50-200KB (quantized)
- **Latency**: 80-90ms per frame

## Next Steps

After training:
1. **Test thoroughly** - Various conditions
2. **Tune threshold** - Based on false positive rate
3. **Optimize model** - Quantization, size reduction
4. **Deploy** - Flash to device, monitor performance
5. **Iterate** - Collect more data, retrain if needed

## Resources

- **OpenWakeWord**: https://github.com/dscripka/openWakeWord
- **Training Data Guide**: `training/hey_naptick/COLLECT_DATA.md`
- **Training Script**: `training/hey_naptick/quick_train.py`
- **General Training Guide**: `docs/openwakeword_training_guide.md`

## Quick Reference

```bash
# Setup
./scripts/setup_training_environment.sh

# Collect data (manual)
# Record samples → training/hey_naptick/data/

# Train
./scripts/train_hey_naptick.sh

# Flash
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 --port /dev/ttyUSB0 \
    write_flash 0x140000 hey_naptick.tflite

# Test
cd samples/korvo_openwakeword_demo
idf.py build flash monitor
```

Good luck with training! 🚀