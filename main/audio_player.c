#include "audio_player.h"

#include <string.h>

#include "driver/i2c.h"
// Compatibility: use old I2C API for ESP-IDF v4.4
#define i2c_master_bus_handle_t i2c_port_t
#define i2c_master_dev_handle_t i2c_cmd_handle_t
#include "esp_check.h"
#include "esp_log.h"

#define AUDIO_PLAYER_I2C_FREQ_HZ 100000
#define ES8388_ADDR 0x20

// ES8388 register definitions
#define ES8388_CONTROL1 0x00
#define ES8388_CONTROL2 0x01
#define ES8388_CHIPPOWER 0x02
#define ES8388_ADCPOWER 0x03
#define ES8388_DACPOWER 0x04
#define ES8388_CHIPLOPOW1 0x05
#define ES8388_CHIPLOPOW2 0x06
#define ES8388_ANAVOLMANAG 0x07
#define ES8388_MASTERMODE 0x08
#define ES8388_ADCCONTROL1 0x09
#define ES8388_ADCCONTROL2 0x0a
#define ES8388_ADCCONTROL3 0x0b
#define ES8388_ADCCONTROL4 0x0c
#define ES8388_ADCCONTROL5 0x0d
#define ES8388_ADCCONTROL8 0x10
#define ES8388_ADCCONTROL9 0x11
#define ES8388_DACCONTROL1 0x17
#define ES8388_DACCONTROL2 0x18
#define ES8388_DACCONTROL3 0x19
#define ES8388_DACCONTROL4 0x1a
#define ES8388_DACCONTROL5 0x1b
#define ES8388_DACCONTROL16 0x26
#define ES8388_DACCONTROL17 0x27
#define ES8388_DACCONTROL20 0x2a
#define ES8388_DACCONTROL21 0x2b
#define ES8388_DACCONTROL23 0x2d
#define ES8388_DACCONTROL24 0x2e
#define ES8388_DACCONTROL25 0x2f
#define ES8388_DACCONTROL26 0x30
#define ES8388_DACCONTROL27 0x31

typedef struct {
    bool initialized;
    audio_player_config_t cfg;
    int current_sample_rate;
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t i2c_dev;
} audio_player_state_t;

static audio_player_state_t s_audio;
static const char *TAG = "audio_player";

static esp_err_t es8388_write_reg(uint8_t reg, uint8_t value)
{
    if (s_audio.i2c_bus == I2C_NUM_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    // Use ESP-IDF v4.4 I2C API
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES8388_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(s_audio.i2c_bus, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 write failed reg=0x%02x err=%s", reg, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t es8388_init(void)
{
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_CONTROL2, 0x50), TAG, "ctrl2");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_CHIPPOWER, 0x00), TAG, "chip power");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_MASTERMODE, 0x00), TAG, "master mode");

    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACPOWER, 0x3e), TAG, "dac power");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_CONTROL1, 0x12), TAG, "ctrl1");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL1, 0x18), TAG, "dac ctrl1");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL2, 0x02), TAG, "dac ctrl2");

    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL16, 0x1b), TAG, "dac ctrl16");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL17, 0x90), TAG, "dac ctrl17");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL20, 0x90), TAG, "dac ctrl20");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL21, 0x80), TAG, "dac ctrl21");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL23, 0x00), TAG, "dac ctrl23");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL5, 0x00), TAG, "dac ctrl5");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL4, 0x00), TAG, "dac ctrl4");

    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_ADCPOWER, 0xff), TAG, "adc power");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_ADCCONTROL1, 0x88), TAG, "adc ctrl1");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_ADCCONTROL2, 0xf0), TAG, "adc ctrl2");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_ADCCONTROL3, 0x80), TAG, "adc ctrl3");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_ADCCONTROL4, 0x0e), TAG, "adc ctrl4");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_ADCCONTROL5, 0x02), TAG, "adc ctrl5");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_ADCCONTROL8, 0x20), TAG, "adc ctrl8");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_ADCCONTROL9, 0x20), TAG, "adc ctrl9");

    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL24, 0x1e), TAG, "dac ctrl24");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL25, 0x1e), TAG, "dac ctrl25");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL26, 0x1e), TAG, "dac ctrl26");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL27, 0x1e), TAG, "dac ctrl27");

    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACPOWER, 0x3c), TAG, "dac power final");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_DACCONTROL3, 0x00), TAG, "dac ctrl3");
    ESP_RETURN_ON_ERROR(es8388_write_reg(ES8388_ADCPOWER, 0x00), TAG, "adc power on");
    return ESP_OK;
}

