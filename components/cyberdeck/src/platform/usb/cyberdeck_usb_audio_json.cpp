#include "platform/usb/cyberdeck_usb_audio_host.h"

#include <cstdio>
#include <cstring>

namespace cyberdeck_usb_audio_host {
namespace {
bool append(char *buffer, std::size_t capacity, std::size_t &offset, const char *text) {
    const std::size_t length = std::strlen(text);
    if (offset >= capacity || length >= capacity - offset) return false;
    std::memcpy(buffer + offset, text, length);
    offset += length;
    buffer[offset] = '\0';
    return true;
}

bool append_json_string(char *buffer, std::size_t capacity, std::size_t &offset, const char *text,
                        std::size_t max_length) {
    if (!append(buffer, capacity, offset, "\"")) return false;
    for (std::size_t index = 0; index < max_length && text[index] != '\0'; ++index) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (ch == '"' || ch == '\\') {
            if (offset + 2 >= capacity) return false;
            buffer[offset++] = '\\';
            buffer[offset++] = static_cast<char>(ch);
            buffer[offset] = '\0';
        } else if (ch < 0x20) {
            if (!append(buffer, capacity, offset, "?")) return false;
        } else {
            if (offset + 1 >= capacity) return false;
            buffer[offset++] = static_cast<char>(ch);
            buffer[offset] = '\0';
        }
    }
    return append(buffer, capacity, offset, "\"");
}
} // namespace

const char *state_name(state value) {
    switch (value) {
    case state::stopped: return "stopped";
    case state::starting: return "starting";
    case state::waiting: return "waiting";
    case state::inspecting: return "inspecting";
    case state::ready: return "ready";
    case state::unsupported: return "unsupported";
    case state::error: return "error";
    }
    return "error";
}

int format_json(char *buffer, std::size_t capacity, const snapshot &value) {
    if (buffer == nullptr || capacity == 0 || value.format_count > k_max_formats) return -1;
    buffer[0] = '\0';
    std::size_t offset = 0;
    char number[320];
    int count = std::snprintf(number, sizeof(number),
                              "{\"state\":\"%s\",\"port\":\"usb-a-host\",\"address\":%u,"
                              "\"interface\":%u,\"vid\":%u,\"pid\":%u,\"usb_version\":%u,"
                              "\"audio_spec\":%u,\"playback_streams\":%u,\"recording_streams\":%u,"
                              "\"audio_control\":%s,\"manufacturer\":",
                              state_name(value.status), value.address, value.interface_number,
                              value.vendor_id, value.product_id, value.usb_version, value.audio_spec,
                              value.playback_streams, value.recording_streams,
                              value.audio_control ? "true" : "false");
    if (count < 0 || static_cast<std::size_t>(count) >= sizeof(number) || !append(buffer, capacity, offset, number) ||
        !append_json_string(buffer, capacity, offset, value.manufacturer, sizeof(value.manufacturer)) ||
        !append(buffer, capacity, offset, ",\"product\":") ||
        !append_json_string(buffer, capacity, offset, value.product, sizeof(value.product)) ||
        !append(buffer, capacity, offset, ",\"formats\":["))
        return -1;

    for (std::size_t index = 0; index < value.format_count; ++index) {
        const audio_format &format = value.formats[index];
        if (index != 0 && !append(buffer, capacity, offset, ",")) return -1;
        count = std::snprintf(number, sizeof(number),
                              "{\"channels\":%u,\"subframe_bytes\":%u,\"bits\":%u,"
                              "\"sample_rate_min\":%lu,\"sample_rate_max\":%lu}",
                              format.channels, format.subframe_bytes, format.bit_resolution,
                              static_cast<unsigned long>(format.sample_rate_min),
                              static_cast<unsigned long>(format.sample_rate_max));
        if (count < 0 || static_cast<std::size_t>(count) >= sizeof(number) ||
            !append(buffer, capacity, offset, number))
            return -1;
    }
    if (!append(buffer, capacity, offset, "]")) return -1;
    count = std::snprintf(number, sizeof(number), ",\"error_code\":%lu,\"updated_at_us\":%llu}",
                          static_cast<unsigned long>(value.error_code),
                          static_cast<unsigned long long>(value.updated_at_us));
    if (count < 0 || static_cast<std::size_t>(count) >= sizeof(number) ||
        !append(buffer, capacity, offset, number))
        return -1;
    return static_cast<int>(offset);
}

} // namespace cyberdeck_usb_audio_host
