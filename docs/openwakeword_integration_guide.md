# OpenWakeWord Integration Guide

## Overview

This guide explains how to integrate the OpenWakeWord port with your existing Naphome firmware to detect "Hey, Naptick" wake word.

## Current Implementation Status

✅ **Completed:**
- Component structure and API
- Basic framework
- Integration points defined

🚧 **In Progress:**
- TensorFlow Lite Micro integration
- Model loading
- Audio preprocessing (melspectrogram)
- Inference engine

## Integration Steps

### Step 1: Add TensorFlow Lite Micro

```bash
# Add esp-tflite-micro as a component
cd components
git submodule add https://github.com/espressif/esp-tflite-micro.git
cd esp-tflite-micro
git submodule update --init --recursive
```

Or add to your main `CMakeLists.txt`:
```cmake
set(EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}/components/openwakeword"
    "${PROJECT_ROOT}/components/esp-tflite-micro"
    # ... other components
)
```

### Step 2: Update Partition Table

Add a partition for the wake word model in `config/partitions.csv`:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     ,        0x6000,
phy_init, data, phy,     ,        0x1000,
factory,  app,  factory, ,        0x140000,
model,    data, spiffs,  ,        0x100000,  # For wake word models
storage,  data, spiffs,  ,        0x200000,
```

### Step 3: Integrate with wake_word_service

Update `samples/korvo_voice_assistant/main/wake_word_service.c`:

```c
#include "openwakeword.h"

// Add OpenWakeWord handle to wake_word_service struct
struct wake_word_service {
    // ... existing fields
    openwakeword_handle_t oww_handle;
    bool use_openwakeword;
};

// In wake_word_task, replace RMS detection with OpenWakeWord
static void wake_word_task(void *arg) {
    wake_word_service_t *service = (wake_word_service_t *)arg;
    
    if (service->use_openwakeword && service->oww_handle) {
        // Use OpenWakeWord inference
        openwakeword_process_audio(service->oww_handle, 
                                   frame_buffer, 
                                   samples_read);
    } else {
        // Fall back to RMS-based detection
        // ... existing RMS code
    }
}
```

### Step 4: Train "Hey, Naptick" Model

See `docs/openwakeword_training_guide.md` for detailed training instructions.

Quick start:
```bash
pip install openwakeword
python train_hey_naptick.py
```

### Step 5: Flash Model to ESP32

```bash
# Convert model to binary and flash to model partition
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 \
    --port /dev/ttyUSB0 \
    write_flash 0x140000 hey_naptick.tflite
```

## Configuration

Add to `sdkconfig.defaults`:

```
CONFIG_OPENWAKEWORD_ENABLE=y
CONFIG_OPENWAKEWORD_MODEL_PATH="/spiffs/hey_naptick.tflite"
CONFIG_OPENWAKEWORD_THRESHOLD=0.5
```

## Testing

1. Build and flash firmware
2. Monitor serial output for OpenWakeWord logs
3. Say "Hey, Naptick" near the microphone
4. Verify callback is triggered

## Fallback Strategy

The implementation supports fallback to RMS-based detection:
- If OpenWakeWord fails to initialize, use RMS detector
- If model not found, use RMS detector
- Can toggle between modes via configuration

## Performance Considerations

- **Memory**: Model + buffers ~100-200 KB
- **CPU**: Inference takes ~20-50ms per 80ms frame
- **Latency**: Detection latency ~100-200ms

## Troubleshooting

**Model not loading:**
- Check partition table has model partition
- Verify model file is flashed correctly
- Check model path in configuration

**No detections:**
- Adjust threshold (lower = more sensitive)
- Check audio is reaching OpenWakeWord
- Verify model was trained correctly
- Check sample rate matches (16kHz)

**High CPU usage:**
- Reduce frame size (trade latency for CPU)
- Optimize melspectrogram computation
- Use ESP32-S3 SIMD instructions