#include "led_controller.h"

#include <math.h>
#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "led_controller";

static void set_pixel_scaled(led_controller_t *controller, uint8_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!controller || !controller->strip || index >= controller->led_count) {
        return;
    }
    uint8_t scaled_r = (red * controller->brightness) / 255;
    uint8_t scaled_g = (green * controller->brightness) / 255;
    uint8_t scaled_b = (blue * controller->brightness) / 255;
    led_strip_set_pixel(controller->strip, index, scaled_r, scaled_g, scaled_b);
}

static void apply_color(led_controller_t *controller, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!controller || !controller->strip) {
        return;
    }
    for (uint8_t i = controller->reserved_pixels; i < controller->led_count; ++i) {
        set_pixel_scaled(controller, i, red, green, blue);
    }
    led_strip_refresh(controller->strip);
}

static void update_state(led_controller_t *controller)
{
    static const struct {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } kStateColors[] = {
        [LED_CONTROLLER_STATE_IDLE] = { .r = 0, .g = 0, .b = 16 },        // deep blue
        [LED_CONTROLLER_STATE_LISTENING] = { .r = 0, .g = 32, .b = 32 },  // cyan
        [LED_CONTROLLER_STATE_THINKING] = { .r = 32, .g = 20, .b = 0 },   // amber
        [LED_CONTROLLER_STATE_SPEAKING] = { .r = 48, .g = 0, .b = 24 },   // magenta
        [LED_CONTROLLER_STATE_ERROR] = { .r = 48, .g = 0, .b = 0 },       // red
    };
    if (controller->state > LED_CONTROLLER_STATE_ERROR) {
        controller->state = LED_CONTROLLER_STATE_IDLE;
    }
    apply_color(controller,
                kStateColors[controller->state].r,
                kStateColors[controller->state].g,
                kStateColors[controller->state].b);
}

esp_err_t led_controller_init(led_controller_t *controller, const led_controller_config_t *config)
{
    ESP_RETURN_ON_FALSE(controller && config, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    ESP_RETURN_ON_FALSE(config->led_count > 0, ESP_ERR_INVALID_ARG, TAG, "led count must be >0");

    led_strip_config_t strip_config = {
        .strip_gpio_num = config->data_gpio,
        .max_leds = config->led_count,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .with_dma = false,
    };

    led_strip_handle_t strip = NULL;
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip), TAG, "RMT init failed");

    controller->strip = strip;
    controller->led_count = config->led_count;
    controller->brightness = config->brightness > 0 ? config->brightness : 32;
    controller->reserved_pixels = config->reserved_pixels <= controller->led_count ? config->reserved_pixels : controller->led_count;
    controller->state = LED_CONTROLLER_STATE_IDLE;
    controller->trippy_active = false;
    controller->trippy_time = 0.0f;
    controller->trippy_speed = 0.02f; // Animation speed (adjust for faster/slower)

    led_strip_clear(controller->strip);
    update_state(controller);
    ESP_LOGI(TAG, "WS2812 strip on GPIO%d with %d pixels", config->data_gpio, config->led_count);
    return ESP_OK;
}

void led_controller_set_state(led_controller_t *controller, led_controller_state_t state)
{
    if (!controller || !controller->strip) {
        return;
    }
    controller->state = state;
    // Only update state colors if trippy mode is not active
    if (!controller->trippy_active) {
        update_state(controller);
    }
}

void led_controller_set_pixel_color(led_controller_t *controller, uint8_t pixel_index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!controller || !controller->strip || pixel_index >= controller->led_count) {
        return;
    }
    set_pixel_scaled(controller, pixel_index, red, green, blue);
    led_strip_refresh(controller->strip);
}

void led_controller_shutdown(led_controller_t *controller)
{
    if (!controller || !controller->strip) {
        return;
    }
    led_controller_stop_trippy_fade(controller);
    led_strip_clear(controller->strip);
    led_strip_del(controller->strip);
    controller->strip = NULL;
}

