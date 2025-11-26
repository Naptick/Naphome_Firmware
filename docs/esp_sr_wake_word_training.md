# Training ESP-SR for "Hey, Naptick" Wake Word

This document explains how to train a custom ESP-SR wake word model for "Hey, Naptick" to use with the Naphome firmware.

## Overview

ESP-SR (Espressif Speech Recognition) provides wake word detection through WakeNet models. To use a custom wake word like "Hey, Naptick", you need to obtain a trained model. 

**Important:** Espressif does NOT provide open-source training tools. The WakeNet training pipeline is proprietary. You cannot train your own ESP-SR models locally.

Espressif offers two main approaches:

1. **Free TTS-based training** (via GitHub issue) - 95-98% accuracy
2. **Paid professional training** (via Espressif sales) - Higher accuracy with human-recorded samples

**Alternative:** If you need to train your own wake word models, see `docs/esp_sr_self_training_alternatives.md` for open-source alternatives like OpenWakeWord.

## Option 1: Free TTS-Based Training (Recommended for Development)

Espressif provides free wake word models trained using TTS (Text-to-Speech) samples that reach 95-98% accuracy compared to human-recorded samples.

### Steps to Request "Hey, Naptick" Wake Word

1. **Visit the Wake Word Request Issue**
   - Go to: https://github.com/espressif/esp-sr/issues/88
   - This is the official community request thread for new wake words

2. **Submit Your Request**
   - Post a comment requesting "Hey, Naptick" wake word
   - Include the following information:
     - Wake word phrase: "Hey, Naptick"
     - Project link: https://github.com/Naptick/Naphome-Firmware
     - Brief project overview (voice assistant for sleep monitoring)
     - Mention it's for the Naphome/Korvo-1 voice assistant

3. **Requirements to Get Approved**
   - Your wake word must meet ONE of these criteria:
     - You have an ongoing project (attach project link + overview) ✓
     - Your wake word gets 5+ likes/upvotes from the community
   
   Since you have an active project, you should meet the first requirement.

4. **Wait for Espressif Response**
   - Espressif reviews requests and trains models using their TTS pipeline
   - Training typically takes a few weeks
   - They will notify you when the model is ready

5. **Download and Integrate the Model**
   Once the model is available:
   
   ```bash
   # The model will be available in esp-sr repository
   # Model name will likely be: wn9_heynaptick_tts2 or similar
   ```

### Integration Steps (After Model is Available)

1. **Update Partition Table**
   Ensure your `partitions.csv` includes a model partition:
   ```csv
   # Name,   Type, SubType, Offset,  Size, Flags
   nvs,      data, nvs,     ,        0x6000,
   phy_init, data, phy,     ,        0x1000,
   factory,  app,  factory, ,        0x140000,
   model,    data, spiffs,  ,        0x100000,
   ```

2. **Flash the Model**
   ```bash
   cd samples/korvo_farfield_mic_demo  # or your target sample
   idf.py set-target esp32s3
   
   # Flash the wake word model to the model partition
   # The exact command will depend on how Espressif provides the model
   # Typically: python $IDF_PATH/components/esptool_py/esptool/esptool.py \
   #   --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x140000 model.bin
   ```

3. **Update Configuration**
   In `sdkconfig.defaults` or via `menuconfig`:
   ```bash
   # Enable the new wake word model
   CONFIG_SR_WN_WN9_HEYNAPTICK_TTS2=y  # or whatever name Espressif assigns
   CONFIG_KORVO_FARFIELD_WAKE_WORD_MODEL="wn9_heynaptick_tts2"
   CONFIG_KORVO_FARFIELD_WAKE_WORD_THRESHOLD=60
   ```

4. **Update Code References**
   Update the wake word model name in your code:
   - `samples/korvo_farfield_mic_demo/main/main.c`
   - `samples/korvo_farfield_mic_demo/sdkconfig.defaults`
   - Any other samples using wake word detection

## Option 2: Paid Professional Training (For Production)

For higher accuracy and exclusive models, Espressif offers paid wake word customization services.

### When to Use This Option

- Production deployment requiring highest accuracy
- Need for exclusive wake word
- Custom training corpus requirements
- Faster turnaround needed

### Process

1. **Contact Espressif Sales**
   - Email: sales@espressif.com
   - Mention: Wake word customization for "Hey, Naptick"
   - Provide project details and production scale

