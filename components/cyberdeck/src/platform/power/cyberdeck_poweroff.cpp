#include "platform/power/cyberdeck_poweroff.h"
#include <initializer_list>
#include <fcntl.h>
#include <unistd.h>

namespace cyberdeck_poweroff {

bool flush_metadata(const char *path) {
    if (path == nullptr) return false;
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    const char marker = '1';
    bool ok = write(fd, &marker, 1) == 1;
    if (fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    return ok;
}

bool emit_factory_signal(signal_ports &backend) {
    if (!backend.configure()) return false;
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        if (!backend.set_level(true)) return false;
        backend.delay_ms(100);
        if (!backend.set_level(false)) return false;
        backend.delay_ms(100);
    }
    return true;
}

bool controller::request() {
    auto expected = state::idle;
    return state_.compare_exchange_strong(expected, state::queued, std::memory_order_acq_rel);
}

bool controller::expire() {
    auto current = state_.load(std::memory_order_acquire);
    while (current == state::queued || current == state::running) {
        if (state_.compare_exchange_weak(current, state::failed, std::memory_order_acq_rel)) return true;
    }
    return false;
}

bool controller::execute(ports &backend, std::uint32_t budget_ms) {
    auto expected = state::queued;
    if (!state_.compare_exchange_strong(expected, state::running, std::memory_order_acq_rel)) return false;
    const auto started = backend.now_ms();
    for (auto step : {stage::ui, stage::fence, stage::serial, stage::screen_nvs, stage::cat, stage::music, stage::audit,
                      stage::time_sync, stage::ssh, stage::http, stage::ble, stage::wifi, stage::drain, stage::log,
                      stage::storage, stage::signal}) {
        stage_.store(step, std::memory_order_release);
        const auto elapsed = backend.now_ms() - started;
        bool admitted = elapsed < budget_ms;
        state wanted = state::running;
        if (step == stage::signal && admitted) {
            auto running = state::running;
            admitted = state_.compare_exchange_strong(running, state::signaling, std::memory_order_acq_rel);
            wanted = state::signaling;
        }
        if (!admitted || state_.load(std::memory_order_acquire) != wanted ||
            !backend.run(step, static_cast<std::uint32_t>(budget_ms - elapsed)) ||
            state_.load(std::memory_order_acquire) != wanted ||
            (step != stage::signal && backend.now_ms() - started >= budget_ms)) {
            state_.store(state::failed, std::memory_order_release);
            return false;
        }
    }
    state_.store(state::signal_sent, std::memory_order_release);
    return true;
}

const char *stage_name(stage value) {
    switch (value) {
    case stage::idle: return "idle";
    case stage::ui: return "UI";
    case stage::screen_nvs: return "screen NVS persistence";
    case stage::fence:
        return "SD admission fence";
    case stage::cat:
        return "cat worker";
    case stage::music:
        return "music player";
    case stage::audit:
        return "Wi-Fi audit";
    case stage::time_sync: return "time sync";
    case stage::ssh: return "SSH";
    case stage::http: return "HTTP readers";
    case stage::ble: return "BLE/NVS";
    case stage::wifi: return "Wi-Fi persistence";
    case stage::serial: return "Serial bridge";
    case stage::drain: return "SD leases";
    case stage::log: return "event log";
    case stage::storage: return "SD sync/unmount";
    case stage::signal: return "power rail signal";
    }
    return "invalid";
}

} // namespace cyberdeck_poweroff

#if defined(ESP_PLATFORM)
#include "apps/bluetooth/ble_mgr.h"
#include "apps/music/cyberdeck_music_player.h"
#include "apps/screenshot/screenshot_server.h"
#include "apps/serial/cyberdeck_serial_bridge.h"
#include "apps/shell/cyberdeck_cat_worker.h"
#include "apps/ssh/ssh_client.h"
#include "apps/system/cyberdeck_time_sync.h"
#include "apps/wifi/wifi_mgr.h"
#include "bsp/esp-bsp.h"
#include "diskio.h"
#include "esp_heap_caps.h"
#include "esp_io_expander_pi4ioe5v6408.h"
#include "esp_timer.h"
#include "ff.h"
#include "freertos/task.h"
#include "platform/display/cyberdeck_ui.h"
#include "platform/display/screen_off.h"
#include "platform/logging/event_log.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

