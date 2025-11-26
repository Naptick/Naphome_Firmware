/**
 * @file tflite_wrapper.cpp
 * @brief TensorFlow Lite Micro wrapper for OpenWakeWord
 * 
 * Provides a C wrapper around TFLite C++ API for easier integration
 */

#include "tflite_wrapper.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "tflite_wrapper";

#ifdef CONFIG_OPENWAKEWORD_USE_TFLITE

// Include TFLite headers when available
#if __has_include("tensorflow/lite/micro/micro_interpreter.h")
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#define TFLITE_HEADERS_AVAILABLE 1
#else
#define TFLITE_HEADERS_AVAILABLE 0
#endif

struct tflite_wrapper {
#if TFLITE_HEADERS_AVAILABLE
    tflite::MicroInterpreter* interpreter;
    const tflite::Model* model;
#else
    void* interpreter;
    void* model;
#endif
    uint8_t *model_data;
    size_t model_size;
    uint8_t *tensor_arena;
    size_t tensor_arena_size;
    float *input_tensor;
    float *output_tensor;
    size_t input_size;
    size_t output_size;
    bool initialized;
};

extern "C" {

tflite_wrapper_t* tflite_wrapper_create(const uint8_t* model_data, 
                                         size_t model_size,
                                         size_t tensor_arena_size)
{
    if (!model_data || model_size == 0) {
        ESP_LOGE(TAG, "Invalid model data");
        return NULL;
    }
    
    tflite_wrapper_t* wrapper = (tflite_wrapper_t*)calloc(1, sizeof(tflite_wrapper_t));
    if (!wrapper) {
        return NULL;
    }
    
    wrapper->model_data = (uint8_t*)model_data;
    wrapper->model_size = model_size;
    wrapper->tensor_arena_size = tensor_arena_size;
    
    // Allocate tensor arena
    wrapper->tensor_arena = (uint8_t*)malloc(tensor_arena_size);
    if (!wrapper->tensor_arena) {
        free(wrapper);
        return NULL;
    }
    
#if TFLITE_HEADERS_AVAILABLE
    // Parse model
    const tflite::Model* model = tflite::GetModel(model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema version %d != %d", 
                 model->version(), TFLITE_SCHEMA_VERSION);
        free(wrapper->tensor_arena);
        free(wrapper);
        return NULL;
    }
    
    // Create interpreter
    static tflite::MicroErrorReporter micro_error_reporter;
    static tflite::AllOpsResolver resolver;
    
    tflite::MicroInterpreter* interpreter = new tflite::MicroInterpreter(
        model, resolver, wrapper->tensor_arena, tensor_arena_size,
        &micro_error_reporter);
    
    if (!interpreter) {
        ESP_LOGE(TAG, "Failed to create interpreter");
        free(wrapper->tensor_arena);
        free(wrapper);
        return NULL;
    }
    
    // Allocate tensors
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors");
        delete interpreter;
        free(wrapper->tensor_arena);
        free(wrapper);
        return NULL;
    }
    
    wrapper->interpreter = interpreter;
    wrapper->model = model;
    
    // Get input/output tensors
    TfLiteTensor* input = interpreter->input(0);
    TfLiteTensor* output = interpreter->output(0);
    
    wrapper->input_tensor = input->data.f;
    wrapper->output_tensor = output->data.f;
    wrapper->input_size = input->bytes / sizeof(float);
    wrapper->output_size = output->bytes / sizeof(float);
    wrapper->initialized = true;
    
    ESP_LOGI(TAG, "TFLite wrapper initialized");
    ESP_LOGI(TAG, "  Input: %zu floats, Output: %zu floats", 
             wrapper->input_size, wrapper->output_size);
#else
    ESP_LOGW(TAG, "TFLite headers not available - add esp-tflite-micro component");
    wrapper->initialized = false;
    wrapper->interpreter = NULL;
    wrapper->model = NULL;
#endif
    
    return wrapper;
}

esp_err_t tflite_wrapper_invoke(tflite_wrapper_t* wrapper, 
                                 const float* input_data, 
                                 size_t input_size,
                                 float* output_data, 
                                 size_t* output_size)
{
    if (!wrapper || !wrapper->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
#if TFLITE_HEADERS_AVAILABLE
    if (!wrapper->interpreter) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Copy input data
    if (input_data && input_size > 0) {
        size_t copy_size = (input_size < wrapper->input_size) ? 
                           input_size : wrapper->input_size;
        memcpy(wrapper->input_tensor, input_data, copy_size * sizeof(float));
    }
    
    // Run inference
    TfLiteStatus status = wrapper->interpreter->Invoke();
    if (status != kTfLiteOk) {
        ESP_LOGE(TAG, "TFLite Invoke failed: %d", status);
        return ESP_FAIL;
    }
    
    // Copy output
    if (output_data && output_size) {
        size_t copy_size = (wrapper->output_size < *output_size) ? 
                           wrapper->output_size : *output_size;
        memcpy(output_data, wrapper->output_tensor, copy_size * sizeof(float));
        *output_size = wrapper->output_size;
    }
    
    return ESP_OK;
#else
    (void)input_data;
    (void)input_size;
    (void)output_data;
    (void)output_size;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void tflite_wrapper_destroy(tflite_wrapper_t* wrapper)
{
    if (!wrapper) {
        return;
    }
    
#if TFLITE_HEADERS_AVAILABLE
    if (wrapper->interpreter) {
        delete wrapper->interpreter;
        wrapper->interpreter = NULL;
    }
#endif
    
    if (wrapper->tensor_arena) {
        free(wrapper->tensor_arena);
        wrapper->tensor_arena = NULL;
    }
    
    free(wrapper);
}

size_t tflite_wrapper_get_input_size(tflite_wrapper_t* wrapper)
{
    return wrapper ? wrapper->input_size : 0;
}

size_t tflite_wrapper_get_output_size(tflite_wrapper_t* wrapper)
{
    return wrapper ? wrapper->output_size : 0;
}

bool tflite_wrapper_is_initialized(tflite_wrapper_t* wrapper)
{
    return wrapper && wrapper->initialized;
}

} // extern "C"

#else // CONFIG_OPENWAKEWORD_USE_TFLITE not defined

extern "C" {

// Stub implementations when TFLite is not enabled
tflite_wrapper_t* tflite_wrapper_create(const uint8_t* model_data, 
                                         size_t model_size,
                                         size_t tensor_arena_size)
{
    ESP_LOGW(TAG, "TFLite not enabled (CONFIG_OPENWAKEWORD_USE_TFLITE)");
    (void)model_data;
    (void)model_size;
    (void)tensor_arena_size;
    return NULL;
}

esp_err_t tflite_wrapper_invoke(tflite_wrapper_t* wrapper, 
                                 const float* input_data, 
                                 size_t input_size,
                                 float* output_data, 
                                 size_t* output_size)
{
    (void)wrapper;
    (void)input_data;
    (void)input_size;
    (void)output_data;
    (void)output_size;
    return ESP_ERR_NOT_SUPPORTED;
}

void tflite_wrapper_destroy(tflite_wrapper_t* wrapper)
{
    (void)wrapper;
    // No-op
}

size_t tflite_wrapper_get_input_size(tflite_wrapper_t* wrapper)
{
    (void)wrapper;
    return 0;
}

size_t tflite_wrapper_get_output_size(tflite_wrapper_t* wrapper)
{
    (void)wrapper;
    return 0;
}

bool tflite_wrapper_is_initialized(tflite_wrapper_t* wrapper)
{
    (void)wrapper;
    return false;
}

} // extern "C"

#endif // CONFIG_OPENWAKEWORD_USE_TFLITE