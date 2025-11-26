/**
 * @file model_loader.c
 * @brief Model loading utilities for OpenWakeWord
 * 
 * Handles loading TFLite models from partitions or SPIFFS
 */

#include "model_loader.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_partition.h"

static const char *TAG = "model_loader";

esp_err_t model_loader_load_from_partition(const char *partition_name,
                                            const char *model_name,
                                            uint8_t **model_data_out,
                                            size_t *model_size_out)
{
    if (!partition_name || !model_data_out || !model_size_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find the partition
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        partition_name
    );
    
    if (!partition) {
        ESP_LOGE(TAG, "Partition '%s' not found", partition_name);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Found partition '%s': size=%u, offset=0x%x", 
             partition_name, (unsigned int)partition->size, (unsigned int)partition->address);
    
    // For SPIFFS partitions, we need to mount and read via VFS
    // For now, return error and suggest using SPIFFS VFS
    ESP_LOGW(TAG, "Direct partition reading not implemented for SPIFFS");
    ESP_LOGW(TAG, "Use model_loader_load_from_spiffs() instead");
    
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t model_loader_load_from_spiffs(const char *model_path,
                                         uint8_t **model_data_out,
                                         size_t *model_size_out)
{
    if (!model_path || !model_data_out || !model_size_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Loading model from SPIFFS: %s", model_path);
    
    // Try to open file from SPIFFS
    // Note: SPIFFS must be mounted before calling this
    FILE *f = fopen(model_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open model file: %s (SPIFFS mounted?)", model_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 1024 * 1024) {  // Max 1MB
        fclose(f);
        ESP_LOGE(TAG, "Invalid model file size: %ld", file_size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Allocate buffer
    uint8_t *model_data = malloc(file_size);
    if (!model_data) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    // Read file
    size_t bytes_read = fread(model_data, 1, file_size, f);
    fclose(f);
    
    if (bytes_read != file_size) {
        free(model_data);
        ESP_LOGE(TAG, "Failed to read complete model: read %zu of %ld bytes", 
                 bytes_read, file_size);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    ESP_LOGI(TAG, "Loaded model: %zu bytes", bytes_read);
    
    *model_data_out = model_data;
    *model_size_out = bytes_read;
    return ESP_OK;
}

esp_err_t model_loader_load_from_partition_raw(const char *partition_name,
                                                uint8_t **model_data_out,
                                                size_t *model_size_out)
{
    if (!partition_name || !model_data_out || !model_size_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find the partition
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        partition_name
    );
    
    if (!partition) {
        ESP_LOGE(TAG, "Partition '%s' not found", partition_name);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Reading from partition '%s': size=%u", 
             partition_name, (unsigned int)partition->size);
    
    // Allocate buffer
    uint8_t *model_data = malloc(partition->size);
    if (!model_data) {
        return ESP_ERR_NO_MEM;
    }
    
    // Read from partition
    esp_err_t err = esp_partition_read(partition, 0, model_data, partition->size);
    if (err != ESP_OK) {
        free(model_data);
        ESP_LOGE(TAG, "Failed to read partition: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Loaded model from partition: %u bytes", (unsigned int)partition->size);
    
    *model_data_out = model_data;
    *model_size_out = partition->size;
    return ESP_OK;
}

void model_loader_free(uint8_t *model_data)
{
    if (model_data) {
        free(model_data);
    }
}