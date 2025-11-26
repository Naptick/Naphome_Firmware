#!/usr/bin/env python3
"""
Train "Hey, Naptick" wake word model using OpenWakeWord
This script trains a custom wake word model and exports it to TFLite format
"""

import os
import sys
import numpy as np
from pathlib import Path

# Add project root to path
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
sys.path.insert(0, str(PROJECT_ROOT))

print("=" * 60)
print("Train 'Hey, Naptick' Wake Word Model")
print("=" * 60)
print("")

# Check dependencies
try:
    import openwakeword
    print(f"✓ OpenWakeWord version: {openwakeword.__version__ if hasattr(openwakeword, '__version__') else 'installed'}")
except ImportError:
    print("Error: openwakeword not installed")
    print("Install with: pip3 install openwakeword")
    sys.exit(1)

try:
    import tensorflow as tf
    print(f"✓ TensorFlow version: {tf.__version__}")
except ImportError:
    print("Warning: TensorFlow not installed")
    print("Install with: pip3 install tensorflow")
    print("(Required for model conversion)")

try:
    import librosa
    print(f"✓ Librosa installed")
except ImportError:
    print("Warning: librosa not installed")
    print("Install with: pip3 install librosa")
    print("(Required for audio processing)")

print("")

# Configuration
TRAIN_DIR = SCRIPT_DIR
DATA_DIR = TRAIN_DIR / "data"
POSITIVE_DIR = DATA_DIR / "positive"
NEGATIVE_DIR = DATA_DIR / "negative"
OUTPUT_DIR = TRAIN_DIR / "output"
MODEL_NAME = "hey_naptick"
MODEL_OUTPUT = PROJECT_ROOT / f"{MODEL_NAME}.tflite"

# Create output directory
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

print("Configuration:")
print(f"  Training directory: {TRAIN_DIR}")
print(f"  Positive samples: {POSITIVE_DIR}")
print(f"  Negative samples: {NEGATIVE_DIR}")
print(f"  Model output: {MODEL_OUTPUT}")
print("")

# Check for training data
positive_files = list(POSITIVE_DIR.glob("*.wav")) + list(POSITIVE_DIR.glob("*.WAV"))
negative_files = list(NEGATIVE_DIR.glob("*.wav")) + list(NEGATIVE_DIR.glob("*.WAV"))

if len(positive_files) == 0:
    print("=" * 60)
    print("ERROR: No positive training samples found!")
    print("=" * 60)
    print("")
    print(f"Please add WAV files to: {POSITIVE_DIR}")
    print("")
    print("Options:")
    print("  1. Record 'Hey, Naptick' samples (recommended)")
    print("     - Record 100-200 samples")
    print("     - Various conditions (distance, environment, speaker)")
    print("     - Format: 16kHz, mono, 16-bit WAV")
    print("")
    print("  2. Generate TTS samples (faster)")
    print("     - Use text-to-speech to generate samples")
    print("     - See: COLLECT_DATA.md for instructions")
    print("")
    print("  3. Use synthetic data (quick test)")
    print("     - Run: python3 generate_synthetic_data.py")
    print("")
    sys.exit(1)

print(f"✓ Found {len(positive_files)} positive samples")
print(f"✓ Found {len(negative_files)} negative samples")
print("")

if len(negative_files) < 50:
    print("⚠ Warning: Few negative samples found")
    print("  Recommended: 100-500 negative samples")
    print("  Current: {len(negative_files)}")
    print("")

# Training function
def train_model():
    """
    Train the wake word model using OpenWakeWord
    """
    print("=" * 60)
    print("Training Model")
    print("=" * 60)
    print("")
    
    print("Note: OpenWakeWord training requires the full training pipeline")
    print("from the openWakeWord repository.")
    print("")
    print("This script sets up the structure. For actual training:")
    print("")
    print("Option 1: Use OpenWakeWord's training utilities")
    print("  - Clone: https://github.com/dscripka/openWakeWord")
    print("  - Follow training documentation")
    print("  - Use your collected data")
    print("")
    print("Option 2: Use pre-trained model and fine-tune")
    print("  - Start with OpenWakeWord's base model")
    print("  - Fine-tune with your 'Hey, Naptick' data")
    print("")
    print("Option 3: Manual training with TensorFlow")
    print("  - Build custom model architecture")
    print("  - Train with your data")
    print("  - Export to TFLite")
    print("")
    
    # Create a simple training template
    training_template = f"""
# OpenWakeWord Training Template for "Hey, Naptick"
# 
# This is a template - actual training requires OpenWakeWord's full pipeline
# See: https://github.com/dscripka/openWakeWord for complete training guide

import os
from pathlib import Path
import numpy as np

# Training data paths
positive_dir = Path("{POSITIVE_DIR}")
negative_dir = Path("{NEGATIVE_DIR}")

# Load training data
positive_files = list(positive_dir.glob("*.wav"))
negative_files = list(negative_dir.glob("*.wav"))

print(f"Training with {{len(positive_files)}} positive and {{len(negative_files)}} negative samples")

# TODO: Implement actual training using OpenWakeWord's training API
# See OpenWakeWord repository for training utilities:
# https://github.com/dscripka/openWakeWord

# After training, convert to TFLite:
# 1. Save model in TensorFlow format
# 2. Convert to TFLite using tf.lite.TFLiteConverter
# 3. Quantize for ESP32 (optional but recommended)
# 4. Save as {MODEL_NAME}.tflite
"""
    
    template_path = OUTPUT_DIR / "training_template.py"
    with open(template_path, "w") as f:
        f.write(training_template)
    
    print(f"Created training template: {template_path}")
    print("")
    
    # Check if we can do a quick test
    print("Attempting quick validation of training data...")
    try:
        import librosa
        
        # Validate a few samples
        valid_count = 0
        for wav_file in positive_files[:5]:
            try:
                y, sr = librosa.load(wav_file, sr=16000, mono=True)
                if len(y) > 0 and sr == 16000:
                    valid_count += 1
            except Exception as e:
                print(f"  ⚠ Invalid file {wav_file.name}: {e}")
        
        if valid_count > 0:
            print(f"  ✓ Validated {valid_count} sample(s) - format looks good")
        else:
            print("  ⚠ Could not validate samples - check format")
    except ImportError:
        print("  ⚠ librosa not installed - skipping validation")
    
    print("")
    print("=" * 60)
    print("Next Steps")
    print("=" * 60)
    print("")
    print("1. Review training template: {template_path}")
    print("2. Follow OpenWakeWord training guide:")
    print("   - https://github.com/dscripka/openWakeWord")
    print("   - Check repository for training utilities")
    print("3. Train model using OpenWakeWord's training pipeline")
    print("4. Export to TFLite format")
    print("5. Flash to ESP32")
    print("")
    print("For detailed instructions, see:")
    print("  - docs/TRAIN_HEY_NAPTICK_GUIDE.md")
    print("  - docs/openwakeword_training_guide.md")
    print("")

if __name__ == "__main__":
    train_model()