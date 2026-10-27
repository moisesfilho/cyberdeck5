#pragma once

#include "apps/music/cyberdeck_music_model.h"
#include "apps/music/cyberdeck_music_player.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>
#include <mutex>

namespace cyberdeck_music {
class application final : public cyberdeck_apps::application {
  public:
    application();
    explicit application(player_interface &player);
    const cyberdeck_apps::manifest &get_manifest() const override;
    bool start() override;
    bool stop() override;
    bool running() const override { return running_; }
    cyberdeck_apps::result execute(std::string_view command, std::string_view args) override;
    bool has_playback() const;
    bool poweroff_quiesce(std::uint32_t timeout_ms);
  private:
    bool scan_directory(std::string_view path);
    bool scan_directory_sync(std::string_view path);
    static void scan_entry(void *context);
    cyberdeck_apps::result status() const;
    model queue_{};
    player_interface &player_;
    mutable std::mutex queue_mutex_;
    std::mutex scan_publication_mutex_;
    TaskHandle_t scan_task_ = nullptr;
    SemaphoreHandle_t scan_done_ = nullptr;
    std::array<char, k_max_path_bytes + 1> scan_path_{};
    std::atomic<bool> scan_cancel_{false};
    std::atomic<bool> scan_running_{false};
    std::atomic<bool> scan_succeeded_{false};
    std::atomic<bool> scan_attempted_{false};
    std::atomic<bool> scan_join_pending_{false};
    std::atomic<bool> scan_quiescing_{false};
    bool running_ = false;
};
application &global_application();
}
