#include "serial_command_parser.h"
#include "spotify_client.h"

#include <string.h>
#include <ctype.h>

#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "serial_cmd";

static spotify_client_t *s_spotify_client = NULL;

static void process_spotify_command(const char *cmd)
{
    if (!s_spotify_client) {
        ESP_LOGW(TAG, "Spotify client not initialized");
        return;
    }
    
    if (strcmp(cmd, "SPOTIFY_PLAY") == 0) {
        ESP_LOGI(TAG, "Serial command: Spotify Play");
        spotify_client_play(s_spotify_client, NULL);
    } else if (strcmp(cmd, "SPOTIFY_PAUSE") == 0) {
        ESP_LOGI(TAG, "Serial command: Spotify Pause");
        spotify_client_pause(s_spotify_client);
    } else if (strcmp(cmd, "SPOTIFY_RESUME") == 0) {
        ESP_LOGI(TAG, "Serial command: Spotify Resume");
        spotify_client_resume(s_spotify_client);
    } else if (strcmp(cmd, "SPOTIFY_VOLUME_UP") == 0) {
        ESP_LOGI(TAG, "Serial command: Spotify Volume Up");
        spotify_client_volume_delta(s_spotify_client, s_spotify_client->volume_step);
    } else if (strcmp(cmd, "SPOTIFY_VOLUME_DOWN") == 0) {
        ESP_LOGI(TAG, "Serial command: Spotify Volume Down");
        spotify_client_volume_delta(s_spotify_client, -s_spotify_client->volume_step);
    } else {
        ESP_LOGW(TAG, "Unknown Spotify command: %s", cmd);
    }
}

static void serial_command_task(void *arg)
{
    uint8_t *data = (uint8_t *)malloc(256);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate command buffer");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Serial command parser task started");
    
    while (1) {
        int len = uart_read_bytes(UART_NUM_0, data, 255, pdMS_TO_TICKS(100));
        if (len > 0) {
            data[len] = '\0';
            
            // Remove trailing newline/carriage return
            while (len > 0 && (data[len-1] == '\n' || data[len-1] == '\r')) {
                data[--len] = '\0';
            }
            
            if (len > 0) {
                char *cmd = (char *)data;
                
                // Skip leading whitespace
                while (*cmd && isspace((unsigned char)*cmd)) {
                    cmd++;
                }
                
                if (*cmd) {
                    ESP_LOGI(TAG, "Received command: %s", cmd);
                    
                    // Process commands
                    if (strncmp(cmd, "SPOTIFY_", 8) == 0) {
                        process_spotify_command(cmd);
                    } else {
                        ESP_LOGW(TAG, "Unknown command: %s", cmd);
                    }
                }
            }
        }
    }
    
    free(data);
    vTaskDelete(NULL);
}

esp_err_t serial_command_parser_init(spotify_client_t *spotify_client)
{
    s_spotify_client = spotify_client;
    
    // UART is already configured by ESP-IDF console, so we just need to read from it
    // Create task to read commands
    xTaskCreate(serial_command_task, "serial_cmd", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Serial command parser initialized");
    return ESP_OK;
}