// Convert HSV to RGB for smooth color transitions
static void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int i = (int)(h * 6.0f);
    float f = (h * 6.0f) - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    
    i = i % 6;
    
    switch (i) {
        case 0: *r = (uint8_t)(v * 255); *g = (uint8_t)(t * 255); *b = (uint8_t)(p * 255); break;
        case 1: *r = (uint8_t)(q * 255); *g = (uint8_t)(v * 255); *b = (uint8_t)(p * 255); break;
        case 2: *r = (uint8_t)(p * 255); *g = (uint8_t)(v * 255); *b = (uint8_t)(t * 255); break;
        case 3: *r = (uint8_t)(p * 255); *g = (uint8_t)(q * 255); *b = (uint8_t)(v * 255); break;
        case 4: *r = (uint8_t)(t * 255); *g = (uint8_t)(p * 255); *b = (uint8_t)(v * 255); break;
        case 5: *r = (uint8_t)(v * 255); *g = (uint8_t)(p * 255); *b = (uint8_t)(q * 255); break;
        default: *r = 0; *g = 0; *b = 0; break;
    }
}

void led_controller_start_trippy_fade(led_controller_t *controller)
{
    if (!controller || !controller->strip) {
        return;
    }
    controller->trippy_active = true;
    controller->trippy_time = 0.0f;
    ESP_LOGI(TAG, "Trippy fade animation started");
}

void led_controller_stop_trippy_fade(led_controller_t *controller)
{
    if (!controller || !controller->strip) {
        return;
    }
    controller->trippy_active = false;
    // Restore normal state colors
    update_state(controller);
    ESP_LOGI(TAG, "Trippy fade animation stopped");
}