static esp_err_t configure_i2c(const audio_player_config_t *cfg)
{
    // Initialize I2C master bus on I2C_NUM_1 (sensor_manager uses I2C_NUM_0 on GPIO 21/22)
    // Audio codec uses GPIO 1/2, so we need a separate I2C port
    // Use ESP-IDF v4.4 I2C API
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = cfg->i2c_sda_gpio,
        .scl_io_num = cfg->i2c_scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = AUDIO_PLAYER_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM_1, &i2c_conf), TAG, "i2c param config");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_NUM_1, i2c_conf.mode, 0, 0, 0), TAG, "i2c driver install");
    s_audio.i2c_bus = I2C_NUM_1;
    s_audio.i2c_dev = NULL; // Not used with v4.4 API
    
    return ESP_OK;
}

static esp_err_t configure_i2s(const audio_player_config_t *cfg)
{
    i2s_config_t i2s_conf = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = cfg->default_sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 256,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = cfg->default_sample_rate * 256,
    };

    i2s_pin_config_t pin_conf = {
        .mck_io_num = cfg->mclk_gpio,
        .bck_io_num = cfg->bclk_gpio,
        .ws_io_num = cfg->lrclk_gpio,
        .data_out_num = cfg->data_gpio,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };

    ESP_RETURN_ON_ERROR(i2s_driver_install(cfg->i2s_port, &i2s_conf, 0, NULL), TAG, "i2s install");
    ESP_RETURN_ON_ERROR(i2s_set_pin(cfg->i2s_port, &pin_conf), TAG, "i2s pins");
    ESP_RETURN_ON_ERROR(i2s_zero_dma_buffer(cfg->i2s_port), TAG, "i2s zero");
    return ESP_OK;
}

esp_err_t audio_player_init(const audio_player_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "cfg required");
    ESP_RETURN_ON_FALSE(cfg->bclk_gpio >= 0 && cfg->lrclk_gpio >= 0 && cfg->data_gpio >= 0,
                        ESP_ERR_INVALID_ARG, TAG, "invalid pins");

    if (s_audio.initialized) {
        return ESP_OK;
    }

    s_audio.cfg = *cfg;
    s_audio.current_sample_rate = cfg->default_sample_rate > 0 ? cfg->default_sample_rate : 44100;

    ESP_RETURN_ON_ERROR(configure_i2c(cfg), TAG, "i2c setup");
    ESP_RETURN_ON_ERROR(configure_i2s(cfg), TAG, "i2s setup");
    ESP_RETURN_ON_ERROR(es8388_init(), TAG, "codec init");

    s_audio.initialized = true;
    ESP_LOGI(TAG, "Audio player ready (sr=%d)", s_audio.current_sample_rate);
    return ESP_OK;
}

static esp_err_t ensure_sample_rate(int sample_rate_hz)
{
    if (!s_audio.initialized || sample_rate_hz <= 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sample_rate_hz == s_audio.current_sample_rate) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(
        i2s_set_clk(s_audio.cfg.i2s_port,
                    sample_rate_hz,
                    I2S_BITS_PER_SAMPLE_16BIT,
                    I2S_CHANNEL_STEREO),
        TAG,
        "set clk");
    s_audio.current_sample_rate = sample_rate_hz;
    ESP_LOGI(TAG, "Playback sample rate -> %d Hz", sample_rate_hz);
    return ESP_OK;
}

