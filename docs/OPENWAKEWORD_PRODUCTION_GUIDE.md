# OpenWakeWord Production Integration Guide

## Overview

This guide covers integrating OpenWakeWord into your production firmware as a replacement or complement to the existing energy-based wake word detector.

## Integration Options

### Option 1: Standalone (Recommended for Testing)

Use the demo application (`samples/korvo_openwakeword_demo`) to test OpenWakeWord independently before integrating.

**Pros**: 
- Isolated testing
- Easy to debug
- No impact on existing code

**Cons**:
- Separate application
- Doesn't integrate with voice pipeline

### Option 2: Replace wake_word_service (Production)

Modify `wake_word_service` to use OpenWakeWord instead of energy-based detection.

**See**: `samples/korvo_voice_assistant/main/wake_word_service_oww_patch.diff`

**Steps**:
1. Review the patch file
2. Apply changes to `wake_word_service.c` and `wake_word_service.h`
3. Add `use_openwakeword` flag to config
4. Enable via Kconfig or config struct

### Option 3: Parallel Detection (Advanced)

Run both detectors in parallel and use OpenWakeWord as primary with energy-based as fallback.

## Step-by-Step Integration

### 1. Setup Dependencies

```bash
# Run setup script
./scripts/setup_openwakeword.sh

# Verify
./scripts/validate_openwakeword_build.sh
```

### 2. Configure Build

```bash
cd samples/korvo_voice_assistant
idf.py menuconfig
```

Enable:
- `Component config -> OpenWakeWord -> Enable OpenWakeWord`
- `Component config -> OpenWakeWord -> Use TensorFlow Lite Micro`
- Set model path: `/spiffs/hey_naptick.tflite`
- Set threshold: `0.5` (tune based on testing)
- Set frame size: `80` ms
- Set cooldown: `2000` ms
- Set tensor arena: `16384` bytes (increase if needed)

### 3. Update Partition Table

Add model partition to `partitions.csv`:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     ,        0x6000,
phy_init, data, phy,     ,        0x1000,
factory,  app,  factory, ,        0x140000,
model,    data, spiffs,  ,        0x100000,  # For TFLite model
```

### 4. Integrate with wake_word_service

#### 4a. Update Header

Add to `wake_word_service.h`:

```c
#include "openwakeword.h"

typedef struct {
    // ... existing fields ...
    bool use_openwakeword;  // Enable OpenWakeWord
} wake_word_service_config_t;
```

#### 4b. Update Implementation

Add to `wake_word_service` struct:

```c
struct wake_word_service {
    // ... existing fields ...
    openwakeword_handle_t oww_handle;
    bool use_openwakeword;
};
```

Initialize in `wake_word_service_start()`:

```c
if (cfg->use_openwakeword) {
    openwakeword_config_t oww_config = {
        .model_path = CONFIG_OPENWAKEWORD_MODEL_PATH,
        .threshold = CONFIG_OPENWAKEWORD_THRESHOLD,
        .sample_rate = 16000,
        .frame_size_ms = CONFIG_OPENWAKEWORD_FRAME_SIZE_MS,
        .cooldown_ms = cfg->cooldown_ms,
        .enable_vad = false,
    };
    
    esp_err_t err = openwakeword_init(&oww_config,
                                       openwakeword_callback,
                                       service,
                                       &service->oww_handle);
    if (err == ESP_OK) {
        service->use_openwakeword = true;
    }
}
```

Process in `wake_word_task()`:

```c
if (service->use_openwakeword && service->oww_handle) {
    openwakeword_process_audio(service->oww_handle, 
                                service->frame_buffer, 
                                read);
    // Skip energy-based detection
    continue;
}
```

Cleanup in `wake_word_service_stop()`:

```c
if (service->oww_handle) {
    openwakeword_deinit(service->oww_handle);
}
```

### 5. Update Main Application

Enable OpenWakeWord in your main config:

```c
wake_word_service_config_t wake_cfg = {
    .audio = &audio,
    .use_openwakeword = true,  // Enable OpenWakeWord
    .sensitivity = 50,
    .cooldown_ms = 2000,
};
```

### 6. Train and Flash Model

```bash
# Train model
./scripts/train_openwakeword.sh

