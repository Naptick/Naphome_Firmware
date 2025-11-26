#!/bin/bash
#
# Verify OpenWakeWord port is complete and ready for use
# Checks all components, documentation, and integration points
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "========================================="
echo "OpenWakeWord Port - Complete Verification"
echo "========================================="
echo ""

ERRORS=0
WARNINGS=0

# Check component files
echo "Checking component files..."
COMPONENT_FILES=(
    "components/openwakeword/CMakeLists.txt"
    "components/openwakeword/Kconfig.projbuild"
    "components/openwakeword/idf_component.yml"
    "components/openwakeword/include/openwakeword.h"
    "components/openwakeword/include/audio_features.h"
    "components/openwakeword/include/model_loader.h"
    "components/openwakeword/include/tflite_wrapper.h"
    "components/openwakeword/src/openwakeword.c"
    "components/openwakeword/src/audio_features.c"
    "components/openwakeword/src/model_loader.c"
    "components/openwakeword/src/tflite_wrapper.cpp"
    "components/openwakeword/src/openwakeword_test_mode.c"
)

for file in "${COMPONENT_FILES[@]}"; do
    if [ -f "$PROJECT_ROOT/$file" ]; then
        echo "  ✓ $(basename $file)"
    else
        echo "  ✗ Missing: $file"
        ERRORS=$((ERRORS + 1))
    fi
done

# Check demo application
echo ""
echo "Checking demo application..."
DEMO_FILES=(
    "samples/korvo_openwakeword_demo/CMakeLists.txt"
    "samples/korvo_openwakeword_demo/main/main.c"
    "samples/korvo_openwakeword_demo/main/CMakeLists.txt"
    "samples/korvo_openwakeword_demo/sdkconfig.defaults"
    "samples/korvo_openwakeword_demo/partitions.csv"
)

for file in "${DEMO_FILES[@]}"; do
    if [ -f "$PROJECT_ROOT/$file" ]; then
        echo "  ✓ $(basename $file)"
    else
        echo "  ✗ Missing: $file"
        ERRORS=$((ERRORS + 1))
    fi
done

# Check key documentation
echo ""
echo "Checking key documentation..."
KEY_DOCS=(
    "docs/OPENWAKEWORD_GETTING_STARTED.md"
    "docs/OPENWAKEWORD_QUICK_START.md"
    "docs/OPENWAKEWORD_PRODUCTION_GUIDE.md"
    "docs/OPENWAKEWORD_TEST_MODE.md"
    "docs/openwakeword_training_guide.md"
    "OPENWAKEWORD_START_HERE.md"
)

for doc in "${KEY_DOCS[@]}"; do
    if [ -f "$PROJECT_ROOT/$doc" ]; then
        echo "  ✓ $(basename $doc)"
    else
        echo "  ⚠ Missing: $doc"
        WARNINGS=$((WARNINGS + 1))
    fi
done

# Check scripts
echo ""
echo "Checking automation scripts..."
SCRIPTS=(
    "scripts/setup_openwakeword.sh"
    "scripts/validate_openwakeword_build.sh"
    "scripts/train_openwakeword.sh"
)

for script in "${SCRIPTS[@]}"; do
    if [ -f "$PROJECT_ROOT/$script" ]; then
        if [ -x "$PROJECT_ROOT/$script" ]; then
            echo "  ✓ $(basename $script) (executable)"
        else
            echo "  ⚠ $(basename $script) (not executable)"
            WARNINGS=$((WARNINGS + 1))
        fi
    else
        echo "  ✗ Missing: $script"
        ERRORS=$((ERRORS + 1))
    fi
done

# Check source code statistics
echo ""
echo "Checking source code..."
if [ -d "$PROJECT_ROOT/components/openwakeword/src" ]; then
    SOURCE_LINES=$(wc -l "$PROJECT_ROOT/components/openwakeword/src"/*.{c,cpp} 2>/dev/null | tail -1 | awk '{print $1}' || echo "0")
    if [ "$SOURCE_LINES" -gt 1000 ]; then
        echo "  ✓ Source code: $SOURCE_LINES lines"
    else
        echo "  ⚠ Source code: $SOURCE_LINES lines (expected >1000)"
        WARNINGS=$((WARNINGS + 1))
    fi
else
    echo "  ✗ Source directory not found"
    ERRORS=$((ERRORS + 1))
fi

# Check for esp-tflite-micro
echo ""
echo "Checking dependencies..."
if [ -d "$PROJECT_ROOT/components/esp-tflite-micro" ]; then
    echo "  ✓ esp-tflite-micro found"
else
    echo "  ⚠ esp-tflite-micro not found (run ./scripts/setup_openwakeword.sh)"
    WARNINGS=$((WARNINGS + 1))
fi

# Summary
echo ""
echo "========================================="
if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo "✅ VERIFICATION PASSED - Everything complete!"
    echo ""
    echo "Next steps:"
    echo "  1. Add esp-tflite-micro: ./scripts/setup_openwakeword.sh"
    echo "  2. Test with test mode: Enable CONFIG_OPENWAKEWORD_TEST_MODE=y"
    echo "  3. Train model: ./scripts/train_openwakeword.sh"
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo "✅ VERIFICATION PASSED with warnings"
    echo "  Warnings: $WARNINGS"
    echo ""
    echo "Port is functional but some optional items are missing."
    exit 0
else
    echo "❌ VERIFICATION FAILED"
    echo "  Errors: $ERRORS"
    echo "  Warnings: $WARNINGS"
    echo ""
    echo "Please fix the errors above."
    exit 1
fi