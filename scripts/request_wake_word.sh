#!/bin/bash
# Script to help request "Hey, Naptick" wake word from Espressif
# This opens the GitHub issue page for easy submission

set -e

echo "=========================================="
echo "ESP-SR Wake Word Request Helper"
echo "=========================================="
echo ""
echo "Wake Word: 'Hey, Naptick'"
echo "Target: ESP32-S3 (Korvo-1)"
echo ""
echo "This script will:"
echo "  1. Open the ESP-SR wake word request issue"
echo "  2. Show the request template"
echo "  3. Provide submission instructions"
echo ""

# Check if we're on macOS or Linux
if [[ "$OSTYPE" == "darwin"* ]]; then
    OPEN_CMD="open"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OPEN_CMD="xdg-open"
else
    OPEN_CMD="echo"
    echo "Please manually open: https://github.com/espressif/esp-sr/issues/88"
fi

echo "Opening GitHub issue page..."
$OPEN_CMD "https://github.com/espressif/esp-sr/issues/88" 2>/dev/null || true

echo ""
echo "=========================================="
echo "Request Template (copy this):"
echo "=========================================="
echo ""

cat << 'EOF'
Wake Word Request: "Hey, Naptick"

Wake Word Phrase: Hey, Naptick

Project Information:
- Project Name: Naphome Firmware
- Project Link: https://github.com/Naptick/Naphome-Firmware
- Project Overview: Voice assistant firmware for sleep monitoring and home automation running on ESP32-S3 (Korvo-1) development board. The assistant uses wake word detection to trigger voice commands for Spotify control, sensor monitoring, and IoT device management.

Hardware Platform:
- ESP32-S3 (Korvo-1 development board)
- 3-microphone PDM array for far-field detection
- ES8388 audio codec for playback

Use Case: Wake word detection for hands-free voice control of sleep monitoring and home automation features.

Additional Notes:
- Prefer WakeNet9 or WakeNet9s model (ESP32-S3 target)
- TTS-based training (V2.0) is acceptable
- Model will be integrated into production firmware
EOF

echo ""
echo "=========================================="
echo "Next Steps:"
echo "=========================================="
echo ""
echo "1. The GitHub issue page should be open in your browser"
echo "2. Copy the template above"
echo "3. Click 'Comment' on the issue"
echo "4. Paste the template and submit"
echo ""
echo "Requirements to get approved:"
echo "  ✓ You have an ongoing project (you meet this!)"
echo "  OR"
echo "  ✓ Get 5+ likes/upvotes from community"
echo ""
echo "Documentation:"
echo "  - Full guide: docs/esp_sr_wake_word_training.md"
echo "  - Template: docs/esp_sr_wake_word_request_template.md"
echo ""
echo "For paid professional training, contact: sales@espressif.com"
echo ""