# TensorFlow Lite Micro Integration Guide

## Overview

This guide explains how to complete the TensorFlow Lite Micro integration for OpenWakeWord.

## Current Status

✅ **Completed:**
- Model loading from SPIFFS and partitions
- Audio preprocessing (melspectrogram)
- Framework for TFLite integration
- Memory management

🚧 **Remaining:**
- Actual TFLite interpreter initialization
- Inference implementation
- Tensor allocation

## Step-by-Step Integration

### Step 1: Add esp-tflite-micro Component

```bash
cd /path/to/Naphome-Firmware/components
git submodule add https://github.com/espressif/esp-tflite-micro.git
cd esp-tflite-micro
git submodule update --init --recursive
```

### Step 2: Update CMakeLists.txt

In `components/openwakeword/CMakeLists.txt`, add:

```cmake
# Add TFLite Micro
if(TARGET __idf_esp_tflite_micro)
    target_link_libraries(${COMPONENT_LIB} PRIVATE esp_tflite_micro)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_OPENWAKEWORD_USE_TFLITE=1)
endif()
```

Or add to main `CMakeLists.txt`:
```cmake
set(EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}/components/openwakeword"
    "${PROJECT_ROOT}/components/esp-tflite-micro"
    # ... other components
)
```

### Step 3: Implement TFLite Initialization

In `components/openwakeword/src/openwakeword.c`, uncomment and complete the TFLite initialization code around line 158-199.

Replace the TODO section with:

```c
#if TFLITE_AVAILABLE
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"

// Static tensor arena (adjust size based on model)
static uint8_t s_tensor_arena[CONFIG_OPENWAKEWORD_TENSOR_ARENA_SIZE] __attribute__((aligned(16)));

// Parse and initialize model
const tflite::Model* model = tflite::GetModel(handle->model_data);
if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(TAG, "Model schema version %d != %d", 
             model->version(), TFLITE_SCHEMA_VERSION);
    model_loader_free(handle->model_data);
    handle->model_data = NULL;
} else {
    // Create resolver and interpreter
    static tflite::MicroErrorReporter micro_error_reporter;
    static tflite::AllOpsResolver resolver;
    
    static tflite::MicroInterpreter* interpreter = new tflite::MicroInterpreter(
        model, resolver, s_tensor_arena, sizeof(s_tensor_arena),
        &micro_error_reporter);
    
    // Allocate tensors
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors");
        delete interpreter;
        interpreter = nullptr;
    } else {
        handle->tflite_interpreter = (void*)interpreter;
        
        // Get input/output tensors
        TfLiteTensor* input = interpreter->input(0);
        TfLiteTensor* output = interpreter->output(0);
        
        handle->input_tensor = input->data.f;
        handle->output_tensor = output->data.f;
        handle->input_size = input->bytes / sizeof(float);
        handle->output_size = output->bytes / sizeof(float);
        handle->model_loaded = true;
        
        ESP_LOGI(TAG, "TFLite interpreter initialized");
        ESP_LOGI(TAG, "Input: %zu floats, Output: %zu floats", 
                 handle->input_size, handle->output_size);
    }
}
#endif
```

### Step 4: Implement Inference

In `openwakeword_process_audio()`, replace the TODO around line 290-304 with:

```c
#if TFLITE_AVAILABLE
if (handle->tflite_interpreter) {
    tflite::MicroInterpreter* interpreter = 
        static_cast<tflite::MicroInterpreter*>(handle->tflite_interpreter);
    
    // Copy melspectrogram to input tensor
    if (handle->input_tensor && handle->input_size >= mel_size) {
        memcpy(handle->input_tensor, handle->melspectrogram_buffer, 
               mel_size * sizeof(float));
        
        // Run inference
        TfLiteStatus invoke_status = interpreter->Invoke();
        if (invoke_status != kTfLiteOk) {
            ESP_LOGE(TAG, "TFLite Invoke failed: %d", invoke_status);
            return ESP_FAIL;
        }
        
        // Get output (assuming binary classification)
        TfLiteTensor* output_tensor = interpreter->output(0);
        if (output_tensor->bytes >= 2 * sizeof(float)) {
            // Binary output: [not_wake_word, wake_word]
            confidence = output_tensor->data.f[1];
        } else if (output_tensor->bytes >= sizeof(float)) {
            // Single output
            confidence = output_tensor->data.f[0];
        }
    }
}
#endif
```

### Step 5: Update Build Configuration

Add to `sdkconfig.defaults`:
```
CONFIG_OPENWAKEWORD_ENABLE=y
CONFIG_OPENWAKEWORD_USE_TFLITE=y
CONFIG_OPENWAKEWORD_MODEL_PATH="/spiffs/hey_naptick.tflite"
CONFIG_OPENWAKEWORD_THRESHOLD=0.5
CONFIG_OPENWAKEWORD_TENSOR_ARENA_SIZE=16384
```

### Step 6: Build and Test

```bash
idf.py set-target esp32s3
idf.py menuconfig  # Verify OpenWakeWord settings
idf.py build
```

## Troubleshooting

**"Model schema version mismatch"**
- Re-export model with compatible TFLite version
- Check TFLITE_SCHEMA_VERSION in TFLite headers

**"Failed to allocate tensors"**
- Increase CONFIG_OPENWAKEWORD_TENSOR_ARENA_SIZE
- Try 32KB or 64KB
- Check model size and complexity

**"TFLite Invoke failed"**
- Check input tensor shape matches model expectations
- Verify melspectrogram size (40 features)
- Enable TFLite logging for details

**Model not found**
- Verify SPIFFS is mounted
- Check model path in configuration
- Ensure model is flashed to correct partition

## Model Requirements

Your trained "Hey, Naptick" model should have:
- **Input**: 40 float values (melspectrogram features)
- **Output**: 1-2 float values (confidence scores)
- **Size**: < 200 KB (preferably < 100 KB)
- **Format**: TensorFlow Lite (`.tflite`)
- **Quantization**: INT8 or FLOAT32

## Next Steps

1. Add esp-tflite-micro component
2. Uncomment and complete TFLite initialization code
3. Train "Hey, Naptick" model
4. Flash model and test
5. Integrate with wake_word_service