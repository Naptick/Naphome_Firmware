# Wake Word Training FAQ

## Can I train my own ESP-SR wake word model?

**No.** Espressif does NOT provide open-source training tools for WakeNet. The training pipeline is proprietary.

## What are my options?

### Option 1: Request Espressif to Train (Recommended)
- **Free TTS training** via GitHub issue #88
- 95-98% accuracy, 2-4 weeks
- Run: `./scripts/request_wake_word.sh`
- See: `docs/esp_sr_wake_word_training.md`

### Option 2: Use Open-Source Alternatives
- **OpenWakeWord** - Can train your own, needs ESP32 porting
- **Mycroft Precise** - Open-source, needs porting
- **TensorFlow Lite** - Full control, more complex
- See: `docs/esp_sr_self_training_alternatives.md`

### Option 3: Improve Current Detector
- Your RMS-based detector in `korvo_voice_assistant`
- Add better signal processing
- Quick improvements without training

## Quick Start

```bash
# Request Espressif to train "Hey, Naptick"
./scripts/request_wake_word.sh
```

## Documentation

- `docs/ESP_SR_TRAINING_SUMMARY.md` - Quick reference
- `docs/esp_sr_wake_word_training.md` - Complete ESP-SR guide
- `docs/esp_sr_self_training_alternatives.md` - Self-training options
- `docs/esp_sr_wake_word_request_template.md` - GitHub issue template
