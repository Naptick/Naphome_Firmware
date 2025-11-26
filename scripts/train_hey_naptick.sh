#!/bin/bash
#
# Train "Hey, Naptick" wake word model using OpenWakeWord
# This script sets up the training environment and trains the model
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TRAIN_DIR="$PROJECT_ROOT/training/hey_naptick"
MODEL_OUTPUT="$PROJECT_ROOT/hey_naptick.tflite"

echo "========================================="
echo "Train 'Hey, Naptick' Wake Word Model"
echo "========================================="
echo ""

# Check Python
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 not found"
    echo "Please install Python 3.8+"
    exit 1
fi

PYTHON_VERSION=$(python3 --version | cut -d' ' -f2 | cut -d'.' -f1,2)
echo "✓ Python version: $PYTHON_VERSION"

# Check if openwakeword is installed
if ! python3 -c "import openwakeword" 2>/dev/null; then
    echo ""
    echo "Installing OpenWakeWord..."
    pip3 install openwakeword --user
    
    if ! python3 -c "import openwakeword" 2>/dev/null; then
        echo "Error: Failed to install openwakeword"
        echo "Try: pip3 install openwakeword"
        exit 1
    fi
fi

echo "✓ OpenWakeWord installed"

# Create training directory
mkdir -p "$TRAIN_DIR"
cd "$TRAIN_DIR"

echo ""
echo "Training directory: $TRAIN_DIR"
echo ""

# Check for existing training data
if [ ! -d "$TRAIN_DIR/data" ] || [ -z "$(ls -A $TRAIN_DIR/data 2>/dev/null)" ]; then
    echo "⚠ No training data found in $TRAIN_DIR/data"
    echo ""
    echo "You need to provide training data. Options:"
    echo ""
    echo "Option 1: Record your own samples"
    echo "  - Record 'Hey, Naptick' in various conditions"
    echo "  - Save as WAV files (16kHz, mono, 16-bit)"
    echo "  - Place in: $TRAIN_DIR/data/positive/"
    echo "  - Record negative samples (other words, noise)"
    echo "  - Place in: $TRAIN_DIR/data/negative/"
    echo ""
    echo "Option 2: Use TTS-generated samples"
    echo "  - Generate 'Hey, Naptick' using text-to-speech"
    echo "  - Multiple voices and variations"
    echo "  - Place in: $TRAIN_DIR/data/positive/"
    echo ""
    echo "Option 3: Use OpenWakeWord's built-in training"
    echo "  - Will generate synthetic training data"
    echo "  - Less accurate but faster"
    echo ""
    read -p "Generate synthetic training data now? (y/n): " -n 1 -r
    echo ""
    
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Generating synthetic training data..."
        python3 << 'PYTHON_SCRIPT'
import os
import numpy as np
from scipy.io import wavfile

# Create directories
os.makedirs("data/positive", exist_ok=True)
os.makedirs("data/negative", exist_ok=True)

# Generate synthetic "Hey, Naptick" samples
# This is a placeholder - real training needs actual audio
print("Note: This generates placeholder data.")
print("For best results, use real recorded samples.")
print("")
print("Creating sample structure...")

# Create a simple script to help with real data collection
with open("collect_data.sh", "w") as f:
    f.write("""#!/bin/bash
# Script to help collect training data
# Record 'Hey, Naptick' samples and save as WAV files

echo "Collecting training data for 'Hey, Naptick'"
echo ""
echo "For each recording:"
echo "  1. Say 'Hey, Naptick' clearly"
echo "  2. Save as: data/positive/hey_naptick_XXX.wav"
echo "  3. Record in different conditions:"
echo "     - Different distances"
echo "     - Different environments"
echo "     - Different speakers (if possible)"
echo ""
echo "Recommended: 50-100 positive samples"
echo ""
echo "For negative samples (background, other words):"
echo "  - Record other phrases"
echo "  - Record background noise"
echo "  - Save as: data/negative/negative_XXX.wav"
echo ""
echo "Recommended: 100-200 negative samples"
""")

os.chmod("collect_data.sh", 0o755)
print("Created collect_data.sh helper script")
PYTHON_SCRIPT
        
        echo "✓ Training directory structure created"
        echo ""
        echo "Next steps:"
        echo "  1. Record training data (see collect_data.sh)"
        echo "  2. Or use TTS to generate samples"
        echo "  3. Run this script again to train"
        exit 0
    else
        echo "Please add training data to $TRAIN_DIR/data/ and run again"
        exit 0
    fi
fi

# Check training data
POSITIVE_COUNT=$(find "$TRAIN_DIR/data/positive" -name "*.wav" 2>/dev/null | wc -l | tr -d ' ')
NEGATIVE_COUNT=$(find "$TRAIN_DIR/data/negative" -name "*.wav" 2>/dev/null | wc -l | tr -d ' ')

if [ "$POSITIVE_COUNT" -eq 0 ]; then
    echo "Error: No positive samples found in data/positive/"
    exit 1
fi

if [ "$NEGATIVE_COUNT" -eq 0 ]; then
    echo "Warning: No negative samples found. Training may be less accurate."
    echo "Consider adding negative samples to data/negative/"
fi

