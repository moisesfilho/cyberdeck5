#pragma once

#include "platform/audio/cyberdeck_audio_presence.h"

#include <cstddef>
#include <cstdint>

namespace cyberdeck_usb_audio_host {

enum class state : std::uint8_t { stopped, starting, waiting, inspecting, ready, unsupported, error };

struct audio_format {
    std::uint8_t channels = 0;
    std::uint8_t subframe_bytes = 0;
    std::uint8_t bit_resolution = 0;
    std::uint32_t sample_rate_min = 0;
    std::uint32_t sample_rate_max = 0;
};

constexpr std::size_t k_max_formats = 8;
constexpr std::size_t k_max_string_bytes = 48;

struct snapshot {
    state status = state::stopped;
    std::uint8_t address = 0;
    std::uint8_t interface_number = 0;
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;
    std::uint16_t usb_version = 0;
    /* 0 = no Audio Control header found, 1 = UAC 1.0 (bcdADC 0x0100),
     * 2 = UAC 2.0 or newer. The class driver only implements UAC 1.0. */
    std::uint8_t audio_spec = 0;
    std::uint8_t playback_streams = 0;
    std::uint8_t recording_streams = 0;
    bool audio_control = false;
    char manufacturer[k_max_string_bytes]{};
    char product[k_max_string_bytes]{};
    audio_format formats[k_max_formats]{};
    std::size_t format_count = 0;
    std::uint32_t error_code = 0;
    std::uint64_t updated_at_us = 0;
};

bool start();
bool stop(std::uint32_t timeout_ms);
bool get_snapshot(snapshot &out);
int format_json(char *buffer, std::size_t capacity, const snapshot &value);
const char *state_name(state value);

/* Playback-capable device on the USB-A port, as last published by the worker.
 * A UAC headset plugged into the port counts even when its formats are not
 * usable (UAC 2.0, unsupported rate): the player pauses either way. Peripherals
 * that are not audio playback devices count as absent, and `unknown` is never
 * treated as absence. */
cyberdeck_audio_presence::presence playback_presence();

/* Authority for "the sink of the open stream is still there": false as soon as
 * the class driver reports the stream disconnected or the published view no
 * longer offers a ready device. Used to decide whether a failed write may fall
 * back to the board codec. */
bool playback_device_present();

} // namespace cyberdeck_usb_audio_host
