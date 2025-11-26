#!/bin/bash
#
# Setup training environment for "Hey, Naptick"
# Installs dependencies and prepares training directory
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TRAIN_DIR="$PROJECT_ROOT/training/hey_naptick"

echo "========================================="
echo "Setup Training Environment for 'Hey, Naptick'"
echo "========================================="
echo ""

# Check Python
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 not found"
    echo "Please install Python 3.8+"
    exit 1
fi

echo "✓ Python found: $(python3 --version)"

# Install OpenWakeWord
echo ""
echo "Installing OpenWakeWord..."
pip3 install --user openwakeword || {
    echo "Error: Failed to install openwakeword"
    echo "Try: pip3 install openwakeword"
    exit 1
}

echo "✓ OpenWakeWord installed"

# Install additional dependencies
echo ""
echo "Installing additional dependencies..."
pip3 install --user numpy scipy librosa tensorflow || {
    echo "Warning: Some dependencies may have failed"
    echo "You may need to install manually:"
    echo "  pip3 install numpy scipy librosa tensorflow"
}

echo "✓ Dependencies installed"

# Create training directory structure
echo ""
echo "Creating training directory structure..."
mkdir -p "$TRAIN_DIR/data/positive"
mkdir -p "$TRAIN_DIR/data/negative"
mkdir -p "$TRAIN_DIR/models"
mkdir -p "$TRAIN_DIR/output"

echo "✓ Training directory created: $TRAIN_DIR"

# Create training data collection guide
cat > "$TRAIN_DIR/COLLECT_DATA.md" << 'EOF'
# Collecting Training Data for "Hey, Naptick"

## Positive Samples (Required)

Record "Hey, Naptick" in various conditions:

### Recommended Conditions
- **Distance**: Close (1-2 feet), Medium (3-5 feet), Far (6-10 feet)
- **Environment**: Quiet room, Noisy room, Outdoor
- **Speaker**: Different speakers if possible
- **Tone**: Normal, Excited, Whispered, Clear
- **Speed**: Normal, Fast, Slow

### File Format
- Format: WAV
- Sample Rate: 16kHz
- Channels: Mono
- Bit Depth: 16-bit

### Naming
Save as: `hey_naptick_001.wav`, `hey_naptick_002.wav`, etc.

### Quantity
- Minimum: 50 samples
- Recommended: 100-200 samples
- Optimal: 200+ samples

## Negative Samples (Recommended)

Record samples that should NOT trigger:

### Types
- Other wake words ("Hey Google", "Alexa", etc.)
- Similar phrases ("Hey, Naptick" variations that shouldn't trigger)
- Background noise
- Other speech
- Music

### Quantity
- Minimum: 100 samples
- Recommended: 200-500 samples

## Collection Methods

### Method 1: Direct Recording
```bash
# Using arecord (Linux) or similar
arecord -r 16000 -f S16_LE -c 1 hey_naptick_001.wav
# Say "Hey, Naptick"
# Press Ctrl+C to stop
```

### Method 2: TTS Generation
Use text-to-speech to generate samples:
- Google TTS
- Amazon Polly
- Azure TTS
- Local TTS (espeak, festival, etc.)

### Method 3: Online Tools
- Use online audio recording tools
- Export as WAV (16kHz, mono, 16-bit)

## Validation

After collecting, verify:
- [ ] All files are WAV format
- [ ] Sample rate is 16kHz
- [ ] Mono channel
- [ ] 16-bit depth
- [ ] Files are 1-3 seconds long
- [ ] Clear audio (not too quiet/loud)
EOF

echo "✓ Created data collection guide: $TRAIN_DIR/COLLECT_DATA.md"

# Create quick training script
cat > "$TRAIN_DIR/quick_train.py" << 'PYTHON_EOF'
#!/usr/bin/env python3
"""
Quick training script for "Hey, Naptick"
Uses OpenWakeWord's training utilities
"""

import os
import sys
from pathlib import Path

try:
    import openwakeword
    print(f"✓ OpenWakeWord version: {openwakeword.__version__}")
except ImportError:
    print("Error: openwakeword not installed")
    print("Install with: pip3 install openwakeword")
    sys.exit(1)

# Training configuration
TRAIN_DIR = Path(__file__).parent
DATA_DIR = TRAIN_DIR / "data"
POSITIVE_DIR = DATA_DIR / "positive"
NEGATIVE_DIR = DATA_DIR / "negative"
OUTPUT_DIR = TRAIN_DIR / "output"
MODEL_NAME = "hey_naptick"

print("Training Configuration:")
print(f"  Training directory: {TRAIN_DIR}")
print(f"  Positive samples: {POSITIVE_DIR}")
print(f"  Negative samples: {NEGATIVE_DIR}")
print(f"  Output directory: {OUTPUT_DIR}")
print("")

# Check for training data
positive_files = list(POSITIVE_DIR.glob("*.wav")) + list(POSITIVE_DIR.glob("*.WAV"))
negative_files = list(NEGATIVE_DIR.glob("*.wav")) + list(NEGATIVE_DIR.glob("*.WAV"))

if len(positive_files) == 0:
    print("Error: No positive samples found")
    print(f"Please add WAV files to: {POSITIVE_DIR}")
    print("See COLLECT_DATA.md for instructions")
    sys.exit(1)

print(f"Found {len(positive_files)} positive samples")
print(f"Found {len(negative_files)} negative samples")
print("")

# Note: Actual training requires OpenWakeWord's full training pipeline
# This script sets up the structure - see OpenWakeWord docs for training

print("Training setup complete!")
print("")
print("Next steps:")
print("  1. Collect training data (see COLLECT_DATA.md)")
print("  2. Review OpenWakeWord training documentation")
print("  3. Run training using OpenWakeWord's training utilities")
print("")
print("Resources:")
print("  - OpenWakeWord repo: https://github.com/dscripka/openWakeWord")
print("  - Training guide: docs/openwakeword_training_guide.md")
PYTHON_EOF

chmod +x "$TRAIN_DIR/quick_train.py"

echo "✓ Created training script: $TRAIN_DIR/quick_train.py"

echo ""
echo "========================================="
echo "Training Environment Setup Complete!"
echo "========================================="
echo ""
echo "Training directory: $TRAIN_DIR"
echo ""
echo "Next steps:"
echo "  1. Collect training data (see $TRAIN_DIR/COLLECT_DATA.md)"
echo "  2. Place positive samples in: $TRAIN_DIR/data/positive/"
echo "  3. Place negative samples in: $TRAIN_DIR/data/negative/"
echo "  4. Run: $TRAIN_DIR/quick_train.py"
echo ""
echo "For detailed training instructions:"
echo "  - docs/openwakeword_training_guide.md"
echo "  - https://github.com/dscripka/openWakeWord"
echo ""