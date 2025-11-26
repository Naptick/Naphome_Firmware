#!/bin/bash
# Helper script to train "Hey, Naptick" wake word using OpenWakeWord

set -e

echo "=========================================="
echo "OpenWakeWord Training Helper"
echo "=========================================="
echo ""
echo "Wake Word: 'Hey, Naptick'"
echo ""

# Check if Python and required packages are available
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 not found"
    exit 1
fi

echo "Checking Python dependencies..."
python3 -c "import openwakeword" 2>/dev/null || {
    echo "Installing openwakeword..."
    pip3 install openwakeword
}

echo ""
echo "=========================================="
echo "Training Options:"
echo "=========================================="
echo ""
echo "1. TTS-Based Training (Recommended)"
echo "   - Generate synthetic speech samples"
echo "   - Fast, no recording needed"
echo "   - Good accuracy for development"
echo ""
echo "2. Record Your Own Samples"
echo "   - Record real audio samples"
echo "   - Better accuracy, more effort"
echo "   - Requires 100-200 samples"
echo ""
echo "3. View OpenWakeWord Documentation"
echo "   - Open training guide"
echo ""

read -p "Select option (1-3): " choice

case $choice in
    1)
        echo "Starting TTS-based training..."
        python3 << 'EOF'
from openwakeword import Model
print("TTS-based training not yet fully automated.")
print("See docs/openwakeword_training_guide.md for manual steps.")
print("")
print("Quick start:")
print("  1. pip install gtts  # or pyttsx3")
print("  2. Generate TTS samples for 'hey naptick'")
print("  3. Use OpenWakeWord training API")
EOF
        ;;
    2)
        echo "Recording-based training..."
        echo "See docs/openwakeword_training_guide.md for recording requirements"
        ;;
    3)
        echo "Opening OpenWakeWord documentation..."
        if [[ "$OSTYPE" == "darwin"* ]]; then
            open "https://github.com/dscripka/openWakeWord/blob/main/docs/TRAINING.md"
        else
            xdg-open "https://github.com/dscripka/openWakeWord/blob/main/docs/TRAINING.md" 2>/dev/null || echo "Visit: https://github.com/dscripka/openWakeWord"
        fi
        ;;
    *)
        echo "Invalid option"
        exit 1
        ;;
esac

echo ""
echo "=========================================="
echo "Next Steps:"
echo "=========================================="
echo ""
echo "1. Train model using OpenWakeWord Python API"
echo "2. Export model to TFLite format"
echo "3. Flash model to ESP32 (see docs/openwakeword_integration_guide.md)"
echo "4. Test wake word detection"
echo ""
echo "Documentation:"
echo "  - Training: docs/openwakeword_training_guide.md"
echo "  - Integration: docs/openwakeword_integration_guide.md"
echo "  - Porting Plan: docs/openwakeword_porting_plan.md"
echo ""
echo "Alternative: Request Espressif to train (faster, professional):"
echo "  ./scripts/request_wake_word.sh"
echo ""