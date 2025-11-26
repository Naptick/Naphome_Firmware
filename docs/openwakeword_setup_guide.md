# OpenWakeWord Setup Guide for ESP32-S3

## Quick Start

This guide will help you complete the OpenWakeWord port and get "Hey, Naptick" wake word detection working.

## Current Implementation Status

✅ **Completed:**
- Component structure and API
- Melspectrogram extraction (with ESP-DSP support)
- Audio preprocessing framework
- Integration examples

🚧 **Remaining Work:**
- TensorFlow Lite Micro integration
- Model loading from partition/SPIFFS
- Complete TFLite inference implementation
- Training "Hey, Naptick" model

## Setup Steps

### Step 1: Add Required Dependencies

```bash
cd /path/to/Naphome-Firmware

# Add ESP-DSP for optimized FFT (recommended)
idf.py add-dependency "espressif/esp-dsp"

# Add TensorFlow Lite Micro (required for inference)
cd components
git submodule add https://github.com/espressif/esp-tflite-micro.git
cd esp-tflite-micro
git submodule update --init --recursive
```

### Step 2: Configure Build

```bash
cd samples/korvo_voice_assistant  # or your target sample
idf.py set-target esp32s3
idf.py menuconfig
```

Enable in menuconfig:
- `Component config → OpenWakeWord Configuration → Enable OpenWakeWord`
- `Component config → OpenWakeWord Configuration → Use TensorFlow Lite Micro`
- Set model path: `/spiffs/hey_naptick.tflite`
- Adjust threshold (default 0.5)

### Step 3: Update Partition Table

Edit `config/partitions.csv`:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     ,        0x6000,
phy_init, data, phy,     ,        0x1000,
factory,  app,  factory, ,        0x140000,
model,    data, spiffs,  ,        0x100000,  # For wake word models
storage,  data, spiffs,  ,        0x200000,
```

### Step 4: Train "Hey, Naptick" Model

```bash
# Install OpenWakeWord Python library
pip install openwakeword

# Run training helper
./scripts/train_openwakeword.sh

# Or follow docs/openwakeword_training_guide.md
```

### Step 5: Flash Model to ESP32

```bash
# Build SPIFFS image with model, or flash directly
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 \
    --port /dev/ttyUSB0 \
    write_flash 0x140000 hey_naptick.tflite
```

### Step 6: Integrate with wake_word_service

See `samples/korvo_voice_assistant/main/openwakeword_integration_example.c` for integration example.

Key changes needed in `wake_word_service.c`:

1. Add OpenWakeWord handle to struct
2. Initialize in `wake_word_service_start()`
3. Call `openwakeword_process_audio()` in wake word task
4. Cleanup in `wake_word_service_stop()`

### Step 7: Complete TFLite Integration

In `components/openwakeword/src/openwakeword.c`, implement:

1. **Model Loading** (in `openwakeword_init()`):
   ```c
   // Load model from partition/SPIFFS
   // Initialize tflite::MicroInterpreter
   // Allocate tensors
   ```

2. **Inference** (in `openwakeword_process_audio()`):
   ```c
   // Copy melspectrogram to input tensor
   // Run interpreter->Invoke()
   // Read output tensor
   // Check threshold and call callback
   ```

See ESP-TFLite-Micro examples for reference implementation.

## Build and Test

```bash
idf.py build flash monitor
```

Look for logs:
- "OpenWakeWord initialized successfully"
- "Using ESP-DSP FFT" (if ESP-DSP enabled)
- "Wake word detected!" when you say "Hey, Naptick"

## Troubleshooting

**"ESP-DSP FFT init failed"**
- Ensure ESP-DSP is added: `idf.py add-dependency "espressif/esp-dsp"`
- Check CONFIG_DSP_MAX_FFT_SIZE >= 512

**"Model not found"**
- Verify model is flashed to correct partition
- Check model path in configuration
- Use `esp_partition_read()` to verify model data

**"TFLite not enabled"**
- Add esp-tflite-micro as submodule
- Enable CONFIG_OPENWAKEWORD_USE_TFLITE
- Implement TFLite initialization code

**No detections**
- Check audio is reaching OpenWakeWord (enable debug logs)
- Verify melspectrogram extraction is working
- Lower threshold for testing
- Ensure model was trained correctly

## Next Steps

1. Complete TFLite integration (see TODO comments in code)
2. Train and test "Hey, Naptick" model
3. Optimize for ESP32-S3 (quantization, SIMD)
4. Integrate with wake_word_service
5. Test in real environment

## Alternative: Use Espressif Training

While completing this port, you can request Espressif to train "Hey, Naptick":

```bash
./scripts/request_wake_word.sh
```

This provides a professionally-trained model in 2-4 weeks with 95-98% accuracy.