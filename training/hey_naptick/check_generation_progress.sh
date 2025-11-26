#!/bin/bash
#
# Check progress of training data generation
#

cd "$(dirname "$0")"

echo "========================================="
echo "Training Data Generation Progress"
echo "========================================="
echo ""

POSITIVE_COUNT=$(find data/positive -name "hey_naptick_*.wav" 2>/dev/null | wc -l | tr -d ' ')
NEGATIVE_COUNT=$(find data/negative -name "negative_*.wav" 2>/dev/null | wc -l | tr -d ' ')

echo "Generated files:"
echo "  Positive samples: $POSITIVE_COUNT / 200"
echo "  Negative samples: $NEGATIVE_COUNT / 300"
echo ""

if [ "$POSITIVE_COUNT" -ge 200 ] && [ "$NEGATIVE_COUNT" -ge 300 ]; then
    echo "✅ Generation complete!"
    echo ""
    echo "Next: Train the model"
    echo "  python3 train_hey_naptick.py"
elif [ "$POSITIVE_COUNT" -gt 0 ] || [ "$NEGATIVE_COUNT" -gt 0 ]; then
    echo "⏳ Generation in progress..."
    echo ""
    echo "Check log: tail -f generation.log"
    echo "Or wait for completion (30-60 minutes total)"
else
    echo "⚠ No files generated yet"
    echo ""
    echo "Start generation:"
    echo "  python3 generate_with_gtts.py"
fi

echo ""
echo "Recent files:"
ls -lt data/positive/*.wav 2>/dev/null | head -3
ls -lt data/negative/*.wav 2>/dev/null | head -3