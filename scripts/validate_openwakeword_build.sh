#!/bin/bash
#
# Validate OpenWakeWord build configuration
# Checks that all dependencies are available and build will succeed
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "========================================="
echo "OpenWakeWord Build Validation"
echo "========================================="
echo ""

ERRORS=0
WARNINGS=0

# Check IDF environment
if [ -z "$IDF_PATH" ]; then
    echo "✗ Error: IDF_PATH not set"
    echo "  Run: . \$HOME/esp/esp-idf/export.sh"
    ERRORS=$((ERRORS + 1))
else
    echo "✓ IDF_PATH: $IDF_PATH"
fi

# Check required components
echo ""
echo "Checking components..."

COMPONENTS=(
    "components/openwakeword"
    "components/esp-tflite-micro:esp-tflite-micro"
)

for component in "${COMPONENTS[@]}"; do
    IFS=':' read -r path name <<< "$component"
    name=${name:-$path}
    
    if [ -d "$PROJECT_ROOT/$path" ]; then
        echo "  ✓ $name found"
    else
        echo "  ✗ $name not found at $path"
        if [ "$name" = "esp-tflite-micro" ]; then
            echo "    Run: ./scripts/setup_openwakeword.sh"
        fi
        ERRORS=$((ERRORS + 1))
    fi
done

# Check optional components
echo ""
echo "Checking optional components..."

OPTIONAL_COMPONENTS=(
    "components/esp-dsp:ESP-DSP (optimized FFT)"
    "managed_components/espressif__esp-dsp:ESP-DSP (managed)"
)

FOUND_DSP=0
for component in "${OPTIONAL_COMPONENTS[@]}"; do
    IFS=':' read -r path name <<< "$component"
    if [ -d "$PROJECT_ROOT/$path" ]; then
        echo "  ✓ $name found"
        FOUND_DSP=1
    fi
done

if [ $FOUND_DSP -eq 0 ]; then
    echo "  ⚠ ESP-DSP not found (will use fallback FFT)"
    WARNINGS=$((WARNINGS + 1))
fi

# Check demo application
echo ""
echo "Checking demo application..."
if [ -d "$PROJECT_ROOT/samples/korvo_openwakeword_demo" ]; then
    echo "  ✓ Demo application found"
    
    # Check demo files
    DEMO_FILES=(
        "samples/korvo_openwakeword_demo/CMakeLists.txt"
        "samples/korvo_openwakeword_demo/main/main.c"
        "samples/korvo_openwakeword_demo/sdkconfig.defaults"
    )
    
    for file in "${DEMO_FILES[@]}"; do
        if [ -f "$PROJECT_ROOT/$file" ]; then
            echo "    ✓ $(basename $file)"
        else
            echo "    ✗ Missing: $(basename $file)"
            ERRORS=$((ERRORS + 1))
        fi
    done
else
    echo "  ⚠ Demo application not found"
    WARNINGS=$((WARNINGS + 1))
fi

# Try to run idf.py build check
echo ""
echo "Checking build system..."
if command -v idf.py &> /dev/null; then
    echo "  ✓ idf.py available"
    
    # Try a dry-run build check
    if [ -d "$PROJECT_ROOT/samples/korvo_openwakeword_demo" ]; then
        cd "$PROJECT_ROOT/samples/korvo_openwakeword_demo"
        echo "  Running build validation..."
        
        if idf.py set-target esp32s3 &> /dev/null && \
           idf.py reconfigure &> /dev/null 2>&1; then
            echo "  ✓ Build configuration valid"
        else
            echo "  ⚠ Build configuration check failed (may need menuconfig)"
            WARNINGS=$((WARNINGS + 1))
        fi
    fi
else
    echo "  ✗ idf.py not found in PATH"
    echo "    Run: . \$IDF_PATH/export.sh"
    ERRORS=$((ERRORS + 1))
fi

# Summary
echo ""
echo "========================================="
if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo "✓ Validation passed!"
    echo ""
    echo "Next steps:"
    echo "  cd samples/korvo_openwakeword_demo"
    echo "  idf.py menuconfig  # Enable OpenWakeWord"
    echo "  idf.py build"
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo "✓ Validation passed with warnings"
    echo "  Warnings: $WARNINGS"
    echo ""
    echo "You can proceed, but some optimizations may be missing."
    exit 0
else
    echo "✗ Validation failed"
    echo "  Errors: $ERRORS"
    echo "  Warnings: $WARNINGS"
    echo ""
    echo "Please fix the errors above before building."
    exit 1
fi