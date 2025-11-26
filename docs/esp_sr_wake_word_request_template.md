# Wake Word Request Template for "Hey, Naptick"

Use this template when submitting your request to [ESP-SR Issue #88](https://github.com/espressif/esp-sr/issues/88).

## Request Template

Copy and paste this into a comment on the issue:

---

**Wake Word Request: "Hey, Naptick"**

**Wake Word Phrase:** Hey, Naptick

**Project Information:**
- **Project Name:** Naphome Firmware
- **Project Link:** https://github.com/Naptick/Naphome-Firmware
- **Project Overview:** 
  Voice assistant firmware for sleep monitoring and home automation running on ESP32-S3 (Korvo-1) development board. The assistant uses wake word detection to trigger voice commands for Spotify control, sensor monitoring, and IoT device management.

**Hardware Platform:**
- ESP32-S3 (Korvo-1 development board)
- 3-microphone PDM array for far-field detection
- ES8388 audio codec for playback

**Use Case:**
Wake word detection for hands-free voice control of sleep monitoring and home automation features. The device stays in low-power listen mode and activates on "Hey, Naptick" to process voice commands.

**Additional Notes:**
- Prefer WakeNet9 or WakeNet9s model (ESP32-S3 target)
- TTS-based training (V2.0) is acceptable
- Model will be integrated into production firmware

---

## Submission Checklist

Before submitting:

- [ ] Review [Wake Word Submission Agreement](https://github.com/espressif/esp-sr/blob/master/docs/_static/Wake%20Word%20Submission%20Agreement.pdf)
- [ ] Ensure project is publicly accessible or provide sufficient project details
- [ ] Verify "Naptick" trademark status if planning commercial use
- [ ] Have GitHub account ready to post comment
- [ ] Bookmark the issue to track responses

## What Happens Next

1. **Espressif Review** (1-2 weeks)
   - They review your request
   - Check if requirements are met
   - May ask for additional project details

2. **Training** (2-4 weeks)
   - Espressif trains model using TTS pipeline V2.0
   - Model achieves 95-98% accuracy vs human samples

3. **Release**
   - Model added to ESP-SR repository
   - You'll be notified via GitHub
   - Model available in `esp-sr/model/wakenet_model/`

4. **Integration**
   - Follow integration steps in `docs/esp_sr_wake_word_training.md`
   - Update firmware configuration
   - Test and tune threshold

## Alternative: Direct Contact

If you need faster turnaround or higher accuracy:

**Email:** sales@espressif.com
**Subject:** Wake Word Customization - "Hey, Naptick"
**Include:**
- Project details
- Production scale/volume
- Timeline requirements
- Budget considerations