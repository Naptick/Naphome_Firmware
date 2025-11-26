#!/bin/bash
#
# Check OpenWakeWord performance and resource usage
# Parses build output and provides recommendations
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "========================================="
echo "OpenWakeWord Performance Check"
echo "========================================="
echo ""

# Check if build directory exists
BUILD_DIR="$PROJECT_ROOT/samples/korvo_openwakeword_demo/build"
if [ ! -d "$BUILD_DIR" ]; then
    echo "⚠ Build directory not found: $BUILD_DIR"
    echo "  Run: cd samples/korvo_openwakeword_demo && idf.py build"
    exit 1
fi

echo "Analyzing build output..."
echo ""

# Check binary size
if [ -f "$BUILD_DIR/openwakeword/libopenwakeword.a" ]; then
    SIZE=$(stat -f%z "$BUILD_DIR/openwakeword/libopenwakeword.a" 2>/dev/null || \
          stat -c%s "$BUILD_DIR/openwakeword/libopenwakeword.a" 2>/dev/null || echo "0")
    SIZE_KB=$((SIZE / 1024))
    echo "Component size: ${SIZE_KB}KB"
    
    if [ $SIZE_KB -gt 200 ]; then
        echo "  ⚠ Large component size, consider optimization"
    else
        echo "  ✓ Reasonable size"
    fi
fi

# Check for TFLite
if grep -q "CONFIG_OPENWAKEWORD_USE_TFLITE=y" "$BUILD_DIR/../sdkconfig" 2>/dev/null; then
    echo "✓ TFLite enabled"
else
    echo "⚠ TFLite not enabled (test mode or not configured)"
fi

# Check for ESP-DSP
if grep -q "CONFIG_DSP_ENABLED" "$BUILD_DIR/../sdkconfig" 2>/dev/null; then
    echo "✓ ESP-DSP optimization enabled"
else
    echo "⚠ ESP-DSP not enabled (using fallback FFT)"
    echo "  To enable: idf.py add-dependency 'espressif/esp-dsp'"
fi

# Check tensor arena size
ARENA_SIZE=$(grep "CONFIG_OPENWAKEWORD_TENSOR_ARENA_SIZE" "$BUILD_DIR/../sdkconfig" 2>/dev/null | \
             cut -d'=' -f2 | tr -d '"' || echo "16384")
ARENA_KB=$((ARENA_SIZE / 1024))
echo "Tensor arena: ${ARENA_KB}KB"

if [ $ARENA_KB -lt 16 ]; then
    echo "  ⚠ May be too small for some models"
elif [ $ARENA_KB -gt 64 ]; then
    echo "  ⚠ Large arena, ensure sufficient RAM"
else
    echo "  ✓ Reasonable size"
fi

# Check frame size
FRAME_MS=$(grep "CONFIG_OPENWAKEWORD_FRAME_SIZE_MS" "$BUILD_DIR/../sdkconfig" 2>/dev/null | \
           cut -d'=' -f2 | tr -d '"' || echo "80")
echo "Frame size: ${FRAME_MS}ms"

# Calculate samples per frame
SAMPLES=$((16000 * FRAME_MS / 1000))
echo "  Samples per frame: ${SAMPLES} (at 16kHz)"

# Estimate inference frequency
INFERENCE_HZ=$((1000 / FRAME_MS))
echo "  Inference frequency: ~${INFERENCE_HZ}Hz"

if [ $FRAME_MS -lt 40 ]; then
    echo "  ⚠ Small frame size, higher CPU usage"
elif [ $FRAME_MS -gt 160 ]; then
    echo "  ⚠ Large frame size, higher latency"
else
    echo "  ✓ Good balance"
fi

# Check threshold
THRESHOLD=$(grep "CONFIG_OPENWAKEWORD_THRESHOLD" "$BUILD_DIR/../sdkconfig" 2>/dev/null | \
            cut -d'=' -f2 | tr -d '"' || echo "0.5")
echo "Detection threshold: ${THRESHOLD}"

if (( $(echo "$THRESHOLD < 0.3" | bc -l 2>/dev/null || echo 0) )); then
    echo "  ⚠ Low threshold, may have false positives"
elif (( $(echo "$THRESHOLD > 0.8" | bc -l 2>/dev/null || echo 0) )); then
    echo "  ⚠ High threshold, may miss detections"
else
    echo "  ✓ Reasonable threshold"
fi

# Check test mode
if grep -q "CONFIG_OPENWAKEWORD_TEST_MODE=y" "$BUILD_DIR/../sdkconfig" 2>/dev/null; then
    echo "⚠ TEST MODE ENABLED - Not for production!"
else
    echo "✓ Test mode disabled (production ready)"
fi

echo ""
echo "========================================="
echo "Recommendations:"
echo "========================================="
echo ""

# Provide recommendations
RECOMMENDATIONS=0

if ! grep -q "CONFIG_DSP_ENABLED" "$BUILD_DIR/../sdkconfig" 2>/dev/null; then
    echo "1. Enable ESP-DSP for optimized FFT:"
    echo "   idf.py add-dependency 'espressif/esp-dsp'"
    RECOMMENDATIONS=$((RECOMMENDATIONS + 1))
fi

if grep -q "CONFIG_OPENWAKEWORD_TEST_MODE=y" "$BUILD_DIR/../sdkconfig" 2>/dev/null; then
    echo "2. Disable test mode for production:"
    echo "   CONFIG_OPENWAKEWORD_TEST_MODE=n"
    RECOMMENDATIONS=$((RECOMMENDATIONS + 1))
fi

if [ $ARENA_KB -lt 16 ]; then
    echo "3. Consider increasing tensor arena if model fails to load"
    RECOMMENDATIONS=$((RECOMMENDATIONS + 1))
fi

if [ $RECOMMENDATIONS -eq 0 ]; then
    echo "✓ Configuration looks good!"
    echo ""
    echo "Next steps:"
    echo "  1. Train and flash model"
    echo "  2. Test detection accuracy"
    echo "  3. Tune threshold based on results"
fi

echo ""