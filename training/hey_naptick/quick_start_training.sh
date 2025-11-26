#!/bin/bash
#
# Quick start training for "Hey, Naptick"
# This script guides you through the fastest path to a trained model
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "========================================="
echo "Quick Start: Train 'Hey, Naptick'"
echo "========================================="
echo ""

cd "$SCRIPT_DIR"

# Check for training data
POSITIVE_COUNT=$(find data/positive -name "*.wav" 2>/dev/null | wc -l | tr -d ' ')
NEGATIVE_COUNT=$(find data/negative -name "*.wav" 2>/dev/null | wc -l | tr -d ' ')

if [ "$POSITIVE_COUNT" -gt 0 ] || [ "$NEGATIVE_COUNT" -gt 0 ]; then
    echo "✓ Found existing training data:"
    echo "  Positive: $POSITIVE_COUNT samples"
    echo "  Negative: $NEGATIVE_COUNT samples"
    echo ""
    
    if [ "$POSITIVE_COUNT" -ge 50 ] && [ "$NEGATIVE_COUNT" -ge 100 ]; then
        echo "✓ Sufficient data for training!"
        echo ""
        echo "Ready to train. Run:"
        echo "  python3 train_hey_naptick.py"
        echo ""
        exit 0
    else
        echo "⚠ Need more data:"
        if [ "$POSITIVE_COUNT" -lt 50 ]; then
            echo "  - Need at least 50 positive samples (have $POSITIVE_COUNT)"
        fi
        if [ "$NEGATIVE_COUNT" -lt 100 ]; then
            echo "  - Need at least 100 negative samples (have $NEGATIVE_COUNT)"
        fi
        echo ""
    fi
else
    echo "No training data found yet."
    echo ""
fi

# Offer to generate synthetic data
echo "Would you like to generate synthetic training data?"
echo "  This uses TTS to quickly generate samples (30-60 minutes)"
echo ""
read -p "Generate synthetic data now? (y/n): " -n 1 -r
echo ""

if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "Checking for TTS library..."
    
    # Check for gTTS or pyttsx3
    if python3 -c "from gtts import gTTS" 2>/dev/null; then
        echo "✓ gTTS found (Google TTS)"
        TTS_METHOD="gtts"
    elif python3 -c "import pyttsx3" 2>/dev/null; then
        echo "✓ pyttsx3 found (offline TTS)"
        TTS_METHOD="pyttsx3"
    else
        echo "⚠ No TTS library found"
        echo ""
        echo "Installing gTTS (requires internet)..."
        pip3 install --user gtts pydub || {
            echo "Failed to install gTTS"
            echo "Try: pip3 install gtts pydub"
            exit 1
        }
        TTS_METHOD="gtts"
    fi
    
    echo ""
    echo "Generating synthetic training data..."
    echo "This will take 30-60 minutes depending on TTS service..."
    echo ""
    
    python3 generate_synthetic_data.py
    
    echo ""
    echo "✓ Synthetic data generation complete!"
    echo ""
    echo "Next: Train the model"
    echo "  python3 train_hey_naptick.py"
    echo ""
else
    echo ""
    echo "To collect training data manually:"
    echo "  1. Record 'Hey, Naptick' samples"
    echo "  2. Save as WAV files (16kHz, mono, 16-bit)"
    echo "  3. Place in: data/positive/"
    echo "  4. Record negative samples"
    echo "  5. Place in: data/negative/"
    echo ""
    echo "See COLLECT_DATA.md for detailed instructions"
    echo ""
fi