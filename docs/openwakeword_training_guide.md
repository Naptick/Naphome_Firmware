# Training "Hey, Naptick" with OpenWakeWord

## Overview

This guide explains how to train a custom "Hey, Naptick" wake word model using OpenWakeWord's Python library, then deploy it to ESP32.

## Prerequisites

```bash
pip install openwakeword
pip install torch torchaudio  # For training
pip install tensorflow  # For model conversion
```

## Training Methods

### Method 1: TTS-Based Training (Recommended)

OpenWakeWord supports training with synthetic TTS data, similar to Espressif's approach.

```python
from openwakeword import Model
import openwakeword.utils as utils

# Generate TTS samples for "Hey, Naptick"
# You can use:
# - Google TTS
# - Amazon Polly
# - Azure TTS
# - Or any TTS service

# Training script
def train_hey_naptick():
    # 1. Generate TTS samples
    wake_word = "hey naptick"
    num_samples = 1000  # Generate 1000 variations
    
    # 2. Train model using OpenWakeWord's training API
    # See OpenWakeWord training documentation
    pass
```

### Method 2: Record Your Own Samples

If you want to record real audio:

```python
# Record requirements:
# - 16 kHz, 16-bit, mono WAV
# - At least 100-200 samples
# - Various speakers, distances, speeds
# - Quiet environment recommended

# Training with recorded samples
def train_with_recordings():
    training_clips = [
        "recordings/hey_naptick_001.wav",
        "recordings/hey_naptick_002.wav",
        # ... more samples
    ]
    
    # Use OpenWakeWord training API
    pass
```

## Training Script Template

Create `train_hey_naptick.py`:

```python
#!/usr/bin/env python3
"""
Train "Hey, Naptick" wake word model using OpenWakeWord
"""

import openwakeword
from openwakeword.model import Model
import numpy as np

def generate_tts_samples():
    """Generate TTS samples for training"""
    # TODO: Implement TTS generation
    # Options:
    # 1. Use gTTS (Google Text-to-Speech)
    # 2. Use pyttsx3 (offline)
    # 3. Use cloud TTS services
    pass

def train_model():
    """Train the wake word model"""
    wake_word = "hey naptick"
    
    # Generate training data
    print(f"Generating TTS samples for '{wake_word}'...")
    training_clips = generate_tts_samples()
    
    # Train using OpenWakeWord
    # Note: Check OpenWakeWord's latest training API
    print("Training model...")
    # model = train_wake_word_model(training_clips, wake_word)
    
    # Export to TFLite
    print("Exporting to TFLite...")
    # model.export_tflite("hey_naptick.tflite")
    
    print("Training complete! Model saved as hey_naptick.tflite")

if __name__ == "__main__":
    train_model()
```

## Model Optimization for ESP32

After training, optimize the model for ESP32:

```python
import tensorflow as tf

# Load trained model
model = tf.lite.TFLiteConverter.from_saved_model("hey_naptick_model")

# Quantize for smaller size
converter = tf.lite.TFLiteConverter.from_saved_model("hey_naptick_model")
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.target_spec.supported_types = [tf.float16]  # or tf.int8

# Convert
tflite_model = converter.convert()

# Save
with open("hey_naptick_quantized.tflite", "wb") as f:
    f.write(tflite_model)
```

## Model Size Targets

- **Target**: < 100 KB for ESP32-S3
- **Acceptable**: < 200 KB
- **Too large**: > 300 KB (may not fit in partition)

## Deployment

1. **Flash model to ESP32**:
   ```bash
   python $IDF_PATH/components/esptool_py/esptool/esptool.py \
       --chip esp32s3 \
       --port /dev/ttyUSB0 \
       write_flash 0x140000 hey_naptick.tflite
   ```

2. **Or use SPIFFS**:
   ```bash
   # Add model to SPIFFS image
   # Flash SPIFFS partition
   ```

## Testing the Model

Test locally before deploying:

```python
from openwakeword import Model
import soundfile as sf

# Load model
model = Model(wakeword_models=["hey_naptick.tflite"])

# Test with audio file
audio, sr = sf.read("test_hey_naptick.wav")
prediction = model.predict(audio)

print(f"Detection: {prediction}")
```

## Resources

- [OpenWakeWord GitHub](https://github.com/dscripka/openWakeWord)
- [OpenWakeWord Training Docs](https://github.com/dscripka/openWakeWord/blob/main/docs/TRAINING.md)
- [TensorFlow Lite Micro](https://www.tensorflow.org/lite/microcontrollers)

## Alternative: Use Espressif Training

While implementing this port, you can request Espressif to train "Hey, Naptick":

```bash
./scripts/request_wake_word.sh
```

This provides a professionally-trained model in 2-4 weeks.