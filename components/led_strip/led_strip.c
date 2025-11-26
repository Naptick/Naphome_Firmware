#include "led_strip.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt.h"

typedef struct led_strip_impl {
    rmt_channel_t channel;
    uint32_t led_count;
    uint8_t *buffer;
    bool inited;
    uint32_t t0h_ticks;
    uint32_t t0l_ticks;
    uint32_t t1h_ticks;
    uint32_t t1l_ticks;
} led_strip_impl_t;

static const char *TAG = "led_strip_drv";

static const uint32_t WS_T0H_NS = 350;
static const uint32_t WS_T0L_NS = 1000;
static const uint32_t WS_T1H_NS = 1000;
static const uint32_t WS_T1L_NS = 350;

static uint32_t s_next_channel = RMT_CHANNEL_0;

static inline rmt_channel_t led_strip_alloc_channel(void)
{
    rmt_channel_t ch = s_next_channel;
    s_next_channel = (s_next_channel + 1) % RMT_CHANNEL_MAX;
    return ch;
}

static void led_strip_build_items(const led_strip_impl_t *impl,
                                  const uint8_t *buffer,
                                  size_t length,
                                  rmt_item32_t *items)
{
    rmt_item32_t bit0 = {{{ impl->t0h_ticks, 1, impl->t0l_ticks, 0 }}};
    rmt_item32_t bit1 = {{{ impl->t1h_ticks, 1, impl->t1l_ticks, 0 }}};

    size_t item_index = 0;
    for (size_t byte = 0; byte < length; ++byte) {
        for (int bit = 7; bit >= 0; --bit) {
            items[item_index++] = (buffer[byte] & (1 << bit)) ? bit1 : bit0;
        }
    }
}

static esp_err_t led_strip_transmit(led_strip_impl_t *impl)
{
    size_t item_count = impl->led_count * 24;
    rmt_item32_t *items = malloc(item_count * sizeof(rmt_item32_t));
    ESP_RETURN_ON_FALSE(items, ESP_ERR_NO_MEM, TAG, "no mem for RMT items");

    led_strip_build_items(impl, impl->buffer, impl->led_count * 3, items);
    esp_err_t ret = rmt_write_items(impl->channel, items, item_count, true);
    if (ret == ESP_OK) {
        ret = rmt_wait_tx_done(impl->channel, pdMS_TO_TICKS(100));
    }
    free(items);
    return ret;
}

esp_err_t led_strip_new_rmt_device(const led_strip_config_t *config,
                                   const led_strip_rmt_config_t *rmt_dev_cfg,
                                   led_strip_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(config && ret_handle, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(config->led_model == LED_MODEL_WS2812, ESP_ERR_NOT_SUPPORTED, TAG, "only WS2812 supported");
    ESP_RETURN_ON_FALSE(config->color_component_format == LED_STRIP_COLOR_COMPONENT_FMT_RGB,
                        ESP_ERR_NOT_SUPPORTED,
                        TAG,
                        "only RGB format supported");

    led_strip_impl_t *impl = calloc(1, sizeof(led_strip_impl_t));
    ESP_RETURN_ON_FALSE(impl, ESP_ERR_NO_MEM, TAG, "no mem impl");
    impl->led_count = config->max_leds;
    impl->buffer = calloc(config->max_leds, 3);
    if (!impl->buffer) {
        free(impl);
        return ESP_ERR_NO_MEM;
    }

    rmt_channel_t channel = led_strip_alloc_channel();

    rmt_config_t rmt_cfg = RMT_DEFAULT_CONFIG_TX(config->strip_gpio_num, channel);
    rmt_cfg.clk_div = 2;
    // Calculate clock divider if resolution is specified
    if (rmt_dev_cfg && rmt_dev_cfg->resolution_hz > 0) {
        // For ESP-IDF v4.4, we need to install driver first before getting clock
        // Use a default base clock estimate (80MHz APB clock / 2 = 40MHz typical)
        uint32_t base_clk = 40 * 1000 * 1000; // 40MHz default
        uint32_t div = base_clk / rmt_dev_cfg->resolution_hz;
        if (div == 0) {
            div = 1;
        }
        rmt_cfg.clk_div = div;
    }
    ESP_RETURN_ON_ERROR(rmt_config(&rmt_cfg), TAG, "rmt config failed");
    ESP_RETURN_ON_ERROR(rmt_driver_install(channel, 0, 0), TAG, "rmt driver install failed");

    impl->channel = channel;
    // Calculate counter clock from APB clock and divider
    // ESP-IDF v4.4: APB clock is typically 80MHz, RMT uses APB clock / clk_div
    // We configured clk_div above, so counter_clk = APB_CLK / clk_div
    uint32_t apb_clk_hz = 80 * 1000 * 1000; // 80MHz APB clock (typical for ESP32-S3)
    uint32_t counter_clk_hz = apb_clk_hz / rmt_cfg.clk_div;
    if (counter_clk_hz == 0) {
        counter_clk_hz = 40 * 1000 * 1000; // Fallback to 40MHz
    }
    float ratio = (float)counter_clk_hz / 1e9f;
    impl->t0h_ticks = (uint32_t)(ratio * WS_T0H_NS);
    impl->t0l_ticks = (uint32_t)(ratio * WS_T0L_NS);
    impl->t1h_ticks = (uint32_t)(ratio * WS_T1H_NS);
    impl->t1l_ticks = (uint32_t)(ratio * WS_T1L_NS);
    impl->inited = true;
    *ret_handle = impl;
    return ESP_OK;
}

esp_err_t led_strip_set_pixel(led_strip_handle_t handle,
                              uint32_t index,
                              uint32_t red,
                              uint32_t green,
                              uint32_t blue)
{
    led_strip_impl_t *impl = handle;
    ESP_RETURN_ON_FALSE(impl && impl->inited, ESP_ERR_INVALID_STATE, TAG, "not ready");
    ESP_RETURN_ON_FALSE(index < impl->led_count, ESP_ERR_INVALID_ARG, TAG, "index out of range");
    size_t offset = index * 3;
    impl->buffer[offset + 0] = (uint8_t)green;
    impl->buffer[offset + 1] = (uint8_t)red;
    impl->buffer[offset + 2] = (uint8_t)blue;
    return ESP_OK;
}

esp_err_t led_strip_refresh(led_strip_handle_t handle)
{
    led_strip_impl_t *impl = handle;
    ESP_RETURN_ON_FALSE(impl && impl->inited, ESP_ERR_INVALID_STATE, TAG, "not ready");
    return led_strip_transmit(impl);
}

esp_err_t led_strip_clear(led_strip_handle_t handle)
{
    led_strip_impl_t *impl = handle;
    ESP_RETURN_ON_FALSE(impl && impl->inited, ESP_ERR_INVALID_STATE, TAG, "not ready");
    memset(impl->buffer, 0, impl->led_count * 3);
    return led_strip_transmit(impl);
}

esp_err_t led_strip_del(led_strip_handle_t handle)
{
    led_strip_impl_t *impl = handle;
    if (!impl) {
        return ESP_OK;
    }
    if (impl->inited) {
        rmt_driver_uninstall(impl->channel);
    }
    free(impl->buffer);
    free(impl);
    return ESP_OK;
}
