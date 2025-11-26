#include <stdbool.h>

#include "esp_log.h"

#include "device_state.h"

static const char *TAG = "aws_led_stub";

// Provided for Somnus MQTT linkage; actual LED feedback is not wired on Atom Echo base yet.
void aws_led_set_connected(bool connected)
{
    ESP_LOGI(TAG, "AWS IoT link %s", connected ? "up" : "down");
    device_state_set_aws(connected);
}
