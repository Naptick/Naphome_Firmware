#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tflite_wrapper tflite_wrapper_t;

/**
 * @brief Create TFLite wrapper
 * 
 * @param model_data TFLite model data
 * @param model_size Model size in bytes
 * @param tensor_arena_size Size of tensor arena
 * @return Wrapper handle or NULL on error
 */
tflite_wrapper_t* tflite_wrapper_create(const uint8_t* model_data, 
                                             size_t model_size,
                                             size_t tensor_arena_size);

/**
 * @brief Run inference
 * 
 * @param wrapper TFLite wrapper
 * @param input_data Input data (melspectrogram)
 * @param input_size Input size in floats
 * @param output_data Output buffer
 * @param output_size Output size (in/out)
 * @return ESP_OK on success
 */
esp_err_t tflite_wrapper_invoke(tflite_wrapper_t* wrapper, 
                                 const float* input_data, 
                                 size_t input_size,
                                 float* output_data, 
                                 size_t* output_size);

/**
 * @brief Destroy TFLite wrapper
 * 
 * @param wrapper TFLite wrapper
 */
void tflite_wrapper_destroy(tflite_wrapper_t* wrapper);

/**
 * @brief Get input tensor size
 * 
 * @param wrapper TFLite wrapper
 * @return Input size in floats
 */
size_t tflite_wrapper_get_input_size(tflite_wrapper_t* wrapper);

/**
 * @brief Get output tensor size
 * 
 * @param wrapper TFLite wrapper
 * @return Output size in floats
 */
size_t tflite_wrapper_get_output_size(tflite_wrapper_t* wrapper);

/**
 * @brief Check if wrapper is initialized
 * 
 * @param wrapper TFLite wrapper
 * @return true if initialized
 */
bool tflite_wrapper_is_initialized(tflite_wrapper_t* wrapper);

#ifdef __cplusplus
}
#endif