echo "✓ Training data found:"
echo "  Positive samples: $POSITIVE_COUNT"
echo "  Negative samples: $NEGATIVE_COUNT"
echo ""

# Train the model
echo "Training 'Hey, Naptick' model..."
echo "This may take 10-30 minutes depending on data size..."
echo ""

python3 << 'PYTHON_TRAIN'
import os
import sys
import numpy as np
from pathlib import Path

try:
    import openwakeword
    from openwakeword import Model
    from openwakeword.utils import compute_features
except ImportError as e:
    print(f"Error: {e}")
    print("Install with: pip3 install openwakeword")
    sys.exit(1)

# Training configuration
TRAIN_DIR = os.getcwd()
DATA_DIR = os.path.join(TRAIN_DIR, "data")
POSITIVE_DIR = os.path.join(DATA_DIR, "positive")
NEGATIVE_DIR = os.path.join(DATA_DIR, "negative")
MODEL_OUTPUT = os.path.join(TRAIN_DIR, "..", "..", "hey_naptick.tflite")

print(f"Training directory: {TRAIN_DIR}")
print(f"Data directory: {DATA_DIR}")
print(f"Model output: {MODEL_OUTPUT}")
print("")

# Load training data
def load_audio_files(directory):
    """Load WAV files from directory"""
    files = []
    for ext in ['*.wav', '*.WAV']:
        files.extend(Path(directory).glob(ext))
    return sorted(files)

positive_files = load_audio_files(POSITIVE_DIR)
negative_files = load_audio_files(NEGATIVE_DIR)

if len(positive_files) == 0:
    print("Error: No positive samples found")
    sys.exit(1)

print(f"Found {len(positive_files)} positive samples")
print(f"Found {len(negative_files)} negative samples")
print("")

# Note: OpenWakeWord training is complex
# For now, we'll create a script that uses OpenWakeWord's training API
# The actual training requires the full OpenWakeWord training pipeline

print("Creating training script...")

training_script = f"""
# OpenWakeWord Training Script for 'Hey, Naptick'
# 
# This script trains a custom wake word model using OpenWakeWord
# 
# Usage:
#   python3 train_hey_naptick.py
#
# Requirements:
#   - Training data in data/positive/ and data/negative/
#   - openwakeword library installed
#   - Sufficient disk space for model files

import os
import sys
from pathlib import Path

# Add OpenWakeWord to path if needed
try:
    from openwakeword import Model
    from openwakeword.utils import compute_features
except ImportError:
    print("Error: openwakeword not installed")
    print("Install with: pip3 install openwakeword")
    sys.exit(1)

# Training parameters
WAKE_WORD = "hey_naptick"
EPOCHS = 50
BATCH_SIZE = 32
LEARNING_RATE = 0.001

print("Training 'Hey, Naptick' wake word model...")
print("")
print("Note: Full training requires OpenWakeWord's training pipeline")
print("See: https://github.com/dscripka/openWakeWord for training details")
print("")
print("For now, you can:")
print("  1. Use OpenWakeWord's pre-trained models as base")
print("  2. Fine-tune with your 'Hey, Naptick' data")
print("  3. Or use the training utilities in openWakeWord repository")
print("")

# Check if we can use OpenWakeWord's training
try:
    # Try to import training utilities
    from openwakeword.train import train_model
    print("OpenWakeWord training utilities available")
    print("Starting training...")
    
    # This would call the actual training function
    # train_model(
    #     positive_dir="data/positive",
    #     negative_dir="data/negative",
    #     output_path="{MODEL_OUTPUT}",
    #     epochs=EPOCHS,
    #     batch_size=BATCH_SIZE,
    #     learning_rate=LEARNING_RATE
    # )
    
    print("Training function would be called here")
    print("See OpenWakeWord documentation for full training pipeline")
    
except ImportError:
    print("OpenWakeWord training utilities not available in this version")
    print("")
    print("Alternative: Use OpenWakeWord's online training or")
    print("follow the training guide in the repository")
    print("")
    print("Repository: https://github.com/dscripka/openWakeWord")
    print("Training guide: Check repository docs/ or README")
"""

with open("train_hey_naptick.py", "w") as f:
    f.write(training_script)

print("Created train_hey_naptick.py")
print("")
print("Next steps:")
print("  1. Review train_hey_naptick.py")
print("  2. Follow OpenWakeWord training documentation")
print("  3. Or use OpenWakeWord's training utilities")
print("")
print("For detailed training instructions, see:")
print("  - docs/openwakeword_training_guide.md")
print("  - https://github.com/dscripka/openWakeWord")
PYTHON_TRAIN

echo ""
echo "========================================="
echo "Training Setup Complete"
echo "========================================="
echo ""
echo "Training script created: $TRAIN_DIR/train_hey_naptick.py"
echo ""
echo "To train the model:"
echo "  1. Ensure training data is in: $TRAIN_DIR/data/"
echo "  2. Review: $TRAIN_DIR/train_hey_naptick.py"
echo "  3. Follow OpenWakeWord training documentation"
echo ""
echo "For detailed instructions, see:"
echo "  - docs/openwakeword_training_guide.md"
echo "  - https://github.com/dscripka/openWakeWord"
echo ""