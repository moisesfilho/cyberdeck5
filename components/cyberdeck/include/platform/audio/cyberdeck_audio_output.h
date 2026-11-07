#pragma once
#include <cstddef>
#include <cstdint>
namespace cyberdeck_audio_output {
struct route_timing {
    bool attempted = false;
    std::uint64_t elapsed_us = 0;
};
struct write_timing {
    route_timing route{};
    bool backend_attempted = false;
    std::uint64_t backend_us = 0;
};
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

/* Optional USB Audio Class sink. When installed and reporting availability, it
 * takes precedence over the on-board codec for the next start(); a failure to
 * open it falls back to the speaker so playback never regresses. */
struct usb_output_hooks {
    bool (*available)();
    bool (*start)(std::uint32_t sample_rate, std::uint8_t channels);
    bool (*set_volume)(std::uint8_t volume);
    bool (*write)(const std::int16_t *samples, std::size_t sample_count);
    void (*stop)();
};

bool install_backend(const backend &device);
bool install_usb_output(const usb_output_hooks &hooks);
bool start(std::uint32_t sample_rate, std::uint8_t channels, std::uint8_t volume,
           route_timing *route = nullptr);
bool write(const std::int16_t *samples, std::size_t sample_count, write_timing *timing = nullptr);
#ifndef ESP_PLATFORM
using clock_us_fn = std::uint64_t (*)();
void set_clock_for_test(clock_us_fn clock);
#endif
bool set_volume(std::uint8_t volume);
void stop();
}
