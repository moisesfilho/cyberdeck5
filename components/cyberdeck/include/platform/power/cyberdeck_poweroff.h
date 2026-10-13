#pragma once

#include <atomic>
#include <cstdint>

namespace cyberdeck_poweroff {

enum class stage { idle, ui, screen_nvs, fence, cat, audit, time_sync, ssh, http, ble, wifi, serial, drain, log, storage, signal };
enum class state { idle, queued, running, signaling, failed, signal_sent };

struct ports {
    virtual ~ports() = default;
    virtual std::uint64_t now_ms() const = 0;
    virtual bool run(stage step, std::uint32_t remaining_ms) = 0;
};

class controller final {
  public:
    bool request();
    bool execute(ports &backend, std::uint32_t budget_ms);
    bool expire();
    state status() const { return state_.load(std::memory_order_acquire); }
    stage last_stage() const { return stage_.load(std::memory_order_acquire); }

  private:
    std::atomic<state> state_{state::idle};
    std::atomic<stage> stage_{stage::idle};
};

const char *stage_name(stage value);
bool flush_metadata(const char *path);

struct signal_ports {
    virtual ~signal_ports() = default;
    virtual bool configure() = 0;
    virtual bool set_level(bool high) = 0;
    virtual void delay_ms(std::uint32_t ms) = 0;
};
bool emit_factory_signal(signal_ports &backend);

} // namespace cyberdeck_poweroff

#if defined(ESP_PLATFORM)
#include "platform/storage/cyberdeck_sd_io_coordinator.h"
void cyberdeck_poweroff_init(cyberdeck_sd_io::coordinator &storage);
bool cyberdeck_poweroff_request();
bool cyberdeck_poweroff_pending();
#endif
