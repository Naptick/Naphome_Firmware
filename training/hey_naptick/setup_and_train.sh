#!/bin/bash
#
# Setup and run training for "Hey, Naptick"
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TRAINING_DIR="$(dirname "$SCRIPT_DIR")"
PROJECT_ROOT="$(dirname "$TRAINING_DIR")"

echo "=========================================="
echo "Setup and Train 'Hey, Naptick'"
echo "=========================================="
echo ""

# Check if OpenWakeWord repo exists
OPENWAKEWORD_REPO="$TRAINING_DIR/openWakeWord"
if [ ! -d "$OPENWAKEWORD_REPO" ]; then
    echo "Cloning OpenWakeWord repository..."
    cd "$TRAINING_DIR"
    git clone https://github.com/dscripka/openWakeWord.git
    echo "✓ Repository cloned"
else
    echo "✓ OpenWakeWord repository found"
fi

echo ""

# Check for training data
DATA_DIR="$SCRIPT_DIR/data"
POSITIVE_DIR="$DATA_DIR/positive"
NEGATIVE_DIR="$DATA_DIR/negative"

if [ ! -d "$POSITIVE_DIR" ] || [ -z "$(ls -A $POSITIVE_DIR/*.wav 2>/dev/null)" ]; then
    echo "Error: No positive training samples found"
    exit 1
fi

POSITIVE_COUNT=$(find "$POSITIVE_DIR" -name "*.wav" -size +40k | wc -l | tr -d ' ')
NEGATIVE_COUNT=$(find "$NEGATIVE_DIR" -name "*.wav" -size +40k | wc -l | tr -d ' ')

echo "Training data:"
echo "  Positive: $POSITIVE_COUNT samples"
echo "  Negative: $NEGATIVE_COUNT samples"
echo ""

if [ "$POSITIVE_COUNT" -lt 10 ]; then
    echo "⚠ Warning: Need at least 10 positive samples"
    echo "   Current: $POSITIVE_COUNT"
    exit 1
fi

# Check for training scripts in OpenWakeWord
if [ -f "$OPENWAKEWORD_REPO/train.py" ] || [ -f "$OPENWAKEWORD_REPO/train_model.py" ]; then
    echo "✓ Found training script in OpenWakeWord"
    echo ""
    echo "Next steps:"
    echo "  1. Copy your data to OpenWakeWord training structure"
    echo "  2. Run their training script"
    echo ""
    echo "See: $OPENWAKEWORD_REPO/README.md or docs/TRAINING.md"
else
    echo "Checking OpenWakeWord structure..."
    ls -la "$OPENWAKEWORD_REPO" | head -20
    echo ""
    echo "Please check OpenWakeWord repository for training instructions:"
    echo "  $OPENWAKEWORD_REPO"
fi

echo ""
echo "Your training data is at:"
echo "  Positive: $POSITIVE_DIR"
echo "  Negative: $NEGATIVE_DIR"
echo ""