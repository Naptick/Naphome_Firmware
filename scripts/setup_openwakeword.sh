#!/bin/bash
#
# Setup script for OpenWakeWord on ESP32-S3
# Adds esp-tflite-micro and validates the setup
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPONENTS_DIR="$PROJECT_ROOT/components"

echo "========================================="
echo "OpenWakeWord Setup for ESP32-S3"
echo "========================================="
echo ""

# Check if we're in the right directory
if [ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]; then
    echo "Error: Not in project root. Looking for CMakeLists.txt"
    exit 1
fi

# Step 1: Add esp-tflite-micro
echo "Step 1: Adding esp-tflite-micro component..."
if [ -d "$COMPONENTS_DIR/esp-tflite-micro" ]; then
    echo "  ✓ esp-tflite-micro already exists"
else
    echo "  Adding esp-tflite-micro git submodule..."
    cd "$COMPONENTS_DIR"
    git submodule add https://github.com/espressif/esp-tflite-micro.git || {
        echo "  Warning: git submodule add failed (might already be in .gitmodules)"
    }
    
    echo "  Initializing submodules..."
    cd esp-tflite-micro
    git submodule update --init --recursive
    echo "  ✓ esp-tflite-micro added"
fi

# Step 2: Check for ESP-DSP (optional but recommended)
echo ""
echo "Step 2: Checking for ESP-DSP (optional optimization)..."
if [ -d "$COMPONENTS_DIR/esp-dsp" ] || [ -d "$PROJECT_ROOT/managed_components/espressif__esp-dsp" ]; then
    echo "  ✓ ESP-DSP found (FFT will be optimized)"
else
    echo "  ⚠ ESP-DSP not found (will use fallback FFT)"
    echo "  To add: idf.py add-dependency 'espressif/esp-dsp'"
fi

# Step 3: Validate OpenWakeWord component
echo ""
echo "Step 3: Validating OpenWakeWord component..."
if [ ! -d "$COMPONENTS_DIR/openwakeword" ]; then
    echo "  ✗ Error: openwakeword component not found"
    exit 1
fi

# Check required files
REQUIRED_FILES=(
    "components/openwakeword/CMakeLists.txt"
    "components/openwakeword/include/openwakeword.h"
    "components/openwakeword/src/openwakeword.c"
    "components/openwakeword/src/audio_features.c"
    "components/openwakeword/src/model_loader.c"
    "components/openwakeword/src/tflite_wrapper.cpp"
)

MISSING_FILES=()
for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$PROJECT_ROOT/$file" ]; then
        MISSING_FILES+=("$file")
    fi
done

if [ ${#MISSING_FILES[@]} -eq 0 ]; then
    echo "  ✓ All required files present"
else
    echo "  ✗ Missing files:"
    for file in "${MISSING_FILES[@]}"; do
        echo "    - $file"
    done
    exit 1
fi

# Step 4: Check demo application
echo ""
echo "Step 4: Checking demo application..."
if [ -d "$PROJECT_ROOT/samples/korvo_openwakeword_demo" ]; then
    echo "  ✓ Demo application found"
else
    echo "  ⚠ Demo application not found"
fi

# Step 5: Summary and next steps
echo ""
echo "========================================="
echo "Setup Complete!"
echo "========================================="
echo ""
echo "Next steps:"
echo ""
echo "1. Configure the build:"
echo "   cd samples/korvo_openwakeword_demo"
echo "   idf.py set-target esp32s3"
echo "   idf.py menuconfig"
echo "   # Enable: Component config -> OpenWakeWord"
echo ""
echo "2. Build the demo:"
echo "   idf.py build"
echo ""
echo "3. Train a model (optional, for testing):"
echo "   pip install openwakeword"
echo "   ./scripts/train_openwakeword.sh"
echo ""
echo "4. Flash and test:"
echo "   idf.py flash monitor"
echo ""
echo "For more details, see:"
echo "  - docs/OPENWAKEWORD_QUICK_START.md"
echo "  - docs/openwakeword_integration_guide.md"
echo ""