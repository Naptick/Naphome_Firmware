# Self-Training Wake Word Alternatives for ESP32-S3

## Short Answer: ESP-SR Does NOT Provide Self-Training

**Espressif does NOT provide open-source training tools for WakeNet models.** The training pipeline is proprietary and kept internal. Your options are:

1. **Request Espressif to train** (free TTS or paid professional) - See `docs/esp_sr_wake_word_training.md`
2. **Use alternative open-source wake word solutions** that can be self-trained
3. **Use a simple energy-based detector** (what you're currently doing in `korvo_voice_assistant`)

## Alternative Open-Source Wake Word Solutions

If you want to train your own wake word model, here are viable alternatives:

### Option 1: OpenWakeWord (Recommended for Self-Training) ⭐

**What it is:** Open-source wake word detection framework by Mycroft AI

**Pros:**
- ✅ Fully open-source, can train your own models
- ✅ Python-based training pipeline
- ✅ Works on resource-constrained devices
- ✅ Can export models for embedded systems
- ✅ Active development and community

**Cons:**
- ❌ Not natively designed for ESP32 (would need porting)
- ❌ May require more memory than ESP-SR
- ❌ Need to port inference engine to ESP-IDF

**Training:**
```bash
# Install OpenWakeWord
pip install openwakeword

# Train custom model
python -m openwakeword.train --wake-word "hey naptick" --output-dir ./models
```

**Resources:**
- GitHub: https://github.com/dscripka/openWakeWord
- Documentation: https://github.com/dscripka/openWakeWord/blob/main/docs/TRAINING.md

### Option 2: Mycroft Precise

**What it is:** Mycroft's wake word engine (predecessor to OpenWakeWord)

**Pros:**
- ✅ Open-source training tools
- ✅ Can train custom wake words
- ✅ Lightweight models
- ✅ Well-documented

**Cons:**
- ❌ Less actively maintained (superseded by OpenWakeWord)
- ❌ Would need ESP32 port
- ❌ Python-based, needs C/C++ port for ESP32

**Resources:**
- GitHub: https://github.com/MycroftAI/mycroft-precise
- Training: https://github.com/MycroftAI/mycroft-precise/wiki/Training-your-own-wake-word

### Option 3: Rhasspy Wake Word

**What it is:** Part of Rhasspy voice assistant framework

**Pros:**
- ✅ Open-source
- ✅ Multiple wake word engines supported
- ✅ Can use Pocketsphinx, Porcupine, or custom models

**Cons:**
- ❌ More complex, full voice assistant framework
- ❌ Not optimized for ESP32
- ❌ Would need significant porting work

**Resources:**
- GitHub: https://github.com/rhasspy/rhasspy

### Option 4: Simple ML-Based Approach (TensorFlow Lite)

**What it is:** Train a simple keyword spotting model using TensorFlow Lite

**Pros:**
- ✅ Full control over training
- ✅ TensorFlow Lite Micro runs on ESP32
- ✅ Can use transfer learning from existing models
- ✅ Many tutorials available

**Cons:**
- ❌ More complex to implement
- ❌ Need ML expertise
- ❌ Model optimization for ESP32 can be challenging
- ❌ Memory constraints on ESP32-S3

**Resources:**
- TensorFlow Lite Micro: https://www.tensorflow.org/lite/microcontrollers
- Keyword Spotting Tutorial: https://www.tensorflow.org/lite/models/modify/model_maker/speech_recognition
- ESP32 TensorFlow Lite: https://github.com/espressif/esp-tflite-micro

### Option 5: Keep Your Current RMS-Based Detector

**What you have:** Simple energy-based wake word detection in `samples/korvo_voice_assistant/main/wake_word_service.c`

**Pros:**
- ✅ Already working
- ✅ No training needed
- ✅ Very lightweight
- ✅ Low latency

**Cons:**
- ❌ High false positive rate
- ❌ Not wake-word specific (just detects speech)
- ❌ Requires manual threshold tuning

**Improvement ideas:**
- Add frequency domain analysis (FFT)
- Implement simple pattern matching on audio features
- Use multiple microphones for beamforming
- Add voice activity detection (VAD) to reduce false positives

## Recommended Approach: Hybrid Solution

Given your constraints, here's a practical recommendation:

### Phase 1: Short-term (Now)
1. **Improve your existing RMS detector** with better signal processing
2. **Request Espressif to train "Hey, Naptick"** via GitHub issue #88
3. **Use "hi esp" model** as placeholder in the meantime

### Phase 2: Medium-term (When model arrives)
1. **Integrate ESP-SR model** when Espressif provides it
2. **Keep RMS detector as fallback** for development/testing

### Phase 3: Long-term (If needed)
1. **Evaluate OpenWakeWord** if ESP-SR model doesn't meet requirements
2. **Port OpenWakeWord inference** to ESP32-S3 if self-training becomes critical
3. **Consider cloud-based wake word** as alternative (send audio to cloud, get wake word detection)

## Porting OpenWakeWord to ESP32-S3

**Status: 🚧 Implementation Started**

I've started porting OpenWakeWord to ESP32-S3. The basic framework is in place at `components/openwakeword/`.

### What's Been Created

✅ **Component Structure**:
- `components/openwakeword/` - New component with API
- Audio preprocessing framework
- Integration points defined

🚧 **Still Needed**:
- TensorFlow Lite Micro integration
- Complete melspectrogram extraction
- Model loading from partition/SPIFFS
- Inference implementation

### Steps to Complete the Port

1. **Add TensorFlow Lite Micro**:
   ```bash
   git submodule add https://github.com/espressif/esp-tflite-micro components/esp-tflite-micro
   ```

2. **Train the model** (Python, on your development machine):
   ```bash
   ./scripts/train_openwakeword.sh
   # Or see docs/openwakeword_training_guide.md
   ```

3. **Complete implementation**:
   - Implement melspectrogram in `components/openwakeword/src/audio_features.c`
   - Add TFLite inference in `components/openwakeword/src/openwakeword.c`
   - Integrate with `wake_word_service`

4. **Optimize for ESP32-S3**:
   - Quantize model to reduce size
   - Use ESP32-S3's SIMD instructions
   - Optimize memory usage

### Documentation Created

- `docs/openwakeword_porting_plan.md` - Implementation plan
- `docs/openwakeword_integration_guide.md` - Integration steps
- `docs/openwakeword_training_guide.md` - Training instructions
- `components/openwakeword/README.md` - Component documentation

### Estimated Effort to Complete
- **Training**: 1-2 days (if you have audio samples)
- **Complete porting**: 2-3 weeks
- **Optimization**: 1-2 weeks
- **Total**: 3-4 weeks to fully working implementation

## Comparison Table

| Solution | Self-Trainable | ESP32 Native | Effort | Accuracy | Memory |
|----------|---------------|--------------|--------|----------|--------|
| **ESP-SR (Espressif)** | ❌ No | ✅ Yes | Low | High (95-98%) | ~20 KB |
| **OpenWakeWord** | ✅ Yes | ⚠️ Needs port | High | High | ~50-100 KB |
| **Mycroft Precise** | ✅ Yes | ⚠️ Needs port | High | Medium-High | ~30-50 KB |
| **TensorFlow Lite** | ✅ Yes | ✅ Yes (TFLM) | Very High | High | ~100-200 KB |
| **RMS Detector** | N/A | ✅ Yes | None | Low | Minimal |

## My Recommendation

**For "Hey, Naptick" specifically:**

1. **Submit request to Espressif** (free, 2-4 weeks) - This is your best bet
2. **While waiting**, improve your RMS detector with better signal processing
3. **Only consider self-training** if:
   - Espressif denies your request
   - You need multiple custom wake words
   - You have specific accuracy requirements Espressif can't meet
   - You have 3-5 weeks to invest in porting

The Espressif TTS-based training (95-98% accuracy) is likely to be better than what you can achieve with limited training data and resources, unless you're willing to invest significant time in ML engineering.

## Quick Start: Request from Espressif

Run this now:
```bash
./scripts/request_wake_word.sh
```

This takes 5 minutes and gives you a professionally-trained model in 2-4 weeks with 95-98% accuracy.