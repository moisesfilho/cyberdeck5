#pragma once

#include "apps/music/cyberdeck_music_model.h"
#include "apps/music/cyberdeck_music_trace.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "platform/audio/cyberdeck_audio_presence.h"
#include "platform/storage/cyberdeck_sd_io_coordinator.h"

#include <cstdint>
#include <string_view>

namespace cyberdeck_music {
struct diagnostics_snapshot {
    std::uint32_t generation = 0;
    std::uint64_t generations_queued = 0;
    std::uint64_t generations_started = 0;
    std::uint64_t generations_retired = 0;
    std::uint64_t failures = 0;
    std::uint8_t failure_stage = 0;
    std::uint64_t failure_counts[8]{};
    std::uint64_t read_calls = 0;
    std::uint64_t read_time_us = 0;
    std::uint64_t read_max_us = 0;
    std::uint64_t decode_calls = 0;
    std::uint64_t decode_time_us = 0;
    std::uint64_t decode_max_us = 0;
    std::uint64_t codec_start_calls = 0;
    std::uint64_t codec_start_time_us = 0;
    std::uint64_t codec_start_max_us = 0;
    std::uint64_t write_calls = 0;
    std::uint64_t write_time_us = 0;
    std::uint64_t write_max_us = 0;
    std::uint64_t route_check_calls = 0;
    std::uint64_t route_check_time_us = 0;
    std::uint64_t route_check_max_us = 0;
    std::uint64_t backend_write_calls = 0;
    std::uint64_t backend_write_time_us = 0;
    std::uint64_t backend_write_max_us = 0;
    std::uint64_t pause_count = 0;
    std::uint64_t pause_time_us = 0;
};

class player_interface {
  public:
    virtual ~player_interface() = default;
    virtual bool start(cyberdeck_sd_io::coordinator *storage, cyberdeck_apps::grant access) = 0;
    virtual bool stop(std::uint32_t timeout_ms) = 0;
    virtual bool play(std::string_view path) = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual bool halt() = 0;
    virtual bool seek(std::uint32_t position_ms) = 0;
    virtual bool set_volume(std::uint8_t volume) = 0;
    virtual bool set_queue(const track *tracks, std::size_t count, std::size_t selected) = 0;
    virtual void select(std::size_t index) = 0;
    virtual void set_repeat(repeat_mode mode) = 0;
    virtual void set_shuffle(bool enabled) = 0;
    virtual bool status(snapshot &out) const = 0;
    virtual bool diagnostics_snapshot(diagnostics_snapshot &) const { return false; }
    virtual bool trace_snapshot(std::uint32_t, std::uint32_t, trace_page &) const { return false; }
    virtual bool running() const = 0;
};

class player final : public player_interface {
  public:
#if defined(CYBERDECK_MUSIC_TRACE_TESTING)
    enum class trace_lifecycle_test_stage { stop_teardown_started, snapshot_lock_acquired };
    using trace_lifecycle_test_hook = void (*)(trace_lifecycle_test_stage, void *);
    static void set_trace_lifecycle_test_hook(trace_lifecycle_test_hook hook, void *context);
#endif
    bool start(cyberdeck_sd_io::coordinator *storage, cyberdeck_apps::grant access) override;
    bool stop(std::uint32_t timeout_ms) override;
    bool play(std::string_view path) override;
    bool pause() override;
    bool resume() override;
    bool halt() override;
    bool seek(std::uint32_t position_ms) override;
    bool set_volume(std::uint8_t volume) override;
    bool set_queue(const track *tracks, std::size_t count, std::size_t selected) override;
    void select(std::size_t index) override;
    void set_repeat(repeat_mode mode) override;
    void set_shuffle(bool enabled) override;
    bool status(snapshot &out) const override;
    bool diagnostics_snapshot(cyberdeck_music::diagnostics_snapshot &out) const override;
    bool trace_snapshot(std::uint32_t cursor, std::uint32_t limit, trace_page &out) const override;
    bool running() const override;
  private:
    struct impl;
    impl *impl_ = nullptr;
};
player &global_player();

/* Headset presence probe used by the playback worker to pause when a headset is
 * inserted or removed on the USB-A UAC port or the 3.5 mm jack. Firmware
 * installs a device probe by default; host tests inject a deterministic one.
 * Install it before start(); nullptr disables the detection. */
using audio_presence_probe = cyberdeck_audio_presence::sample (*)(void *context);
void set_audio_presence_probe(audio_presence_probe probe, void *context);
}

extern "C" bool cyberdeck_music_poweroff_quiesce(std::uint32_t timeout_ms);