static esp_err_t write_pcm_frames(const int16_t *samples, size_t sample_count, int num_channels)
{
    ESP_RETURN_ON_FALSE(samples && sample_count > 0, ESP_ERR_INVALID_ARG, TAG, "bad pcm args");
    ESP_RETURN_ON_FALSE(num_channels == 1 || num_channels == 2, ESP_ERR_INVALID_ARG, TAG, "channels");

    const size_t chunk_frames = 256;
    int16_t stereo_buffer[chunk_frames * 2];

    size_t frames_written = 0;
    while (frames_written < sample_count) {
        size_t frames_this = chunk_frames;
        if (frames_this > sample_count - frames_written) {
            frames_this = sample_count - frames_written;
        }

        if (num_channels == 1) {
            for (size_t i = 0; i < frames_this; ++i) {
                int16_t sample = samples[frames_written + i];
                stereo_buffer[2 * i] = sample;
                stereo_buffer[2 * i + 1] = sample;
            }
        } else {
            memcpy(stereo_buffer,
                   &samples[(frames_written)*2],
                   frames_this * sizeof(int16_t) * 2);
        }

        size_t bytes_to_write = frames_this * sizeof(int16_t) * 2;
        size_t total_written = 0;
        while (total_written < bytes_to_write) {
            size_t bytes_written = 0;
            esp_err_t err = i2s_write(s_audio.cfg.i2s_port,
                                      (uint8_t *)stereo_buffer + total_written,
                                      bytes_to_write - total_written,
                                      &bytes_written,
                                      portMAX_DELAY);
            if (err != ESP_OK) {
                return err;
            }
            total_written += bytes_written;
        }
        frames_written += frames_this;
    }
    return ESP_OK;
}

typedef struct __attribute__((packed)) {
    char chunk_id[4];
    uint32_t chunk_size;
    char format[4];
} wav_header_t;

typedef struct __attribute__((packed)) {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_body_t;

esp_err_t audio_player_play_wav(const uint8_t *wav_data, size_t wav_len)
{
    ESP_RETURN_ON_FALSE(s_audio.initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_FALSE(wav_data && wav_len > sizeof(wav_header_t), ESP_ERR_INVALID_ARG, TAG, "bad wav");

    const uint8_t *ptr = wav_data;
    const uint8_t *end = wav_data + wav_len;
    const wav_header_t *hdr = (const wav_header_t *)ptr;
    if (memcmp(hdr->chunk_id, "RIFF", 4) != 0 || memcmp(hdr->format, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ptr += sizeof(wav_header_t);

    wav_fmt_body_t fmt = {0};
    bool fmt_found = false;
    const uint8_t *data_ptr = NULL;
    uint32_t data_size = 0;

    while (ptr + 8 <= end) {
        char chunk_id[4];
        memcpy(chunk_id, ptr, 4);
        uint32_t chunk_size = *(const uint32_t *)(ptr + 4);
        ptr += 8;
        if (ptr + chunk_size > end) {
            return ESP_ERR_INVALID_ARG;
        }

        if (!fmt_found && memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < sizeof(wav_fmt_body_t)) {
                return ESP_ERR_INVALID_ARG;
            }
            memcpy(&fmt, ptr, sizeof(wav_fmt_body_t));
            fmt_found = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            data_ptr = ptr;
            data_size = chunk_size;
            break;
        } else {
            uint32_t advance = chunk_size;
            if (advance & 1) {
                advance++;
            }
            ptr += advance;
        }
    }

    ESP_RETURN_ON_FALSE(fmt_found && data_ptr, ESP_ERR_INVALID_ARG, TAG, "wav missing fmt/data");
    ESP_RETURN_ON_FALSE(fmt.audio_format == 1, ESP_ERR_NOT_SUPPORTED, TAG, "PCM required");
    ESP_RETURN_ON_FALSE(fmt.bits_per_sample == 16, ESP_ERR_NOT_SUPPORTED, TAG, "16-bit required");

    size_t sample_count = data_size / (fmt.bits_per_sample / 8);
    const int16_t *samples = (const int16_t *)data_ptr;

    ESP_RETURN_ON_ERROR(ensure_sample_rate(fmt.sample_rate), TAG, "sr");
    return write_pcm_frames(samples, sample_count / fmt.num_channels, fmt.num_channels);
}

esp_err_t audio_player_submit_pcm(const int16_t *samples,
                                  size_t sample_count,
                                  int sample_rate_hz,
                                  int num_channels)
{
    ESP_RETURN_ON_FALSE(s_audio.initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_ERROR(ensure_sample_rate(sample_rate_hz), TAG, "sr");
    return write_pcm_frames(samples, sample_count, num_channels);
}

void audio_player_shutdown(void)
{
    if (!s_audio.initialized) {
        return;
    }
    i2s_driver_uninstall(s_audio.cfg.i2s_port);
    
    // Clean up I2C
    if (s_audio.i2c_bus != I2C_NUM_MAX) {
        i2c_driver_delete(s_audio.i2c_bus);
        s_audio.i2c_bus = I2C_NUM_MAX;
    }
    s_audio.i2c_dev = NULL;
    
    memset(&s_audio, 0, sizeof(s_audio));
}
