#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int id;
    gpio_num_t gpio;
    bool active_low;
} button_service_button_t;

typedef void (*button_service_cb_t)(int button_id, void *ctx);

typedef struct button_service button_service_t;

typedef struct {
    const button_service_button_t *buttons;
    size_t button_count;
    button_service_cb_t callback;
    void *callback_ctx;
    uint32_t debounce_ms;
} button_service_config_t;

button_service_t *button_service_start(const button_service_config_t *config);
void button_service_stop(button_service_t *service);

#ifdef __cplusplus
}
#endif
