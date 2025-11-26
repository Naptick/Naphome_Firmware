# OpenWakeWord - Quick Reference Card

## 🚀 Quick Start (3 Commands)

```bash
./scripts/setup_openwakeword.sh                    # Add dependencies
cd samples/korvo_openwakeword_demo && idf.py build flash monitor  # Build & test
```

## 📋 Configuration (menuconfig)

```
Component config → OpenWakeWord
  [*] Enable OpenWakeWord
  [*] Use TensorFlow Lite Micro
  Model path: /spiffs/hey_naptick.tflite
  Threshold: 0.5
  Frame size: 80 ms
  Cooldown: 2000 ms
  Tensor arena: 16384 bytes
  [ ] Test mode (enable for testing without model)
```

## 🔧 Key APIs

### Initialization
```c
openwakeword_config_t config = {
    .model_path = "/spiffs/hey_naptick.tflite",
    .threshold = 0.5f,
    .sample_rate = 16000,
    .frame_size_ms = 80,
    .cooldown_ms = 2000,
};

openwakeword_handle_t handle;
openwakeword_init(&config, callback, user_data, &handle);
```

### Processing
```c
openwakeword_process_audio(handle, audio_samples, sample_count);
```

### Statistics
```c
uint32_t frames, detections;
bool loaded;
openwakeword_get_statistics(handle, &frames, &detections, &loaded);
```

## 📊 Typical Values

| Parameter | Default | Range | Notes |
|-----------|---------|-------|-------|
| Threshold | 0.5 | 0.3-0.8 | Lower = more sensitive |
| Frame size | 80ms | 40-160ms | 80ms = 1280 samples @ 16kHz |
| Cooldown | 2000ms | 1000-3000ms | Prevents re-triggering |
| Tensor arena | 16KB | 8-64KB | Increase if "Failed to allocate" |
| Sample rate | 16kHz | Fixed | Must match audio input |

## 🎯 Common Tasks

### Test Without Model
```ini
CONFIG_OPENWAKEWORD_TEST_MODE=y
```

### Tune Threshold
- Too many false positives → Increase (0.6-0.8)
- Misses detections → Decrease (0.3-0.4)
- Start at 0.5, adjust based on testing

### Increase Tensor Arena
```ini
CONFIG_OPENWAKEWORD_TENSOR_ARENA_SIZE=32768
```

### Enable ESP-DSP Optimization
```bash
idf.py add-dependency "espressif/esp-dsp"
```

## 🐛 Troubleshooting

| Symptom | Solution |
|---------|----------|
| "Failed to load model" | Check SPIFFS mounted, model path correct |
| "Failed to allocate tensors" | Increase tensor arena size |
| No detections | Lower threshold, check audio input |
| Too many false positives | Raise threshold, check test mode disabled |
| High CPU | Increase frame size, enable ESP-DSP |

## 📁 Key Files

- **Component**: `components/openwakeword/`
- **Demo**: `samples/korvo_openwakeword_demo/`
- **Setup**: `./scripts/setup_openwakeword.sh`
- **Validate**: `./scripts/validate_openwakeword_build.sh`
- **Train**: `./scripts/train_openwakeword.sh`

## 📚 Documentation

- **Quick Start**: `docs/OPENWAKEWORD_QUICK_START.md`
- **Production**: `docs/OPENWAKEWORD_PRODUCTION_GUIDE.md`
- **Test Mode**: `docs/OPENWAKEWORD_TEST_MODE.md`
- **Integration**: `docs/openwakeword_integration_guide.md`
- **Training**: `docs/openwakeword_training_guide.md`

## ⚡ Status

✅ **99% Complete** - Just add esp-tflite-micro and train model!