2. **Training Options**

   **Option A: Customer-Provided Corpus**
   - You provide at least 20,000 qualified audio samples
   - Requirements:
     - Format: 16 kHz, 16-bit signed int, mono WAV
     - Speakers: 500+ people (men, women, all ages, 100+ children)
     - Environment: Quiet room (< 40 dB), professional audio room recommended
     - Recording: High-fidelity microphone
     - Distances: 1m and 3m from microphone
     - Each person: 15 recordings (5 fast, 5 normal, 5 slow speed)
   - Timeline: 2-3 weeks after corpus delivery
   - Cost: Training fee applies

   **Option B: Espressif-Provided Corpus**
   - Espressif collects all training data
   - Timeline: Corpus collection time + 2-3 weeks training
   - Cost: Training fee + corpus collection fee

3. **Hardware Review (Recommended)**
   - Send 1-2 hardware units to Espressif
   - They can optimize wake word performance for your specific hardware
   - Especially important for microphone array and cavity design

## Current Implementation Status

The Naphome firmware currently uses:

- **Temporary solution**: RMS-based energy detector (in `samples/korvo_voice_assistant`)
- **Reference implementation**: ESP-SR with "hi esp" wake word (in `samples/korvo_farfield_mic_demo`)

### Files That Need Updates

Once you have the "Hey, Naptick" model:

1. **`samples/korvo_farfield_mic_demo/sdkconfig.defaults`**
   ```bash
   # Change from:
   CONFIG_SR_WN_WN9_HIESP=y
   CONFIG_KORVO_FARFIELD_WAKE_WORD_MODEL="wn9_hiesp"
   
   # To:
   CONFIG_SR_WN_WN9_HEYNAPTICK_TTS2=y  # or whatever Espressif names it
   CONFIG_KORVO_FARFIELD_WAKE_WORD_MODEL="wn9_heynaptick_tts2"
   ```

2. **`samples/korvo_farfield_mic_demo/main/main.c`**
   - Update log messages referencing the wake word
   - Update model name in `init_afe()` function

3. **`samples/korvo_voice_assistant/main/wake_word_service.c`**
   - Replace RMS-based detector with ESP-SR integration
   - Follow the pattern from `korvo_farfield_mic_demo`

4. **`docs/naphome_voice_assistant.md`**
   - Update references from "Naphome" to "Hey, Naptick"

## Testing the Wake Word

After integrating the model:

1. **Build and Flash**
   ```bash
   cd samples/korvo_farfield_mic_demo
   idf.py set-target esp32s3
   idf.py build flash monitor
   ```

2. **Monitor Detection**
   - Watch for "WAKE WORD DETECTED" logs
   - LEDs 4, 8, 12 should turn white when detected
   - Adjust threshold if too sensitive/not sensitive enough

3. **Tune Threshold**
   ```bash
   idf.py menuconfig
   # Korvo Far-Field Mic Demo -> Wake Word Threshold
   # Higher = less sensitive (fewer false positives)
   # Lower = more sensitive (more false positives)
   ```

## Wake Word Model Specifications

- **WakeNet9**: Full-featured model, requires PSRAM
- **WakeNet9s**: Lightweight version, works without PSRAM
- **Model size**: ~20 KB (command-and-control profile)
- **Supported targets**: ESP32-S3 (Korvo-1)
- **Up to 5 wake words** per model
- **Wake word length**: 3-6 symbols recommended

## Additional Resources

- [ESP-SR Documentation](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/index.html)
- [Wake Word Customization Guide](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html)
- [Wake Word Request Issue #88](https://github.com/espressif/esp-sr/issues/88)
- [ESP-SR GitHub Repository](https://github.com/espressif/esp-sr)

## Next Steps

1. **Immediate**: Submit request to GitHub issue #88 for "Hey, Naptick"
2. **Short-term**: Continue development with RMS-based detector or "hi esp" model
3. **When model available**: Integrate the trained model following steps above
4. **Production**: Consider paid training if higher accuracy needed

## Notes

- The wake word "Naptick" appears in various places in the codebase
- Some references use "Naphome" - clarify which is the official wake word
- "Hey, Naptick" is 2 words, which is within the 3-6 symbol recommendation
- Consider trademark protection for "Naptick" as a wake word brand