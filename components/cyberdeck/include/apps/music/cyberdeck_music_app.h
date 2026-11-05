#pragma once

#include "apps/music/cyberdeck_music_browser.h"
#include "apps/music/cyberdeck_music_model.h"
#include "apps/music/cyberdeck_music_player.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>
#include <mutex>

namespace cyberdeck_music {
enum class browser_scan_state : unsigned char { idle, scanning, ready, error };

class application final : public cyberdeck_apps::application {
  public:
    application();
    explicit application(player_interface &player);
    application(player_interface &player, const browser_scan_ops &browser_ops);
    const cyberdeck_apps::manifest &get_manifest() const override;
    bool start() override;
    bool stop() override;
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view command, std::string_view args) override;
    bool has_playback() const;
    bool player_snapshot(snapshot &out) const {
        return player_.status(out);
    }
    bool toggle_selected(std::size_t selected);
    bool play_selected(std::size_t selected);
    bool browser_scan(std::string_view directory);
    bool get_browser_snapshot(browser_snapshot &out) const;
    bool browser_scan_active() const {
        return browser_scan_state_.load(std::memory_order_acquire) == browser_scan_state::scanning;
    }
    bool queue_scan_active() const {
        return legacy_scan_running_.load(std::memory_order_acquire);
    }
    scan_measurements scan_metrics() const {
        std::lock_guard<std::mutex> lock(scan_metrics_mutex_);
        return scan_metrics_;
    }
    browser_scan_state get_browser_scan_state() const {
        return browser_scan_state_.load(std::memory_order_acquire);
    }
    bool browser_select(std::size_t index);
    bool browser_toggle_selected(std::size_t index);
    bool browser_play_selected(std::size_t index);
    cyberdeck_apps::result player_command(std::string_view args) {
        return execute("music", args);
    }
    bool consume_player_request() {
        return player_requested_.exchange(false, std::memory_order_acq_rel);
    }
    bool poweroff_quiesce(std::uint32_t timeout_ms);

  private:
    bool scan_directory(std::string_view path);
    bool scan_directory_sync(std::string_view path);
    bool scan_browser_sync(std::string_view path);
    bool browser_playback(std::size_t index, bool toggle);
    static void scan_entry(void *context);
    cyberdeck_apps::result status() const;
    model queue_{};
    browser_model browser_{};
    player_interface &player_;
    browser_scan_ops browser_ops_ = default_browser_scan_ops();
    mutable std::mutex queue_mutex_;
    mutable std::mutex scan_publication_mutex_;
    mutable std::mutex scan_metrics_mutex_;
    scan_measurements scan_metrics_{};
    TaskHandle_t scan_task_ = nullptr;
    SemaphoreHandle_t scan_done_ = nullptr;
    std::array<char, k_max_path_bytes + 1> scan_path_{};
    std::atomic<bool> scan_is_browser_{false};
    std::atomic<browser_scan_state> browser_scan_state_{browser_scan_state::idle};
    std::atomic<bool> scan_cancel_{false};
    // scan_running_ serializes the shared worker; legacy status uses only legacy fields.
    std::atomic<bool> scan_running_{false};
    std::atomic<bool> scan_succeeded_{false};
    std::atomic<bool> scan_attempted_{false};
    std::atomic<bool> scan_truncated_{false};
    std::atomic<bool> legacy_scan_running_{false};
    std::atomic<bool> player_requested_{false};
    std::atomic<bool> scan_join_pending_{false};
    std::atomic<bool> scan_quiescing_{false};
    bool running_ = false;
};
application &global_application();
} // namespace cyberdeck_music
