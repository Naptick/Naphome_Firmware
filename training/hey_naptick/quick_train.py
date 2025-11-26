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
