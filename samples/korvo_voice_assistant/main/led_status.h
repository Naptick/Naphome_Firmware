#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Update AWS IoT LED status based on connection state
 * 
 * @param connected true if AWS IoT is connected, false if disconnected
 */
void aws_led_set_connected(bool connected);

#ifdef __cplusplus
}
#endif
