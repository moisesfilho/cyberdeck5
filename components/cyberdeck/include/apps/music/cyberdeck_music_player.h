#pragma once

#include "apps/music/cyberdeck_music_model.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "platform/storage/cyberdeck_sd_io_coordinator.h"

#include <cstdint>
#include <string_view>

namespace cyberdeck_music {
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
    virtual bool running() const = 0;
};

class player final : public player_interface {
  public:
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
    bool running() const override;
  private:
    struct impl;
    impl *impl_ = nullptr;
};
player &global_player();
}

extern "C" bool cyberdeck_music_poweroff_quiesce(std::uint32_t timeout_ms);
