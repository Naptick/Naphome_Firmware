# OpenWakeWord Porting Plan for ESP32-S3

## Overview

This document outlines the plan to port OpenWakeWord wake word detection to ESP32-S3 (Korvo-1) for self-training "Hey, Naptick" wake word.

## Architecture

```
Raw I2S Audio (16kHz, 16-bit, mono)
    ↓
Audio Preprocessing (melspectrogram)
    ↓
TensorFlow Lite Micro Inference
    ↓
Wake Word Detection Result
    ↓
Callback to Voice Pipeline
```

## Key Components

### 1. Audio Preprocessing
- Convert 16kHz PCM to melspectrogram features
- Implement melspectrogram extraction in C
- Buffer management for 80ms frames (1280 samples @ 16kHz)

### 2. TensorFlow Lite Micro Integration
- Add `esp-tflite-micro` component
- Model loading from partition or SPIFFS
- Inference engine wrapper

### 3. OpenWakeWord Component
- `components/openwakeword/` - New component
- Model management
- Inference loop
- Integration with existing audio pipeline

## Implementation Steps

### Phase 1: Setup TensorFlow Lite Micro
1. Add esp-tflite-micro as component dependency
2. Create model partition in partition table
3. Test model loading

### Phase 2: Audio Preprocessing
1. Implement melspectrogram extraction
2. Port audio feature extraction from OpenWakeWord
3. Test with sample audio

### Phase 3: Inference Integration
1. Create OpenWakeWord wrapper component
2. Integrate with wake_word_service
3. Add configuration options

### Phase 4: Training & Model Conversion
1. Train "Hey, Naptick" model using OpenWakeWord Python
2. Convert model to ESP32-optimized format
3. Quantize model for size reduction

## Challenges

1. **Memory Constraints**: ESP32-S3 has limited RAM, need model quantization
2. **Audio Preprocessing**: Melspectrogram computation is CPU-intensive
3. **Model Size**: Need to fit in flash partition
4. **Real-time Performance**: Must run inference in <80ms per frame

## Estimated Effort

- Setup & Integration: 1 week
- Audio Preprocessing: 1 week  
- Inference Optimization: 1 week
- Testing & Tuning: 1 week
- **Total: 3-4 weeks**

## Alternative: Start with Simpler Approach

Given the complexity, consider:
1. First request Espressif to train (2-4 weeks, free)
2. While waiting, implement basic TensorFlow Lite keyword spotting
3. Only port full OpenWakeWord if Espressif option doesn't work