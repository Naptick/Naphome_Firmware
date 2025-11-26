#include "button_service.h"

#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BUTTON_SERVICE_MAX_BUTTONS 4

typedef struct {
    const button_service_button_t *def;
    button_service_t *owner;
    uint64_t last_press_us;
} button_info_t;

typedef struct {
    int id;
} button_event_t;

struct button_service {
    button_info_t buttons[BUTTON_SERVICE_MAX_BUTTONS];
    size_t button_count;
    button_service_cb_t callback;
    void *callback_ctx;
    uint32_t debounce_us;
    QueueHandle_t queue;
    TaskHandle_t task;
};

static const char *TAG = "button_service";

static void button_service_task(void *arg)
{
    button_service_t *service = (button_service_t *)arg;
    button_event_t evt;
    while (xQueueReceive(service->queue, &evt, portMAX_DELAY) == pdTRUE) {
        if (service->callback) {
            service->callback(evt.id, service->callback_ctx);
        }
    }
    vTaskDelete(NULL);
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    button_info_t *info = (button_info_t *)arg;
    if (!info || !info->def) {
        return;
    }
    button_service_t *service = info->owner;
    if (!service || !service->queue) {
        return;
    }
    int level = gpio_get_level(info->def->gpio);
    bool pressed = info->def->active_low ? (level == 0) : (level == 1);
    if (!pressed) {
        return;
    }
    uint64_t now = esp_timer_get_time();
    if (now - info->last_press_us < service->debounce_us) {
        return;
    }
    info->last_press_us = now;
    button_event_t evt = { .id = info->def->id };
    xQueueSendFromISR(service->queue, &evt, NULL);
}

button_service_t *button_service_start(const button_service_config_t *config)
{
    ESP_RETURN_ON_FALSE(config && config->buttons && config->button_count > 0, NULL, TAG, "invalid config");
    ESP_RETURN_ON_FALSE(config->button_count <= BUTTON_SERVICE_MAX_BUTTONS, NULL, TAG, "too many buttons");

    button_service_t *service = calloc(1, sizeof(button_service_t));
    if (!service) {
        return NULL;
    }

    service->button_count = config->button_count;
    service->callback = config->callback;
    service->callback_ctx = config->callback_ctx;
    service->debounce_us = (config->debounce_ms ? config->debounce_ms : 75) * 1000;
    service->queue = xQueueCreate(8, sizeof(button_event_t));

    if (!service->queue) {
        free(service);
        return NULL;
    }

    for (size_t i = 0; i < config->button_count; ++i) {
        service->buttons[i].def = &config->buttons[i];
        service->buttons[i].last_press_us = 0;
        service->buttons[i].owner = service;
    }

    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        gpio_install_isr_service(0);
        isr_service_installed = true;
    }

    for (size_t i = 0; i < service->button_count; ++i) {
        const button_service_button_t *btn = service->buttons[i].def;
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << btn->gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = btn->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = btn->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
            .intr_type = btn->active_low ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE,
        };
        gpio_config(&io_conf);

        gpio_isr_handler_add(btn->gpio, gpio_isr_handler, &service->buttons[i]);
    }

    BaseType_t rc = xTaskCreate(button_service_task, "button_service", 2048, service, 5, &service->task);
    if (rc != pdPASS) {
        button_service_stop(service);
        return NULL;
    }

    ESP_LOGI(TAG, "Button service started with %d inputs", (int)service->button_count);
    return service;
}

void button_service_stop(button_service_t *service)
{
    if (!service) {
        return;
    }
    for (size_t i = 0; i < service->button_count; ++i) {
        if (service->buttons[i].def) {
            gpio_isr_handler_remove(service->buttons[i].def->gpio);
        }
    }
    if (service->task) {
        vTaskDelete(service->task);
    }
    if (service->queue) {
        vQueueDelete(service->queue);
    }
    free(service);
}
