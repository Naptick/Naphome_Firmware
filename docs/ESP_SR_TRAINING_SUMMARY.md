# Quick Start: Training ESP-SR for "Hey, Naptick"

## TL;DR - How to Train ESP-SR for "Hey, Naptick"

**Note:** You cannot train ESP-SR models yourself - Espressif keeps the training pipeline proprietary. You have **two main options** to get a custom wake word model for "Hey, Naptick":

**For self-training options**, see `docs/esp_sr_self_training_alternatives.md`

### Option 1: Free TTS-Based Training (Recommended) ⭐

**Best for:** Development, prototyping, and projects with active GitHub repos

1. **Submit request to Espressif** (5 minutes):
   ```bash
   # Run the helper script
   ./scripts/request_wake_word.sh
   
   # Or manually visit:
   # https://github.com/espressif/esp-sr/issues/88
   ```

2. **Post the request** using the template from `docs/esp_sr_wake_word_request_template.md`

3. **Wait 2-4 weeks** for Espressif to train the model using TTS samples (95-98% accuracy)

4. **Integrate the model** when notified (follow `docs/esp_sr_wake_word_training.md`)

**Requirements:** Active project (you have this!) OR 5+ community upvotes

### Option 2: Paid Professional Training

**Best for:** Production deployments requiring highest accuracy

1. **Contact Espressif sales**: sales@espressif.com
2. **Provide training corpus** (20,000+ samples) OR let Espressif collect it
3. **Timeline**: 2-3 weeks after corpus delivery
4. **Cost**: Varies by production scale

## What You Need to Know

### Wake Word Specifications
- **Phrase**: "Hey, Naptick" (2 words, within 3-6 symbol recommendation)
- **Model**: WakeNet9 or WakeNet9s for ESP32-S3
- **Size**: ~20 KB
- **Accuracy**: 95-98% with TTS training, higher with human samples

### Current Status in Your Codebase
- ✅ ESP-SR integration exists in `samples/korvo_farfield_mic_demo`
- ✅ Currently using "hi esp" (wn9_hiesp) as placeholder
- ✅ Partition table configured for model storage
- ⏳ Waiting for "Hey, Naptick" model from Espressif

### Files That Will Need Updates (After Model Available)

1. **`samples/korvo_farfield_mic_demo/sdkconfig.defaults`**
   - Change `CONFIG_SR_WN_WN9_HIESP=y` to new model
   - Update `CONFIG_KORVO_FARFIELD_WAKE_WORD_MODEL`

2. **`samples/korvo_farfield_mic_demo/main/main.c`**
   - Update log messages and model name in `init_afe()`

3. **`samples/korvo_voice_assistant/main/wake_word_service.c`**
   - Replace RMS-based detector with ESP-SR (currently using temporary solution)

## Next Steps - Do This Now

1. **Run the request script**:
   ```bash
   ./scripts/request_wake_word.sh
   ```

2. **Or manually submit**:
   - Go to https://github.com/espressif/esp-sr/issues/88
   - Copy template from `docs/esp_sr_wake_word_request_template.md`
   - Post as comment

3. **Continue development** with current "hi esp" model or RMS detector

4. **When model arrives**, follow integration guide in `docs/esp_sr_wake_word_training.md`

## Documentation Files Created

- **`docs/esp_sr_wake_word_training.md`** - Complete training guide with both options
- **`docs/esp_sr_wake_word_request_template.md`** - Ready-to-use GitHub issue template
- **`scripts/request_wake_word.sh`** - Helper script to open issue and show template
- **`docs/ESP_SR_TRAINING_SUMMARY.md`** - This file (quick reference)

## Can You Train Your Own?

**Short answer: No, not with ESP-SR.**

Espressif does NOT provide open-source training tools for WakeNet. The training pipeline is proprietary. You have two paths:

1. **Request Espressif to train** (recommended) - Free TTS training or paid professional
2. **Use alternative open-source solutions** - See `docs/esp_sr_self_training_alternatives.md` for options like OpenWakeWord, Mycroft Precise, or TensorFlow Lite

## Key Resources

- [ESP-SR Docs](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/index.html)
- [Wake Word Customization](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html)
- [Request Issue #88](https://github.com/espressif/esp-sr/issues/88)
- [ESP-SR GitHub](https://github.com/espressif/esp-sr)
- **Self-Training Alternatives**: `docs/esp_sr_self_training_alternatives.md`

## Important Notes

⚠️ **Trademark**: Ensure you have rights to use "Naptick" as a wake word commercially

⚠️ **License**: Review [Wake Word Submission Agreement](https://github.com/espressif/esp-sr/blob/master/docs/_static/Wake%20Word%20Submission%20Agreement.pdf)

✅ **Project Status**: You have an active project, so you meet Espressif's requirements for free training