#pragma once

#include "apps/image/cyberdeck_image_bmp.h"
#include "apps/image/cyberdeck_image_decode.h"
#include "apps/image/cyberdeck_image_list.h"
#include "apps/image/cyberdeck_image_types.h"
#include "apps/image/cyberdeck_image_worker_state.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

/* Foreground image viewer as an isolated compiled application.
 *
 * Binding: `image <path>` opens one BMP of the directory that contains it and
 * publishes it for the UI; left/right browse the bounded list of the same
 * directory; Esc returns to the shell.  The application owns no LVGL object and
 * never touches the filesystem from the LVGL task: a dedicated worker lists,
 * reads and decodes, and the UI timer only *moves* an already decoded frame
 * into the view, which becomes its sole owner. */
namespace cyberdeck_image {

class application final : public cyberdeck_apps::application {
  public:
    application();
    const cyberdeck_apps::manifest &get_manifest() const override;
    bool start() override;
    bool stop() override;
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view command, std::string_view args) override;

    /* ---- LVGL task only ---- */
    bool take_published(published_image &out);
    bool take_notice(std::string &out);
    void release(published_image &image) const;
    viewer_state state() const {
        return state_.load(std::memory_order_acquire);
    }
    /* Requests the neighbouring image of the published list.  Returns false
     * when there is no neighbour (clamped) or no job could be accepted. */
    bool navigate(int direction);

  private:
    struct job {
        std::array<char, k_max_path_bytes + 1> directory{};
        std::array<char, k_max_name_bytes + 1> name{};
        std::size_t index = 0;
        bool by_index = false;
        std::uint64_t generation = 0;
    };

    bool request(const job &value);
    void run_job(const job &value);
    void publish(std::uint64_t generation, published_image &image, const image_directory &listing,
                 const std::array<char, k_max_path_bytes + 1> &directory_path, std::size_t index);
    void fail(std::uint64_t generation, const char *reason);
    static void worker_entry(void *context);

    std::mutex request_mutex_;
    std::mutex publication_mutex_;
    job pending_{};
    /* Handoff protocol; every access happens with `request_mutex_` held. */
    worker_state worker_{};
    std::uint64_t request_generation_ = 0;
    image_directory directory_{};
    std::array<char, k_max_path_bytes + 1> directory_path_{};
    std::size_t current_index_ = 0;
    bool has_directory_ = false;
    /* Both are read by the worker outside the publication mutex, so they are
     * atomic; the value that matters is the newest *requested* generation. */
    std::atomic<std::uint64_t> published_generation_{0};
    std::atomic<std::uint64_t> pending_generation_{0};
    std::atomic<std::uint64_t> generation_counter_{0};
    std::atomic<std::uint64_t> publish_generation_{0};
    published_image published_{};
    bool has_published_ = false;
    std::string notice_{};
    std::atomic<viewer_state> state_{viewer_state::idle};
    std::atomic<bool> cancel_{false};
    /* The worker is created once by `start()` and only leaves on cancellation:
     * `wake_` hands it work and `done_` is given once when it exits. */
    SemaphoreHandle_t wake_ = nullptr;
    SemaphoreHandle_t done_ = nullptr;
    bool running_ = false;
};

application &global_application();

} // namespace cyberdeck_image
