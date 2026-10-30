#include "platform/audio/cyberdeck_audio_output.h"
#include "freertos/FreeRTOS.h"
#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
int checks = 0;
int failures = 0;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #condition "\n"; } \
} while (false)

struct codec_probe {
    bool headset = false;
    bool fail_mute_on = false;
    bool fail_mute_off = false;
    bool fail_open = false;
    bool fail_volume = false;
    bool fail_write = false;
    bool fail_close = false;
    bool fail_amplifier = false;
    int initializes = 0;
    int opens = 0;
    int closes = 0;
    int writes = 0;
    int amp_on = 0;
    int amp_off = 0;
    std::vector<std::string> calls;
};
TickType_t now = 0;

void *initialize(void *context) {
    auto &p = *static_cast<codec_probe *>(context);
    ++p.initializes; p.calls.emplace_back("initialize"); return &p;
}
bool mute(void *context, void *, bool enabled) {
    auto &p = *static_cast<codec_probe *>(context);
    p.calls.emplace_back(enabled ? "mute" : "unmute");
    return enabled ? !p.fail_mute_on : !p.fail_mute_off;
}
bool open_codec(void *context, void *, std::uint32_t rate, std::uint8_t channels) {
    auto &p = *static_cast<codec_probe *>(context);
    p.calls.emplace_back("open:" + std::to_string(rate) + ":" + std::to_string(channels));
    ++p.opens; return !p.fail_open;
}
bool set_volume(void *context, void *, std::uint8_t volume) {
    auto &p = *static_cast<codec_probe *>(context);
    p.calls.emplace_back("volume:" + std::to_string(volume)); return !p.fail_volume;
}
bool write_samples(void *context, void *, const std::int16_t *, std::size_t count) {
    auto &p = *static_cast<codec_probe *>(context);
    p.calls.emplace_back("write:" + std::to_string(count)); ++p.writes; return !p.fail_write;
}
bool close_codec(void *context, void *) {
    auto &p = *static_cast<codec_probe *>(context);
    p.calls.emplace_back("close"); ++p.closes; return !p.fail_close;
}
bool headset_connected(void *context) {
    auto &p = *static_cast<codec_probe *>(context); p.calls.emplace_back("headset"); return p.headset;
}
bool set_amplifier(void *context, bool enabled) {
    auto &p = *static_cast<codec_probe *>(context);
    p.calls.emplace_back(enabled ? "amp-on" : "amp-off");
    if (enabled) ++p.amp_on; else ++p.amp_off;
    return !p.fail_amplifier;
}

bool contains_after(const std::vector<std::string> &calls, std::size_t from, const std::string &expected) {
    for (std::size_t i = from; i < calls.size(); ++i) if (calls[i] == expected) return true;
    return false;
}
}

TickType_t xTaskGetTickCount() { return now; }

void *bsp_audio_codec_speaker_init() { return nullptr; }
bool bsp_headphone_is_connected() { return false; }
int bsp_feature_enable(int, bool) { return ESP_OK; }
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t, bool) { return ESP_CODEC_DEV_OK; }
int esp_codec_dev_open(esp_codec_dev_handle_t, const esp_codec_dev_sample_info_t *) { return ESP_CODEC_DEV_OK; }
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t, int) { return ESP_CODEC_DEV_OK; }
int esp_codec_dev_write(esp_codec_dev_handle_t, std::int16_t *, int) { return ESP_CODEC_DEV_OK; }
int esp_codec_dev_close(esp_codec_dev_handle_t) { return ESP_CODEC_DEV_OK; }

