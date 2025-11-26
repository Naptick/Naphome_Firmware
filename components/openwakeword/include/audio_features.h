#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct audio_features audio_features_t;

audio_features_t *audio_features_init(uint32_t sample_rate);

esp_err_t audio_features_extract_melspectrogram(audio_features_t *features,
                                                  const int16_t *audio_samples,
                                                  size_t sample_count,
                                                  float *melspectrogram_out,
                                                  size_t *melspectrogram_size_out);

void audio_features_deinit(audio_features_t *features);