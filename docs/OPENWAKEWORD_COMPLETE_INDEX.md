# OpenWakeWord Documentation Index

Complete guide to all OpenWakeWord documentation and resources.

## 🚀 Getting Started

### For First-Time Users
1. **[Quick Start Guide](OPENWAKEWORD_QUICK_START.md)** - Get running in 3 steps
2. **[Quick Reference](OPENWAKEWORD_QUICK_REFERENCE.md)** - Cheat sheet
3. **[Setup Guide](openwakeword_setup_guide.md)** - Detailed setup instructions

### For Developers
1. **[Integration Guide](openwakeword_integration_guide.md)** - Integrate into your app
2. **[Production Guide](OPENWAKEWORD_PRODUCTION_GUIDE.md)** - Production deployment
3. **[Test Mode Guide](OPENWAKEWORD_TEST_MODE.md)** - Test without model

## 📚 Implementation Guides

### Core Implementation
- **[Porting Plan](openwakeword_porting_plan.md)** - Original implementation plan
- **[TFLite Integration](openwakeword_tflite_integration_guide.md)** - TensorFlow Lite setup
- **[Training Guide](openwakeword_training_guide.md)** - Train "Hey, Naptick" model

### Integration
- **[Integration Examples](openwakeword_integration_guide.md)** - Code examples
- **[wake_word_service Patch](samples/korvo_voice_assistant/main/wake_word_service_oww_patch.diff)** - Integration patch
- **[Production Integration](OPENWAKEWORD_PRODUCTION_GUIDE.md)** - Production deployment

## 📊 Status & Summaries

### Current Status
- **[Final Status](OPENWAKEWORD_FINAL_STATUS.md)** - Current implementation status
- **[Complete Summary](OPENWAKEWORD_COMPLETE_SUMMARY.md)** - Full feature list
- **[Next Steps](OPENWAKEWORD_NEXT_STEPS.md)** - What remains

### Session Summaries
- **[Continuation Complete](OPENWAKEWORD_CONTINUATION_COMPLETE.md)** - First continuation
- **[Continuation Final](OPENWAKEWORD_CONTINUATION_FINAL.md)** - Final continuation
- **[Port Summary](OPENWAKEWORD_PORT_SUMMARY.md)** - Port overview

## 🔧 Reference

### Comparison
- **[OpenWakeWord vs ESP-SR](OPENWAKEWORD_VS_ESP_SR.md)** - When to use which

### ESP-SR Context
- **[ESP-SR Training](esp_sr_wake_word_training.md)** - How to request ESP-SR training
- **[ESP-SR Alternatives](esp_sr_self_training_alternatives.md)** - Self-training options
- **[Wake Word FAQ](WAKE_WORD_FAQ.md)** - Common questions

## 🛠️ Scripts & Tools

### Setup & Validation
- `scripts/setup_openwakeword.sh` - Automated setup
- `scripts/validate_openwakeword_build.sh` - Build validation
- `scripts/check_openwakeword_performance.sh` - Performance analysis

### Training
- `scripts/train_openwakeword.sh` - Training helper
- `scripts/request_wake_word.sh` - Request ESP-SR training

## 📁 Code Locations

### Component
- `components/openwakeword/` - Main component
  - `include/` - Headers
  - `src/` - Implementation
  - `CMakeLists.txt` - Build config
  - `Kconfig.projbuild` - Configuration
  - `README.md` - Component docs

### Demo Application
- `samples/korvo_openwakeword_demo/` - Standalone demo
  - `main/main.c` - Demo implementation
  - `sdkconfig.defaults` - Default config
  - `partitions.csv` - Partition table

### Integration Examples
- `samples/korvo_voice_assistant/main/openwakeword_integration_example.c`
- `samples/korvo_voice_assistant/main/wake_word_service_oww_integration.c`
- `samples/korvo_voice_assistant/main/wake_word_service_oww_patch.diff`

