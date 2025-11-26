#!/usr/bin/env python3
"""
Train "Hey, Naptick" wake word model
Uses OpenWakeWord's training capabilities
"""

import os
import sys
from pathlib import Path
import numpy as np

# Fix Python path
import site
site_packages = site.getsitepackages()
user_site = site.getusersitepackages()
for path in site_packages + [user_site]:
    if path and path not in sys.path:
        sys.path.insert(0, path)

SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent

print("=" * 60)
print("Train 'Hey, Naptick' Wake Word Model")
print("=" * 60)
print("")

# Check dependencies
try:
    import openwakeword
    print("✓ OpenWakeWord available")
except ImportError:
    print("Error: openwakeword not installed")
    print("Install: python3 -m pip install openwakeword --break-system-packages")
    sys.exit(1)

try:
    import tensorflow as tf
    print(f"✓ TensorFlow {tf.__version__}")
except ImportError:
    print("⚠ TensorFlow not installed (needed for TFLite conversion)")
    print("Install: python3 -m pip install tensorflow --break-system-packages")

try:
    import librosa
    print("✓ librosa available")
except ImportError:
    print("⚠ librosa not installed (needed for audio processing)")
    print("Install: python3 -m pip install librosa --break-system-packages")

print("")

# Configuration
DATA_DIR = SCRIPT_DIR / "data"
POSITIVE_DIR = DATA_DIR / "positive"
NEGATIVE_DIR = DATA_DIR / "negative"
OUTPUT_DIR = SCRIPT_DIR / "output"
MODEL_OUTPUT = PROJECT_ROOT / "hey_naptick.tflite"

OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# Check training data
positive_files = sorted(list(POSITIVE_DIR.glob("*.wav")) + list(POSITIVE_DIR.glob("*.WAV")))
negative_files = sorted(list(NEGATIVE_DIR.glob("*.wav")) + list(NEGATIVE_DIR.glob("*.WAV")))

# Filter out placeholder files (small size)
real_positive = [f for f in positive_files if f.stat().st_size > 40000]
real_negative = [f for f in negative_files if f.stat().st_size > 40000]

print(f"Training data:")
print(f"  Positive samples: {len(real_positive)} (total: {len(positive_files)})")
print(f"  Negative samples: {len(real_negative)} (total: {len(negative_files)})")
print("")

if len(real_positive) < 50:
    print("⚠ Warning: Few real positive samples found")
    print("  Recommended: 100-200 samples")
    print("  Current: {len(real_positive)}")
    print("")
    print("  Note: Placeholder files (63KB) are being filtered out")
    print("  Wait for TTS generation to complete or record real samples")
    print("")

if len(real_positive) == 0:
    print("Error: No valid training data found")
    print("")
    print("Options:")
    print("  1. Wait for TTS generation to complete")
    print("  2. Record real samples (see COLLECT_DATA.md)")
    print("  3. Use existing placeholder files (not recommended)")
    sys.exit(1)

# Use all files if we have real data, otherwise use what we have
use_positive = real_positive if real_positive else positive_files[:100]
use_negative = real_negative if real_negative else negative_files[:200]

print(f"Using {len(use_positive)} positive and {len(use_negative)} negative samples")
print("")

# OpenWakeWord training approach
print("=" * 60)
print("Training Approach")
print("=" * 60)
print("")
print("OpenWakeWord's Python library is primarily for inference.")
print("For training, you need to use the full OpenWakeWord repository.")
print("")
print("However, we can:")
print("  1. Validate your training data")
print("  2. Prepare data for training")
print("  3. Create a training script template")
print("  4. Guide you through the training process")
print("")

# Validate audio files
print("Validating training data...")
try:
    import librosa
    
    valid_positive = 0
    valid_negative = 0
    
    for wav_file in use_positive[:10]:  # Check first 10
        try:
            y, sr = librosa.load(wav_file, sr=16000, mono=True, duration=3.0)
            if len(y) > 0 and sr == 16000:
                valid_positive += 1
        except Exception as e:
            print(f"  ⚠ Invalid: {wav_file.name} - {e}")
    
    for wav_file in use_negative[:10]:
        try:
            y, sr = librosa.load(wav_file, sr=16000, mono=True, duration=3.0)
            if len(y) > 0 and sr == 16000:
                valid_negative += 1
        except Exception as e:
            print(f"  ⚠ Invalid: {wav_file.name} - {e}")
    
    print(f"  ✓ Validated {valid_positive}/{min(10, len(use_positive))} positive samples")
    print(f"  ✓ Validated {valid_negative}/{min(10, len(use_negative))} negative samples")
    
except ImportError:
    print("  ⚠ librosa not available - skipping validation")

print("")

# Create training instructions
training_guide = f"""
# Training "Hey, Naptick" with OpenWakeWord

## Current Status

✅ Training data ready:
   - {len(use_positive)} positive samples
   - {len(use_negative)} negative samples

## Training Options

### Option 1: Use OpenWakeWord Repository (Recommended)

1. Clone OpenWakeWord repository:
   ```bash
   git clone https://github.com/dscripka/openWakeWord.git
   cd openWakeWord
   ```

2. Copy your training data:
   ```bash
   cp -r {DATA_DIR}/positive /path/to/openWakeWord/training_data/positive_hey_naptick
   cp -r {DATA_DIR}/negative /path/to/openWakeWord/training_data/negative
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
  {MODEL_OUTPUT}

## Next Steps

1. Choose training option above
2. Train model
3. Convert to TFLite format
4. Flash to ESP32 (see main docs)
"""

guide_path = OUTPUT_DIR / "TRAINING_INSTRUCTIONS.md"
with open(guide_path, "w") as f:
    f.write(training_guide)

print(f"✓ Created training guide: {guide_path}")
print("")

# Create a simple data preparation script
data_prep_script = f"""#!/usr/bin/env python3
\"\"\"
Prepare training data for OpenWakeWord
\"\"\"

import os
from pathlib import Path

# Data paths
positive_dir = Path("{POSITIVE_DIR}")
negative_dir = Path("{NEGATIVE_DIR}")

# List all files
positive_files = sorted(positive_dir.glob("*.wav"))
negative_files = sorted(negative_dir.glob("*.wav"))

print(f"Positive samples: {{len(positive_files)}}")
print(f"Negative samples: {{len(negative_files)}}")

# Create file lists for training
with open("positive_files.txt", "w") as f:
    for file in positive_files:
        f.write(f"{{file}}\\n")

with open("negative_files.txt", "w") as f:
    for file in negative_files:
        f.write(f"{{file}}\\n")

print("Created file lists: positive_files.txt, negative_files.txt")
"""

prep_script_path = OUTPUT_DIR / "prepare_data.py"
with open(prep_script_path, "w") as f:
    f.write(data_prep_script)
os.chmod(prep_script_path, 0o755)

print(f"✓ Created data preparation script: {prep_script_path}")
print("")

print("=" * 60)
print("Summary")
print("=" * 60)
print("")
print("Training data is ready!")
print("")
print("Next steps:")
print("  1. Review: {guide_path}")
print("  2. Follow OpenWakeWord training guide")
print("  3. Train your model")
print("  4. Export to TFLite: {MODEL_OUTPUT}")
print("")
print("For quick start, see:")
print("  - docs/TRAIN_HEY_NAPTICK_GUIDE.md")
print("  - https://github.com/dscripka/openWakeWord")
print("")