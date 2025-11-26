# Korvo-1 OpenWakeWord Demo

Demonstration of OpenWakeWord wake word detection for "Hey, Naptick" on ESP32-S3 (Korvo-1).

## What This Demo Does

- Initializes Korvo-1 audio capture (16kHz mono)
- Loads OpenWakeWord TFLite model from SPIFFS
- Processes audio frames through melspectrogram extraction
- Runs TFLite inference for wake word detection
- Logs detections when "Hey, Naptick" is detected

## Prerequisites

1. **Add esp-tflite-micro component**:
   ```bash
   cd components
   git submodule add https://github.com/espressif/esp-tflite-micro.git
   cd esp-tflite-micro && git submodule update --init --recursive
   ```

2. **Train "Hey, Naptick" model** (or use placeholder):
   ```bash
   ./scripts/train_openwakeword.sh
   # See docs/openwakeword_training_guide.md
   ```

3. **Flash model to SPIFFS**:
   ```bash
   # Add model to SPIFFS image or flash directly
   python $IDF_PATH/components/esptool_py/esptool/esptool.py \
       --chip esp32s3 --port /dev/ttyUSB0 \
       write_flash 0x140000 hey_naptick.tflite
   ```

## Build and Run

```bash
cd samples/korvo_openwakeword_demo
idf.py set-target esp32s3
idf.py menuconfig  # Verify OpenWakeWord settings
idf.py build flash monitor
```

## Expected Output

```
I (1234) oww_demo: === Korvo-1 OpenWakeWord Demo ===
I (1235) oww_demo: Listening for 'Hey, Naptick'
I (1236) openwakeword: Initializing OpenWakeWord
I (1237) model_loader: Loading model from SPIFFS: /spiffs/hey_naptick.tflite
I (1238) model_loader: Loaded model: 45678 bytes
I (1239) tflite_wrapper: TFLite wrapper initialized
I (1240) tflite_wrapper:   Input: 40 floats, Output: 2 floats
I (1241) openwakeword: TFLite wrapper initialized
I (1242) oww_demo: OpenWakeWord initialized successfully
I (1243) oww_demo: Required samples per frame: 1280
I (1244) oww_demo: Starting audio capture loop...
I (1245) oww_demo: Say 'Hey, Naptick' to test wake word detection
...
I (5678) openwakeword: *** WAKE WORD DETECTED *** Confidence: 0.85 (threshold: 0.50)
I (5679) oww_demo: ========================================
I (5680) oww_demo: *** WAKE WORD DETECTED ***
I (5681) oww_demo:   Word: hey_naptick
I (5682) oww_demo:   Confidence: 0.85
I (5683) oww_demo: ========================================
```

## Troubleshooting

**"TFLite headers not available"**
- Add esp-tflite-micro component
- Enable CONFIG_OPENWAKEWORD_USE_TFLITE

**"Failed to load model"**
- Ensure SPIFFS is mounted
- Check model path is correct
- Verify model is flashed to partition

**"Failed to allocate tensors"**
- Increase CONFIG_OPENWAKEWORD_TENSOR_ARENA_SIZE
- Try 32KB or 64KB

**No detections**
- Check audio is reaching OpenWakeWord
- Lower threshold for testing
- Verify model was trained correctly
- Check melspectrogram extraction is working