## 🎯 Common Tasks

### I want to...

**...get started quickly**
→ [Quick Start](OPENWAKEWORD_QUICK_START.md) → [Quick Reference](OPENWAKEWORD_QUICK_REFERENCE.md)

**...integrate into my app**
→ [Integration Guide](openwakeword_integration_guide.md) → [Production Guide](OPENWAKEWORD_PRODUCTION_GUIDE.md)

**...test without a model**
→ [Test Mode Guide](OPENWAKEWORD_TEST_MODE.md)

**...train a model**
→ [Training Guide](openwakeword_training_guide.md) → `./scripts/train_openwakeword.sh`

**...understand the implementation**
→ [Complete Summary](OPENWAKEWORD_COMPLETE_SUMMARY.md) → [Porting Plan](openwakeword_porting_plan.md)

**...compare with ESP-SR**
→ [OpenWakeWord vs ESP-SR](OPENWAKEWORD_VS_ESP_SR.md)

**...troubleshoot issues**
→ [Production Guide](OPENWAKEWORD_PRODUCTION_GUIDE.md) (Troubleshooting section)
→ [Test Mode Guide](OPENWAKEWORD_TEST_MODE.md) (Troubleshooting section)

**...see what's left to do**
→ [Next Steps](OPENWAKEWORD_NEXT_STEPS.md) → [Final Status](OPENWAKEWORD_FINAL_STATUS.md)

## 📈 Implementation Progress

- ✅ **Audio Preprocessing** - Complete melspectrogram extraction
- ✅ **Model Loading** - SPIFFS and partition loading
- ✅ **TFLite Integration** - Wrapper and inference
- ✅ **Test Mode** - Test without model
- ✅ **Performance Monitoring** - Statistics API
- ✅ **Documentation** - Comprehensive guides
- ✅ **Automation** - Setup and validation scripts
- 🚧 **Remaining** - Add esp-tflite-micro, train model

## 🎓 Learning Path

### Beginner
1. Read [Quick Start](OPENWAKEWORD_QUICK_START.md)
2. Run `./scripts/setup_openwakeword.sh`
3. Enable test mode and build demo
4. Review [Quick Reference](OPENWAKEWORD_QUICK_REFERENCE.md)

### Intermediate
1. Read [Integration Guide](openwakeword_integration_guide.md)
2. Review integration examples
3. Apply wake_word_service patch
4. Test with your application

### Advanced
1. Read [Complete Summary](OPENWAKEWORD_COMPLETE_SUMMARY.md)
2. Review [Porting Plan](openwakeword_porting_plan.md)
3. Train custom model
4. Optimize for production
5. Compare with ESP-SR

## 🔗 External Resources

- **OpenWakeWord**: https://github.com/dscripka/openWakeWord
- **ESP-TFLite-Micro**: https://github.com/espressif/esp-tflite-micro
- **ESP-SR Docs**: https://docs.espressif.com/projects/esp-sr/
- **TensorFlow Lite**: https://www.tensorflow.org/lite/microcontrollers

## 📝 Quick Links by Use Case

| I want to... | Read this... |
|--------------|--------------|
| Get started in 5 minutes | [Quick Start](OPENWAKEWORD_QUICK_START.md) |
| See all features | [Complete Summary](OPENWAKEWORD_COMPLETE_SUMMARY.md) |
| Integrate into my app | [Production Guide](OPENWAKEWORD_PRODUCTION_GUIDE.md) |
| Test without model | [Test Mode Guide](OPENWAKEWORD_TEST_MODE.md) |
| Train a model | [Training Guide](openwakeword_training_guide.md) |
| Understand vs ESP-SR | [Comparison](OPENWAKEWORD_VS_ESP_SR.md) |
| Troubleshoot | [Production Guide](OPENWAKEWORD_PRODUCTION_GUIDE.md) |
| See what's done | [Final Status](OPENWAKEWORD_FINAL_STATUS.md) |