extern "C" DRESULT __real_ff_disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count);
extern "C" DRESULT __wrap_ff_disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count) {
    const auto result = __real_ff_disk_write(drive, buffer, sector, count);
    // FatFS R0.15 ignores the FSInfo disk_write result in sync_fs. Observe the
    // public disk-I/O boundary as well, so a masked error cannot authorize cut.
    if (result != RES_OK) cyberdeck_sd_io::record_global_persistence_failure();
    return result;
}
extern "C" DRESULT __real_ff_disk_ioctl(BYTE drive, BYTE command, void *buffer);
extern "C" DRESULT __wrap_ff_disk_ioctl(BYTE drive, BYTE command, void *buffer) {
    const auto result = __real_ff_disk_ioctl(drive, command, buffer);
    if (command == CTRL_SYNC && result != RES_OK) cyberdeck_sd_io::record_global_persistence_failure();
    return result;
}

namespace {
static_assert(BSP_IO_EXPANDER_ADDRESS_1 == 0x44, "poweroff requires expander B at 0x44");
cyberdeck_poweroff::controller shutdown;
cyberdeck_sd_io::coordinator *sd_io = nullptr;
std::uint64_t fence_owner = 0;
std::atomic<std::uint64_t> shutdown_deadline_ms{0};

bool sync_and_unmount() {
    if (sd_io == nullptr || sd_io->state() != cyberdeck_sd_io::gate_state::closed ||
        sd_io->active_leases() != 0 || sd_io->persistence_failed()) return false;
    // A modified FIL's f_sync flushes FatFS metadata via sync_fs. This is a
    // final metadata barrier, not a replacement for each owner's fsync/close.
    return cyberdeck_poweroff::flush_metadata("/sdcard/.poweroff-sync") && !sd_io->persistence_failed() &&
           bsp_sdcard_unmount() == ESP_OK && !sd_io->persistence_failed();
}

class factory_signal_ports final : public cyberdeck_poweroff::signal_ports {
  public:
    explicit factory_signal_ports(std::uint64_t deadline) : deadline_(deadline) {}
    bool configure() override {
        expander_ = bsp_io_expander1_init();
        if (expander_ == nullptr) return false;
        // set_level rejects input pins. Keep HIGH-Z while enabling output,
        // establish the low idle latch, then enable the required push-pull drive.
        return esp_io_expander_set_output_mode(expander_, pin_, IO_EXPANDER_OUTPUT_MODE_OPEN_DRAIN) == ESP_OK &&
               esp_io_expander_set_dir(expander_, pin_, IO_EXPANDER_OUTPUT) == ESP_OK &&
               esp_io_expander_set_level(expander_, pin_, 0) == ESP_OK &&
               esp_io_expander_set_output_mode(expander_, pin_, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL) == ESP_OK &&
               static_cast<std::uint64_t>(esp_timer_get_time() / 1000) + 600 <= deadline_;
    }
    bool set_level(bool high) override {
        if (high && !started_) {
            if (static_cast<std::uint64_t>(esp_timer_get_time() / 1000) + 600 > deadline_) return false;
            started_ = true;
        }
        return esp_io_expander_set_level(expander_, pin_, high) == ESP_OK;
    }
    void delay_ms(std::uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }
  private:
    esp_io_expander_handle_t expander_ = nullptr;
    std::uint64_t deadline_;
    bool started_ = false;
    static constexpr auto pin_ = IO_EXPANDER_PIN_NUM_4; // B 0x44: PWROFF_PLUSE, never LCD_EN.
};

class firmware_ports final : public cyberdeck_poweroff::ports {
  public:
    std::uint64_t now_ms() const override { return esp_timer_get_time() / 1000; }
    bool run(cyberdeck_poweroff::stage step, std::uint32_t remaining_ms) override {
        using cyberdeck_poweroff::stage;
        const auto deadline = shutdown_deadline_ms.load(std::memory_order_acquire);
        const auto now = now_ms();
        if (now >= deadline) return false;
        if (deadline - now < remaining_ms) remaining_ms = static_cast<std::uint32_t>(deadline - now);
        const auto cap = [remaining_ms](std::uint32_t maximum) {
            return remaining_ms < maximum ? remaining_ms : maximum;
        };
        switch (step) {
        case stage::ui:
            return cyberdeck_ui_poweroff_freeze(cap(1000));
        case stage::screen_nvs:
            return screen_off_quiesce(cap(2000)) == ESP_OK;
        case stage::fence:
            return fence_owner != 0 && sd_io->state() == cyberdeck_sd_io::gate_state::closing;
        case stage::cat:
            return cyberdeck_cat_worker_quiesce(cap(2000));
        case stage::music:
            return cyberdeck_music_poweroff_quiesce(cap(10000));
        case stage::audit:
            return cyberdeck_ui_audit_quiesce(cap(5000));
        case stage::time_sync:
            return cyberdeck_time_sync_stop(cap(9000)) == ESP_OK;
        case stage::ssh:
            return ssh_client_disconnect_and_wait(cap(12000));
        case stage::http:
            return screenshot_server_stop(cap(2000)) == ESP_OK;
        case stage::ble:
            return ble_mgr_stop() == ESP_OK;
        case stage::wifi: return wifi_mgr_stop(cap(8000)) == ESP_OK;
        case stage::serial: return cyberdeck_serial::bridge_stop(cap(2000));
        case stage::drain:
            return sd_io->drain(fence_owner, std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(remaining_ms)) == cyberdeck_sd_io::drain_result::drained &&
                   !sd_io->persistence_failed();
        case stage::log:
            if (event_log_write_durable('I', "poweroff", "writers quiesced; preparing SD unmount") != ESP_OK) return false;
            return event_log_quiesce(remaining_ms) == ESP_OK;
        case stage::storage: return sync_and_unmount();
        case stage::signal: {
            if (remaining_ms < 1000) return false;
            factory_signal_ports signal(deadline);
            return cyberdeck_poweroff::emit_factory_signal(signal);
        }
        default: return false;
        }
    }
};

void shutdown_task(void *) {
    firmware_ports backend;
    if (!shutdown.execute(backend, 45000)) {
        char message[192];
        snprintf(message, sizeof(message), "poweroff: FAILED at %s; no retry. Reboot required.\n",
                 cyberdeck_poweroff::stage_name(shutdown.last_stage()));
        cyberdeck_ui_poweroff_message(message);
        printf("%s", message);
    } else {
        cyberdeck_ui_poweroff_message("poweroff: factory signal sent; physical cut must be verified.\n");
    }
    vTaskDelete(nullptr);
}

void shutdown_watchdog(void *) {
    const auto deadline = shutdown_deadline_ms.load(std::memory_order_acquire);
    const auto now = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    if (deadline > now) vTaskDelay(pdMS_TO_TICKS(deadline - now));
    // A timeout fences preparation without deleting a task inside driver I/O.
    // The factory signal is irreversible once started.
    if (shutdown.expire())
        cyberdeck_ui_poweroff_message("poweroff: global deadline expired; live resources retained, reboot required.\n");
    vTaskDelete(nullptr);
}
} // namespace

