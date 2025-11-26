#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * @brief Load TFLite model from SPIFFS partition
 * 
 * @param partition_name Partition name (e.g., "model")
 * @param model_name Model filename within partition
 * @param model_data_out Output buffer for model data (caller must free)
 * @param model_size_out Output model size
 * @return ESP_OK on success
 */
esp_err_t model_loader_load_from_partition(const char *partition_name,
                                            const char *model_name,
                                            uint8_t **model_data_out,
                                            size_t *model_size_out);

/**
 * @brief Load TFLite model from SPIFFS filesystem
 * 
 * @param model_path Full path to model file (e.g., "/spiffs/hey_naptick.tflite")
 * @param model_data_out Output buffer for model data (caller must free)
 * @param model_size_out Output model size
 * @return ESP_OK on success
 */
esp_err_t model_loader_load_from_spiffs(const char *model_path,
                                         uint8_t **model_data_out,
                                         size_t *model_size_out);

/**
 * @brief Load model directly from raw partition (no filesystem)
 * 
 * @param partition_name Partition name
 * @param model_data_out Output buffer for model data (caller must free)
 * @param model_size_out Output model size
 * @return ESP_OK on success
 */
esp_err_t model_loader_load_from_partition_raw(const char *partition_name,
                                                uint8_t **model_data_out,
                                                size_t *model_size_out);

/**
 * @brief Free model data allocated by loader
 * 
 * @param model_data Model data to free
 */
void model_loader_free(uint8_t *model_data);