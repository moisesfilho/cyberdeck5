#pragma once
#include <cstddef>
#include <cstdint>
namespace cyberdeck_audio_output {
struct backend {
    void *context;
    void *(*initialize)(void *context);
    bool (*mute)(void *context, void *codec, bool enabled);
    bool (*open)(void *context, void *codec, std::uint32_t sample_rate, std::uint8_t channels);
    bool (*set_volume)(void *context, void *codec, std::uint8_t volume);
    bool (*write)(void *context, void *codec, const std::int16_t *samples, std::size_t sample_count);
    bool (*close)(void *context, void *codec);
    bool (*headphone_connected)(void *context);
    bool (*set_speaker_amplifier)(void *context, bool enabled);
};
bool install_backend(const backend &device);
bool start(std::uint32_t sample_rate, std::uint8_t channels, std::uint8_t volume);
bool write(const std::int16_t *samples, std::size_t sample_count);
bool set_volume(std::uint8_t volume);
void stop();
}