# Flash model to SPIFFS or partition
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 --port /dev/ttyUSB0 \
    write_flash 0x140000 hey_naptick.tflite
```

### 7. Build and Test

```bash
idf.py build flash monitor
```

## Configuration Tuning

### Threshold Tuning

Start with `0.5` and adjust based on testing:

- **Too many false positives**: Increase threshold (0.6-0.8)
- **Misses detections**: Decrease threshold (0.3-0.4)
- **Test with**: `CONFIG_OPENWAKEWORD_TEST_MODE=y` first

### Frame Size

- **80ms** (default): Good balance
- **40ms**: Lower latency, more CPU
- **160ms**: Less CPU, higher latency

### Tensor Arena Size

- **16KB**: Small models
- **32KB**: Medium models
- **64KB**: Large models

Increase if you see "Failed to allocate tensors" error.

### Cooldown

- **2000ms** (2s): Default, prevents multiple triggers
- **1000ms**: Faster re-triggering
- **3000ms**: More conservative

## Performance Monitoring

Get statistics:

```c
uint32_t frames, detections;
bool loaded;
openwakeword_get_statistics(handle, &frames, &detections, &loaded);

ESP_LOGI(TAG, "Processed: %lu frames, %lu detections, model: %s",
         frames, detections, loaded ? "loaded" : "not loaded");
```

## Troubleshooting

### Model Not Loading

**Symptoms**: "Failed to load model", "Model not loaded"

**Solutions**:
1. Check model path is correct
2. Verify SPIFFS is mounted
3. Check partition table has model partition
4. Verify model file is flashed
5. Check model size fits in partition

### Inference Errors

**Symptoms**: "TFLite Invoke failed", "Failed to allocate tensors"

**Solutions**:
1. Increase `CONFIG_OPENWAKEWORD_TENSOR_ARENA_SIZE`
2. Check model is compatible with TFLite Micro
3. Verify model input/output shapes match
4. Check for memory leaks

### No Detections

**Symptoms**: Audio processing but no detections

**Solutions**:
1. Lower threshold for testing
2. Check audio is reaching OpenWakeWord
3. Verify sample rate matches (16kHz)
4. Test with `CONFIG_OPENWAKEWORD_TEST_MODE=y`
5. Check melspectrogram logs look reasonable
6. Verify model was trained correctly

### High CPU Usage

**Symptoms**: Audio dropouts, system lag

**Solutions**:
1. Increase frame size (more samples per inference)
2. Enable ESP-DSP for optimized FFT
3. Reduce logging level
4. Check other tasks aren't blocking

## Production Checklist

Before deploying to production:

- [ ] Model trained and validated
- [ ] Threshold tuned for environment
- [ ] Test mode disabled (`CONFIG_OPENWAKEWORD_TEST_MODE=n`)
- [ ] Tensor arena size optimized
- [ ] Performance tested under load
- [ ] False positive rate acceptable
- [ ] Detection accuracy validated
- [ ] Memory usage verified
- [ ] Error handling tested
- [ ] Fallback behavior tested (if energy-based is fallback)

## Migration from Energy-Based

If replacing energy-based detector:

1. **Test in parallel first**: Run both detectors, compare results
2. **Gradual rollout**: Enable for subset of devices
3. **Monitor metrics**: Track detection rates, false positives
4. **Tune threshold**: Adjust based on real-world data
5. **Remove energy-based**: Once confident in OpenWakeWord

## Advanced: Custom Wake Words

To add multiple wake words:

1. Train separate models for each wake word
2. Load model based on configuration
3. Update callback to identify which wake word
4. Or use multi-class model (more complex)

## Support

For issues:
1. Check `docs/OPENWAKEWORD_TEST_MODE.md` for testing
2. Review `docs/openwakeword_integration_guide.md`
3. Run `./scripts/validate_openwakeword_build.sh`
4. Check logs for specific error messages