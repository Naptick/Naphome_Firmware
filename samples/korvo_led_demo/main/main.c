#include <stdbool.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#define STATUS_LED_COUNT 3
#define WIFI_LED_INDEX 0
#define SPOTIFY_LED_INDEX 1
#define AWS_LED_INDEX 2

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} rgb_t;

typedef struct {
    const char *label;
    rgb_t wifi;
    bool wifi_blink;
    rgb_t spotify;
    rgb_t aws;
} status_pattern_t;

static const char *TAG = "korvo_led_demo";
static led_strip_handle_t s_strip = NULL;

static inline uint8_t apply_brightness(uint8_t value)
{
    return (uint16_t)value * CONFIG_LED_DEMO_BRIGHTNESS / 255;
}

static void set_pixel_rgb(uint32_t index, rgb_t color)
{
    if (!s_strip || index >= CONFIG_LED_DEMO_LED_COUNT) {
        return;
    }
    led_strip_set_pixel(s_strip, index,
                        apply_brightness(color.red),
                        apply_brightness(color.green),
                        apply_brightness(color.blue));
}

static rgb_t wheel(uint8_t pos)
{
    pos = 255 - pos;
    if (pos < 85) {
        return (rgb_t){255 - pos * 3, 0, pos * 3};
    }
    if (pos < 170) {
        pos -= 85;
        return (rgb_t){0, pos * 3, 255 - pos * 3};
    }
    pos -= 170;
    return (rgb_t){pos * 3, 255 - pos * 3, 0};
}

static void render_rainbow_tail(uint32_t wheel_offset)
{
    const uint32_t reserved = CONFIG_LED_DEMO_LED_COUNT < STATUS_LED_COUNT ? CONFIG_LED_DEMO_LED_COUNT : STATUS_LED_COUNT;
    if (CONFIG_LED_DEMO_LED_COUNT <= reserved) {
        return;
    }

    const uint32_t tail_count = CONFIG_LED_DEMO_LED_COUNT - reserved;
    for (uint32_t i = 0; i < tail_count; ++i) {
        uint8_t wheel_pos = (uint8_t)((i * 256 / tail_count) + wheel_offset);
        rgb_t color = wheel(wheel_pos);
        set_pixel_rgb(reserved + i, color);
    }
}

static void apply_status_pattern(const status_pattern_t *pattern, bool wifi_on)
{
    if (!pattern) {
        return;
    }

    if (CONFIG_LED_DEMO_LED_COUNT > WIFI_LED_INDEX) {
        set_pixel_rgb(WIFI_LED_INDEX, wifi_on ? pattern->wifi : (rgb_t){0, 0, 0});
    }
    if (CONFIG_LED_DEMO_LED_COUNT > SPOTIFY_LED_INDEX) {
        set_pixel_rgb(SPOTIFY_LED_INDEX, pattern->spotify);
    }
    if (CONFIG_LED_DEMO_LED_COUNT > AWS_LED_INDEX) {
        set_pixel_rgb(AWS_LED_INDEX, pattern->aws);
    }
}

void app_main(void)
{
    static const status_pattern_t patterns[] = {
        {
            .label = "Wi-Fi connecting / services booting",
            .wifi = {0, 180, 255},       // cyan blink
            .wifi_blink = true,
            .spotify = {255, 90, 0},     // amber
            .aws = {255, 90, 0},         // amber
        },
        {
            .label = "All services ready",
            .wifi = {0, 255, 64},        // cyan/green solid
            .wifi_blink = false,
            .spotify = {0, 255, 0},      // green
            .aws = {0, 255, 0},          // green
        },
        {
            .label = "Spotify failure example",
            .wifi = {0, 255, 128},
            .wifi_blink = false,
            .spotify = {255, 0, 0},      // red fault
            .aws = {0, 255, 0},
        },
        {
            .label = "AWS failure example",
            .wifi = {0, 255, 128},
            .wifi_blink = false,
            .spotify = {0, 255, 0},
            .aws = {255, 0, 0},
        },
    };

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = CONFIG_LED_DEMO_STRIP_GPIO,
        .max_leds = CONFIG_LED_DEMO_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
        .with_dma = true,
    };

    ESP_LOGI(TAG, "Starting Korvo LED demo with %d pixels on GPIO %d (brightness=%d)",
             CONFIG_LED_DEMO_LED_COUNT, CONFIG_LED_DEMO_STRIP_GPIO, CONFIG_LED_DEMO_BRIGHTNESS);

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    ESP_ERROR_CHECK(led_strip_clear(s_strip));

    const uint32_t frame_delay_ms = CONFIG_LED_DEMO_SPIN_DELAY_MS;
    const uint32_t frames_per_pattern = CONFIG_LED_DEMO_PATTERN_INTERVAL_MS / frame_delay_ms ? CONFIG_LED_DEMO_PATTERN_INTERVAL_MS / frame_delay_ms : 1;
    const uint32_t blink_interval_frames = (250 / frame_delay_ms) ? (250 / frame_delay_ms) : 1;

    uint32_t pattern_index = 0;
    uint32_t frame_in_pattern = 0;
    uint32_t rainbow_offset = 0;

    ESP_LOGI(TAG, "Pattern: %s", patterns[pattern_index].label);

    while (true) {
        const status_pattern_t *pattern = &patterns[pattern_index];
        bool wifi_on = !pattern->wifi_blink || ((frame_in_pattern / blink_interval_frames) % 2 == 0);

        apply_status_pattern(pattern, wifi_on);
        render_rainbow_tail(rainbow_offset++);
        ESP_ERROR_CHECK(led_strip_refresh(s_strip));

        vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));

        frame_in_pattern++;
        if (frame_in_pattern >= frames_per_pattern) {
            frame_in_pattern = 0;
            pattern_index = (pattern_index + 1) % (sizeof(patterns) / sizeof(patterns[0]));
            ESP_LOGI(TAG, "Pattern: %s", patterns[pattern_index].label);
        }
    }
}