int main() {
    codec_probe probe;
    const cyberdeck_audio_output::backend device{
        &probe, initialize, mute, open_codec, set_volume, write_samples,
        close_codec, headset_connected, set_amplifier};
    CHECK(!cyberdeck_audio_output::install_backend({&probe, nullptr, mute, open_codec, set_volume, write_samples,
                                                    close_codec, headset_connected, set_amplifier}));
    CHECK(!cyberdeck_audio_output::install_backend({&probe, initialize, nullptr, open_codec, set_volume, write_samples,
                                                    close_codec, headset_connected, set_amplifier}));
    CHECK(!cyberdeck_audio_output::install_backend({&probe, initialize, mute, nullptr, set_volume, write_samples,
                                                    close_codec, headset_connected, set_amplifier}));
    CHECK(!cyberdeck_audio_output::install_backend({&probe, initialize, mute, open_codec, nullptr, write_samples,
                                                    close_codec, headset_connected, set_amplifier}));
    CHECK(!cyberdeck_audio_output::install_backend({&probe, initialize, mute, open_codec, set_volume, nullptr,
                                                    close_codec, headset_connected, set_amplifier}));
    CHECK(!cyberdeck_audio_output::install_backend({&probe, initialize, mute, open_codec, set_volume, write_samples,
                                                    nullptr, headset_connected, set_amplifier}));
    CHECK(!cyberdeck_audio_output::install_backend({&probe, initialize, mute, open_codec, set_volume, write_samples,
                                                    close_codec, nullptr, set_amplifier}));
    CHECK(!cyberdeck_audio_output::install_backend({&probe, initialize, mute, open_codec, set_volume, write_samples,
                                                    close_codec, headset_connected, nullptr}));
    CHECK(cyberdeck_audio_output::install_backend(device));
    CHECK(!cyberdeck_audio_output::install_backend({}));
    CHECK(!cyberdeck_audio_output::start(7999, 1, 20));
    CHECK(!cyberdeck_audio_output::start(48001, 2, 20));
    CHECK(!cyberdeck_audio_output::start(44100, 3, 20));
    CHECK(!cyberdeck_audio_output::start(44100, 2, 51));
    CHECK(probe.initializes == 0);

    CHECK(cyberdeck_audio_output::start(16000, 1, 20));
    CHECK(probe.initializes == 1 && probe.opens == 1);
    const int opens_before_same_format = probe.opens;
    CHECK(cyberdeck_audio_output::start(16000, 1, 21));
    CHECK(probe.opens == opens_before_same_format);
    probe.fail_volume = true;
    CHECK(!cyberdeck_audio_output::start(16000, 1, 22));
    probe.fail_volume = false;
    CHECK(cyberdeck_audio_output::start(16000, 1, 20));
    CHECK(contains_after(probe.calls, 0, "mute"));
    CHECK(contains_after(probe.calls, 0, "open:16000:1"));
    CHECK(contains_after(probe.calls, 0, "amp-on"));
    const std::int16_t sample = 1;
    CHECK(cyberdeck_audio_output::write(&sample, 1));
    CHECK(!cyberdeck_audio_output::write(nullptr, 1));
    const std::int16_t samples[2305]{};
    CHECK(!cyberdeck_audio_output::write(samples, 0));
    CHECK(!cyberdeck_audio_output::write(samples, 2305));
    CHECK(cyberdeck_audio_output::set_volume(50));
    CHECK(!cyberdeck_audio_output::set_volume(51));

    probe.headset = true;
    now += 499;
    CHECK(cyberdeck_audio_output::write(&sample, 1));
    CHECK(probe.amp_off == 0);
    now += 1;
    CHECK(cyberdeck_audio_output::write(&sample, 1));
    CHECK(probe.amp_off == 1);
    probe.headset = false;
    now += 500;
    CHECK(cyberdeck_audio_output::write(&sample, 1));
    CHECK(probe.amp_on == 2);

    const int closes_before_reopen = probe.closes;
    CHECK(cyberdeck_audio_output::start(44100, 2, 30));
    CHECK(probe.closes == closes_before_reopen + 1);
    CHECK(contains_after(probe.calls, 0, "open:44100:2"));

    probe.fail_write = true;
    CHECK(!cyberdeck_audio_output::write(&sample, 1));
    probe.fail_write = false;
    cyberdeck_audio_output::stop();
    CHECK(probe.amp_off >= 2);
    CHECK(probe.closes == closes_before_reopen + 2);
    const int calls_when_idle = static_cast<int>(probe.calls.size());
    cyberdeck_audio_output::stop();
    CHECK(static_cast<int>(probe.calls.size()) == calls_when_idle + 1); // mute only; already closed
    CHECK(!cyberdeck_audio_output::write(&sample, 1));

    CHECK(cyberdeck_audio_output::start(16000, 1, 20));
    probe.fail_amplifier = true;
    cyberdeck_audio_output::stop();
    const int opens_after_failed_amp_off = probe.opens;
    CHECK(!cyberdeck_audio_output::start(16000, 1, 20));
    CHECK(probe.opens == opens_after_failed_amp_off);
    CHECK(!cyberdeck_audio_output::write(&sample, 1));
    CHECK(!cyberdeck_audio_output::set_volume(20));
    probe.fail_amplifier = false;
    CHECK(cyberdeck_audio_output::start(16000, 1, 20));
    CHECK(probe.opens == opens_after_failed_amp_off + 1);
    cyberdeck_audio_output::stop();

    probe.fail_open = true;
    CHECK(!cyberdeck_audio_output::start(22050, 1, 10));
    probe.fail_open = false;
    CHECK(cyberdeck_audio_output::start(22050, 1, 10));
    probe.fail_volume = true;
    CHECK(!cyberdeck_audio_output::start(24000, 1, 10));
    probe.fail_volume = false;
    CHECK(cyberdeck_audio_output::start(24000, 1, 10));
    probe.fail_amplifier = true;
    now += 500;
    CHECK(!cyberdeck_audio_output::start(32000, 2, 10));
    probe.fail_amplifier = false;
    CHECK(cyberdeck_audio_output::start(32000, 2, 10));
    probe.fail_mute_off = true;
    CHECK(!cyberdeck_audio_output::start(48000, 2, 10));
    probe.fail_mute_off = false;
    cyberdeck_audio_output::stop();
    CHECK(probe.amp_off >= 5);

    /* The player calls start() once per PCM frame (about 42 times per second)
     * with identical arguments, so a reentrant start() must stay off the codec
     * I2C bus: volume only when it actually changes and the jack probe only
     * inside the existing 500 ms route throttle. */
    auto count_calls = [&probe](const std::string &prefix) {
        int total = 0;
        for (const auto &call : probe.calls)
            if (call.compare(0, prefix.size(), prefix) == 0)
                ++total;
        return total;
    };
    const int volumes_before_burst = count_calls("volume:");
    const int headset_before_burst = count_calls("headset");
    CHECK(cyberdeck_audio_output::start(48000, 2, 20));
    const int volumes_after_open = count_calls("volume:");
    const int headset_after_open = count_calls("headset");
    CHECK(volumes_after_open == volumes_before_burst + 1);
    CHECK(headset_after_open == headset_before_burst + 1);

    for (int i = 0; i < 100; ++i)
        CHECK(cyberdeck_audio_output::start(48000, 2, 20));
    const int burst_volumes = count_calls("volume:") - volumes_after_open;
    const int burst_headsets = count_calls("headset") - headset_after_open;
    std::cout << "identical start() burst: set_volume=" << burst_volumes << " headphone_connected=" << burst_headsets
              << "\n";
    CHECK(burst_volumes == 0);
    CHECK(burst_headsets == 0);

    now += 500;
    CHECK(cyberdeck_audio_output::start(48000, 2, 20));
    CHECK(count_calls("headset") - headset_after_open == 1);
    CHECK(count_calls("volume:") - volumes_after_open == 0);

    now += 1;
    CHECK(cyberdeck_audio_output::start(48000, 2, 21));
    CHECK(count_calls("volume:") - volumes_after_open == 1);
    CHECK(count_calls("headset") - headset_after_open == 1);
    CHECK(cyberdeck_audio_output::start(48000, 2, 21));
    CHECK(cyberdeck_audio_output::start(48000, 2, 21));
    CHECK(count_calls("volume:") - volumes_after_open == 1);

    cyberdeck_audio_output::stop();
    CHECK(cyberdeck_audio_output::start(48000, 2, 21));
    CHECK(count_calls("volume:") - volumes_after_open == 2);

    /* The throttle must not turn into a blind spot: a jack change during
     * playback is still honoured, just once per 500 ms window instead of once
     * per PCM frame. */
    const int volumes_before_jack = count_calls("volume:");
    const int headsets_before_jack = count_calls("headset");
    const int amp_off_before_jack = probe.amp_off;
    probe.headset = true;
    now += 1; // inside the current route window
    CHECK(cyberdeck_audio_output::start(48000, 2, 21));
    CHECK(probe.amp_off == amp_off_before_jack);
    CHECK(count_calls("headset") == headsets_before_jack);
    now += 500;
    CHECK(cyberdeck_audio_output::start(48000, 2, 21));
    CHECK(probe.amp_off == amp_off_before_jack + 1);
    CHECK(count_calls("headset") == headsets_before_jack + 1);
    CHECK(count_calls("volume:") == volumes_before_jack);
    probe.headset = false;
    const int amp_on_before_jack = probe.amp_on;
    now += 500;
    CHECK(cyberdeck_audio_output::start(48000, 2, 21));
    CHECK(probe.amp_on == amp_on_before_jack + 1);
    CHECK(count_calls("headset") == headsets_before_jack + 2);

    /* A speaker-amplifier failure inside the reentrant route is fail-closed:
     * start() fails without writing the volume, the codec is not reopened and
     * the fault blocks write()/set_volume() until the route recovers. */
    const int opens_before_fault = probe.opens;
    const int volumes_before_fault = count_calls("volume:");
    const int amp_attempts_before_fault = probe.amp_off;
    probe.headset = true;
    probe.fail_amplifier = true;
    now += 500;
    CHECK(!cyberdeck_audio_output::start(48000, 2, 22));
    CHECK(probe.amp_off == amp_attempts_before_fault + 1); // one attempt, rejected
    CHECK(count_calls("volume:") == volumes_before_fault);
    CHECK(probe.opens == opens_before_fault);
    CHECK(!cyberdeck_audio_output::write(&sample, 1));
    CHECK(probe.amp_off == amp_attempts_before_fault + 2); // write() retries it too
    CHECK(!cyberdeck_audio_output::set_volume(22));
    now += 1; // the route fault bypasses the throttle and keeps retrying
    CHECK(!cyberdeck_audio_output::start(48000, 2, 22));
    CHECK(probe.amp_off == amp_attempts_before_fault + 3);
    probe.fail_amplifier = false;
    now += 1;
    CHECK(cyberdeck_audio_output::start(48000, 2, 22));
    CHECK(probe.amp_off == amp_attempts_before_fault + 4); // route recovered once
    CHECK(probe.opens == opens_before_fault);
    CHECK(count_calls("volume:") == volumes_before_fault + 1); // 21 -> 22, once
    CHECK(cyberdeck_audio_output::start(48000, 2, 22));
    CHECK(count_calls("volume:") == volumes_before_fault + 1);

    /* set_volume() must keep active_volume in sync, otherwise the next
     * per-frame start() would rewrite the same value over I2C. */
    const int volumes_before_sync = count_calls("volume:");
    CHECK(cyberdeck_audio_output::set_volume(24));
    CHECK(count_calls("volume:") == volumes_before_sync + 1);
    CHECK(cyberdeck_audio_output::start(48000, 2, 24));
    CHECK(cyberdeck_audio_output::start(48000, 2, 24));
    CHECK(count_calls("volume:") == volumes_before_sync + 1);
    CHECK(cyberdeck_audio_output::write(&sample, 1));

    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