void cyberdeck_poweroff_init(cyberdeck_sd_io::coordinator &storage) { sd_io = &storage; }
bool cyberdeck_poweroff_pending() { return shutdown.status() != cyberdeck_poweroff::state::idle; }
cyberdeck_poweroff::admission_result cyberdeck_poweroff_request() {
    using cyberdeck_poweroff::admission_result;
    if (sd_io == nullptr) return admission_result::unavailable;
    if (!shutdown.request()) return admission_result::already_requested;
    shutdown_deadline_ms.store(static_cast<std::uint64_t>(esp_timer_get_time() / 1000) + 45000,
                               std::memory_order_release);
    fence_owner = sd_io->begin_close();
    if (fence_owner == 0) {
        (void)shutdown.expire();
        return admission_result::fence_failed;
    }
    if (xTaskCreate(shutdown_watchdog, "poweroff_watch", 4096, nullptr, 5, nullptr) != pdPASS) {
        (void)shutdown.expire();
        return admission_result::watchdog_unavailable;
    }
    if (xTaskCreateWithCaps(shutdown_task, "poweroff", 8192, nullptr, 4, nullptr, MALLOC_CAP_SPIRAM) != pdPASS) {
        (void)shutdown.expire();
        // Retain the one-shot fence; task allocation failure must not allow retry.
        cyberdeck_poweroff::observe_task_create_failure(
            [] {
                return cyberdeck_poweroff::task_create_heap_snapshot{
                    heap_caps_get_free_size(MALLOC_CAP_8BIT),
                    heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                };
            },
            [](const cyberdeck_poweroff::task_create_heap_snapshot &snapshot) {
                printf("poweroff: shutdown task unavailable; heap 8bit free=%u largest_block=%u; internal 8bit free=%u largest_block=%u bytes\n",
                       static_cast<unsigned>(snapshot.free_bytes), static_cast<unsigned>(snapshot.largest_block_bytes),
                       static_cast<unsigned>(snapshot.internal_free_bytes),
                       static_cast<unsigned>(snapshot.internal_largest_block_bytes));
            });
        return admission_result::task_unavailable;
    }
    return admission_result::admitted;
}
#endif
