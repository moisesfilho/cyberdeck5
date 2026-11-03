#pragma once
#include <cstdint>
using esp_codec_dev_handle_t = void *;
struct esp_codec_dev_sample_info_t {
    int bits_per_sample{};
    int channel{};
    int sample_rate{};
    int mclk_multiple{};
};
#define ESP_CODEC_DEV_OK 0
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t, bool);
int esp_codec_dev_open(esp_codec_dev_handle_t, const esp_codec_dev_sample_info_t *);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t, int);
int esp_codec_dev_write(esp_codec_dev_handle_t, std::int16_t *, int);
int esp_codec_dev_close(esp_codec_dev_handle_t);