void led_controller_update_trippy_fade(led_controller_t *controller)
{
    if (!controller || !controller->strip || !controller->trippy_active) {
        return;
    }
    
    // Update animation time
    controller->trippy_time += controller->trippy_speed;
    if (controller->trippy_time > 1000.0f) {
        controller->trippy_time = 0.0f; // Reset to prevent overflow
    }
    
    // Animate ALL LEDs (including status LEDs)
    if (controller->led_count == 0) {
        return;
    }
    
    // Sunrise/Sunset cycle speed - full cycle duration
    // Make it faster and more visible: cycle completes in ~15 seconds instead of 31
    float cycle_speed = 0.16f;  // Doubled for faster, more visible cycle
    float cycle_position = fmodf(controller->trippy_time * cycle_speed, 1.0f);  // 0.0 to 1.0
    
    // Smooth easing function for natural transitions (ease-in-out smoothstep)
    // Inline smoothstep: t * t * (3.0f - 2.0f * t)
    
    // Map cycle position to time of day with smooth transitions:
    // 0.0: Night (dark blue-purple) - matches end of cycle for smooth wraparound
    // 0.0-0.3: Sunrise (night -> warm colors -> bright)
    // 0.3-0.5: Day (bright white, peak brightness)
    // 0.5-0.7: Sunset (bright -> warm colors -> dim)
    // 0.7-1.0: Evening/Night (dim warm colors -> night)
    // 1.0: Night (dark blue-purple) - matches start of cycle for smooth wraparound
    
    float hue, saturation, brightness;
    
    // Handle very beginning (0.0) and very end (1.0) to ensure smooth wraparound
    if (cycle_position < 0.001f || cycle_position >= 0.999f) {
        // At both start (0.0) and end (1.0), we're at night state
        float night_hue = 0.68f;  // Deep blue-purple
        hue = night_hue;
        saturation = 0.3f;  // Low saturation (night)
        brightness = 0.05f;  // Very dark
    } else if (cycle_position < 0.3f) {
        // Sunrise: Night -> Warm colors -> Bright
        float t = cycle_position / 0.3f;  // 0.0 to 1.0
        float phase = t * t * (3.0f - 2.0f * t);  // Smoothstep easing
        // Start: very dark blue-purple (night), End: warm orange/red at full brightness
        float night_hue = 0.68f;  // Deep blue-purple (night)
        float sunrise_hue = 0.08f;  // Orange-red (sunrise)
        // Smooth hue transition from night to sunrise
        hue = night_hue + (sunrise_hue - night_hue) * phase;
        // Handle hue wraparound smoothly
        if (hue < 0.0f) hue += 1.0f;
        if (hue >= 1.0f) hue -= 1.0f;
        saturation = 0.3f + phase * 0.7f;  // 0.3 to 1.0 (night to vibrant sunrise)
        brightness = 0.05f + phase * 0.85f;  // 0.05 to 0.9 (dark to bright)
    } else if (cycle_position < 0.5f) {
        // Day: Peak brightness, transitioning from sunrise to pure white
        float t = (cycle_position - 0.3f) / 0.2f;  // 0.0 to 1.0
        float phase = t * t * (3.0f - 2.0f * t);  // Smoothstep easing
        float sunrise_hue = 0.08f;  // Orange-red
        float day_hue = 0.15f;  // Slight yellow tint for warm white
        hue = sunrise_hue + (day_hue - sunrise_hue) * phase;
        saturation = 1.0f - phase * 0.98f;  // 1.0 to 0.02 (color to pure white, more dramatic)
        brightness = 0.85f + phase * 0.15f;  // 0.85 to 1.0 (very bright, more contrast)
    } else if (cycle_position < 0.7f) {
        // Sunset: Bright white -> Warm colors -> Dim
        float t = (cycle_position - 0.5f) / 0.2f;  // 0.0 to 1.0
        float phase = t * t * (3.0f - 2.0f * t);  // Smoothstep easing
        float day_hue = 0.15f;  // Yellow-white
        float sunset_hue = 0.08f;  // Orange-red
        hue = day_hue + (sunset_hue - day_hue) * phase;
        saturation = 0.02f + phase * 0.98f;  // 0.02 to 1.0 (white to full color, more dramatic)
        brightness = 1.0f - phase * 0.45f;  // 1.0 to 0.55 (bright to dim, more visible change)
    } else {
        // Evening/Night: Dim warm colors -> Night (ending at same state as start)
        float t = (cycle_position - 0.7f) / 0.3f;  // 0.0 to 1.0
        float phase = t * t * (3.0f - 2.0f * t);  // Smoothstep easing
        float sunset_hue = 0.08f;  // Orange-red
        float night_hue = 0.68f;  // Deep blue-purple
        // Smooth hue transition from sunset to night
        hue = sunset_hue + (night_hue - sunset_hue) * phase;
        // Handle hue wraparound smoothly
        if (hue < 0.0f) hue += 1.0f;
        if (hue >= 1.0f) hue -= 1.0f;
        saturation = 1.0f - phase * 0.7f;  // 1.0 to 0.3 (losing color more visibly)
        brightness = 0.55f - phase * 0.5f;  // 0.55 to 0.05 (more visible dim to dark transition)
        
        // Ensure we end exactly at night state (matching start) for smooth wraparound
        if (cycle_position >= 0.999f) {
            hue = night_hue;
            saturation = 0.3f;
            brightness = 0.05f;
        }
    }
    
    // Clamp values
    if (saturation < 0.0f) saturation = 0.0f;
    if (saturation > 1.0f) saturation = 1.0f;
    if (brightness < 0.05f) brightness = 0.05f;  // Very dark but not completely off
    if (brightness > 1.0f) brightness = 1.0f;
    if (hue < 0.0f) hue += 1.0f;
    if (hue >= 1.0f) hue -= 1.0f;
    
    // Apply same color to all LEDs for uniform cycle
    uint8_t r, g, b;
    hsv_to_rgb(hue, saturation, brightness, &r, &g, &b);
    
    for (uint8_t i = 0; i < controller->led_count; ++i) {
        set_pixel_scaled(controller, i, r, g, b);
    }
    
    led_strip_refresh(controller->strip);
}
