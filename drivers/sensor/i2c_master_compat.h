/**
 * @file i2c_master_compat.h
 * @brief Compatibility header for ESP-IDF v4.4 to support i2c_master.h API
 * 
 * This provides compatibility for sensor drivers that use ESP-IDF v5.0 I2C master API
 * when building with ESP-IDF v4.4.
 */

#ifndef I2C_MASTER_COMPAT_H
#define I2C_MASTER_COMPAT_H

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "hal/i2c_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Type compatibility
typedef i2c_port_t i2c_master_bus_handle_t;
typedef i2c_cmd_handle_t i2c_master_dev_handle_t;

// Type definitions for ESP-IDF v4.4 compatibility
typedef enum {
    I2C_ADDR_BIT_LEN_7BIT = 7,
    I2C_ADDR_BIT_LEN_10BIT = 10,
} i2c_addr_bit_len_t;

typedef enum {
    I2C_CLK_SRC_DEFAULT = 0,
    I2C_CLK_SRC_APB = 1,
} i2c_clock_source_t;

// Configuration structures
typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t sda_io_num;
    gpio_num_t scl_io_num;
    i2c_clock_source_t clk_source;
    uint8_t glitch_ignore_cnt;
    struct {
        bool enable_internal_pullup;
    } flags;
} i2c_master_bus_config_t;

typedef struct {
    i2c_addr_bit_len_t dev_addr_length;
    uint8_t device_address;
    uint32_t scl_speed_hz;
} i2c_device_config_t;

// Function compatibility wrappers
static inline esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *ret_bus_handle) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = config->sda_io_num,
        .scl_io_num = config->scl_io_num,
        .sda_pullup_en = config->flags.enable_internal_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .scl_pullup_en = config->flags.enable_internal_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .master.clk_speed = 100000, // Default speed
    };
    esp_err_t ret = i2c_param_config(config->i2c_port, &conf);
    if (ret != ESP_OK) return ret;
    ret = i2c_driver_install(config->i2c_port, conf.mode, 0, 0, 0);
    if (ret == ESP_OK && ret_bus_handle) {
        *ret_bus_handle = config->i2c_port;
    }
    return ret;
}

static inline esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle, const i2c_device_config_t *dev_config, i2c_master_dev_handle_t *ret_handle) {
    // In v4.4, we don't need to "add" devices - just return a handle
    // The handle is just the port number encoded
    if (ret_handle) {
        *ret_handle = (i2c_master_dev_handle_t)(uintptr_t)((bus_handle << 8) | dev_config->device_address);
    }
    return ESP_OK;
}

static inline esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev_handle, const uint8_t *write_buffer, size_t write_size, int timeout_ms) {
    i2c_port_t port = (i2c_port_t)((uintptr_t)dev_handle >> 8);
    uint8_t addr = (uint8_t)((uintptr_t)dev_handle & 0xFF);
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (write_size > 0) {
        i2c_master_write(cmd, write_buffer, write_size, true);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(timeout_ms > 0 ? timeout_ms : 1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static inline esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev_handle, uint8_t *read_buffer, size_t read_size, int timeout_ms) {
    i2c_port_t port = (i2c_port_t)((uintptr_t)dev_handle >> 8);
    uint8_t addr = (uint8_t)((uintptr_t)dev_handle & 0xFF);
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (read_size > 1) {
        i2c_master_read(cmd, read_buffer, read_size - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, read_buffer + read_size - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(timeout_ms > 0 ? timeout_ms : 1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static inline esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev_handle, const uint8_t *write_buffer, size_t write_size, uint8_t *read_buffer, size_t read_size, int timeout_ms) {
    i2c_port_t port = (i2c_port_t)((uintptr_t)dev_handle >> 8);
    uint8_t addr = (uint8_t)((uintptr_t)dev_handle & 0xFF);
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (write_size > 0) {
        i2c_master_write(cmd, write_buffer, write_size, true);
    }
    i2c_master_start(cmd); // Repeated start
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (read_size > 1) {
        i2c_master_read(cmd, read_buffer, read_size - 1, I2C_MASTER_ACK);
    }
    if (read_size > 0) {
        i2c_master_read_byte(cmd, read_buffer + read_size - 1, I2C_MASTER_NACK);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(timeout_ms > 0 ? timeout_ms : 1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static inline esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev_handle) {
    // No-op in v4.4
    (void)dev_handle;
    return ESP_OK;
}

static inline esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle) {
    return i2c_driver_delete(bus_handle);
}

// Additional convenience functions - use ESP-IDF v4.4 versions directly via macros
// Note: v4.4 has these functions but with different signatures (port + address instead of handle)
// We'll use the handle-based versions we defined above

// Constants
#define I2C_ADDR_BIT_LEN_7 I2C_ADDR_BIT_LEN_7BIT
#define I2C_CLK_SRC_DEFAULT 0

#ifdef __cplusplus
}
#endif

#endif // I2C_MASTER_COMPAT_H
