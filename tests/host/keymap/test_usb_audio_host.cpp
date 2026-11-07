#include "platform/usb/cyberdeck_usb_audio_host.h"

#include <cassert>
#include <cstring>

int main() {
    using namespace cyberdeck_usb_audio_host;
    snapshot value{};
    value.status = state::ready;
    value.address = 3;
    value.interface_number = 2;
    value.vendor_id = 0x1234;
    value.product_id = 0xabcd;
    value.usb_version = 0x0200;
    value.audio_spec = 1;
    value.playback_streams = 2;
    value.recording_streams = 1;
    value.audio_control = true;
    std::strcpy(value.manufacturer, "Generic \"Audio\"");
    std::strcpy(value.product, "USB\\DSP\nHeadset");
    value.format_count = 1;
    value.formats[0] = {2, 2, 16, 44100, 48000};
    value.updated_at_us = 987654321;

    char json[1024]{};
    const int length = format_json(json, sizeof(json), value);
    assert(length > 0);
    assert(static_cast<std::size_t>(length) == std::strlen(json));
    assert(std::strstr(json, "\"port\":\"usb-a-host\"") != nullptr);
    assert(std::strstr(json, "\"usb_version\":512") != nullptr);
    assert(std::strstr(json, "\"audio_spec\":1,\"playback_streams\":2,\"recording_streams\":1,"
                             "\"audio_control\":true") != nullptr);
    assert(std::strstr(json, "\"manufacturer\":\"Generic \\\"Audio\\\"\"") != nullptr);
    assert(std::strstr(json, "\"product\":\"USB\\\\DSP?Headset\"") != nullptr);
    assert(std::strstr(json, "\"sample_rate_min\":44100,\"sample_rate_max\":48000") != nullptr);
    assert(std::strstr(json, "\"updated_at_us\":987654321") != nullptr);

    char too_small[8] = "KEEP";
    assert(format_json(too_small, sizeof(too_small), value) == -1);
    assert(too_small[sizeof(too_small) - 1] == '\0');
    assert(format_json(nullptr, 0, value) == -1);
    value.format_count = k_max_formats + 1;
    assert(format_json(json, sizeof(json), value) == -1);
}
