#include "apps/image/cyberdeck_image_app.h"

#include "apps/image/cyberdeck_image_list.h"
#include "apps/runtime/cyberdeck_app_storage.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <utility>

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

namespace cyberdeck_image {
namespace {

constexpr char k_tag[] = "image";
/* `stop()` runs on the LVGL task (Esc / foreground switch), so the join must
 * be short.  Cancellation is cooperative — polled per directory entry and per
 * decoded row — therefore the worker leaves within milliseconds; this budget
 * only covers a device-level filesystem stall that cannot be cancelled, and on
 * timeout nothing is freed while the worker may still use it. */
constexpr std::uint32_t k_worker_join_budget_ms = 200;

/* Command-bearing foreground app: `image <path>` opens the file, the arrows
 * browse the directory list and Esc returns to the shell.  The command carries
 * an argument and opens the foreground by itself, so
 * `commands_require_active_foreground` must stay false: the supervisor decides
 * "open" from a missing argument only, which would reject `image /fotos/a.bmp`
 * before the application ever runs. */
constexpr cyberdeck_apps::manifest k_manifest{
    "cyberdeck.image_viewer",
    "Image viewer",
    "1.0.1",
    "Foreground BMP image viewer",
    "",
    {},
    0,
    {"display", "input", "storage"},
    3,
    5000,
    "1",
    cyberdeck_apps::app_type::foreground,
    {},
    0,
    0,
    0,
    {"image"},
    1,
    false,
    {{{"image <path>", "path: absolute BMP file inside the SD namespace; left/right browses "
                       "the same directory; Esc closes\n"}}},
    false};

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    return value;
}

/* Bounded copy into a fixed-size destination: at most `N - 1` bytes plus the
 * terminator, so a 257-byte path array and a 129-byte name array share one
 * helper without either one being able to overflow or to lose its NUL. */
template <std::size_t N> void copy_bounded(std::array<char, N> &out, std::string_view value) {
    static_assert(N > 0, "the destination must hold at least the terminator");
    constexpr std::size_t k_limit = N - 1;
    const std::size_t length = value.size() > k_limit ? k_limit : value.size();
    std::memcpy(out.data(), value.data(), length);
    out[length] = '\0';
}

} // namespace

application::application() = default;

const cyberdeck_apps::manifest &application::get_manifest() const {
    return k_manifest;
}

bool application::start() {
    if (running_)
        return true;
    const cyberdeck_apps::grant access = app_grant();
    if (!access.allows(cyberdeck_apps::resource::storage) || !access.allows(cyberdeck_apps::resource::display) ||
        !access.allows(cyberdeck_apps::resource::input)) {
        ESP_LOGE(k_tag, "start failed stage=grant");
        return false;
    }
    if (done_ != nullptr) {
        /* A previous stop() timed out: only one worker may exist.  Reap it if
         * it has already exited, otherwise refuse to start a second one. */
        if (xSemaphoreTake(done_, 0) != pdTRUE) {
            ESP_LOGE(k_tag, "start failed stage=worker_not_reaped");
            return false;
        }
        vSemaphoreDelete(done_);
        done_ = nullptr;
        if (wake_ != nullptr) {
            vSemaphoreDelete(wake_);
            wake_ = nullptr;
        }
    }
    wake_ = xSemaphoreCreateBinary();
    done_ = xSemaphoreCreateBinary();
    if (wake_ == nullptr || done_ == nullptr) {
        ESP_LOGE(k_tag, "start failed stage=completion_semaphore");
        if (wake_ != nullptr) {
            vSemaphoreDelete(wake_);
            wake_ = nullptr;
        }
        if (done_ != nullptr) {
            vSemaphoreDelete(done_);
            done_ = nullptr;
        }
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        cancel_.store(false);
        worker_.reset();
        if (xTaskCreate(worker_entry, "image_load", static_cast<std::uint32_t>(k_worker_stack_bytes), this, 4,
                        nullptr) != pdPASS) {
            ESP_LOGE(k_tag, "start failed stage=worker_task");
            vSemaphoreDelete(wake_);
            vSemaphoreDelete(done_);
            wake_ = nullptr;
            done_ = nullptr;
            return false;
        }
        worker_.mark_alive();
    }
    {
        std::lock_guard<std::mutex> lock(publication_mutex_);
        published_ = published_image{};
        has_published_ = false;
        has_directory_ = false;
        directory_.clear();
        current_index_ = 0;
        notice_.clear();
    }
    state_.store(viewer_state::idle);
    running_ = true;
    return true;
}

bool application::stop() {
    if (!running_)
        return true;
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        cancel_.store(true);
    }
    /* Wake an idle worker so it observes the cancellation; if it is already
     * decoding, the per-row/per-entry polls stop it. */
    if (wake_ != nullptr)
        xSemaphoreGive(wake_);
    if (done_ == nullptr || xSemaphoreTake(done_, pdMS_TO_TICKS(k_worker_join_budget_ms)) != pdTRUE) {
        /* Keep every resource: the worker still owns `this` and the queues, so
         * freeing them here would be a use-after-free.  A later stop() retries
         * the join. */
        ESP_LOGE(k_tag, "stop failed stage=worker_join");
        return false;
    }
    published_image discarded{};
    {
        std::lock_guard<std::mutex> lock(publication_mutex_);
        discarded = published_;
        published_ = published_image{};
        has_published_ = false;
        has_directory_ = false;
        directory_.clear();
        current_index_ = 0;
        directory_path_.fill('\0');
        notice_.clear();
    }
    release(discarded);
    if (wake_ != nullptr) {
        vSemaphoreDelete(wake_);
        wake_ = nullptr;
    }
    if (done_ != nullptr) {
        vSemaphoreDelete(done_);
        done_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        cancel_.store(false);
        worker_.reset();
    }
    state_.store(viewer_state::idle);
    running_ = false;
    return true;
}

cyberdeck_apps::result application::execute(std::string_view command, std::string_view args) {
    if (command != "image")
        return {cyberdeck_apps::result_status::not_handled, std::string{}};
    const std::string_view path = trim(args);
    if (path.empty()) {
        state_.store(viewer_state::error);
        return {cyberdeck_apps::result_status::rejected, std::string("usage: image <path>\n")};
    }
    std::string_view directory{};
    std::string_view name{};
    if (!split_image_path(path, directory, name) || !is_bmp_name(name)) {
        /* Fail closed before touching the card: the supervisor already made
         * this app the foreground, and the UI returns to the shell because the
         * state is `error` and no image was published. */
        state_.store(viewer_state::error);
        return {cyberdeck_apps::result_status::rejected, std::string("image: invalid path (absolute .bmp only)\n")};
    }
    job value{};
    copy_bounded(value.directory, directory);
    copy_bounded(value.name, name);
    if (!request(value)) {
        state_.store(viewer_state::error);
        return {cyberdeck_apps::result_status::rejected, std::string("image: load unavailable\n")};
    }
    return {cyberdeck_apps::result_status::handled, std::string{}};
}

bool application::request(const job &value) {
    if (!running_ || wake_ == nullptr)
        return false;
    std::lock_guard<std::mutex> lock(request_mutex_);
    if (cancel_.load(std::memory_order_acquire))
        return false;
    job queued = value;
    queued.generation = generation_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    pending_ = queued;
    pending_generation_.store(queued.generation, std::memory_order_release);
    state_.store(viewer_state::loading, std::memory_order_release);
    /* The task is created by start(); enqueue() only reports whether a worker
     * exists, and the wake-up is handed over inside the same critical section,
     * so a request can never be queued without waking its consumer. */
    if (worker_.enqueue()) {
        state_.store(viewer_state::error, std::memory_order_release);
        return false;
    }
    xSemaphoreGive(wake_);
    return true;
}

void application::worker_entry(void *context) {
    auto *self = static_cast<application *>(context);
    for (;;) {
        if (xSemaphoreTake(self->wake_, portMAX_DELAY) != pdTRUE)
            continue;
        job current{};
        bool run = false;
        bool exit = false;
        {
            std::lock_guard<std::mutex> lock(self->request_mutex_);
            switch (self->worker_.next(self->cancel_.load(std::memory_order_acquire))) {
            case worker_state::step::run:
                current = self->pending_;
                run = true;
                break;
            case worker_state::step::exit:
                exit = true;
                break;
            case worker_state::step::wait:
                break;
            }
        }
        if (exit)
            break;
        if (run)
            self->run_job(current);
    }
    const SemaphoreHandle_t completed = self->done_;
    if (completed != nullptr)
        xSemaphoreGive(completed);
    vTaskDelete(nullptr);
}

void application::run_job(const job &value) {
    decode_ops ops = default_decode_ops();
    ops.context = this;
    ops.cancelled = [](void *context) {
        return static_cast<application *>(context)->cancel_.load(std::memory_order_acquire);
    };
    if (cancel_.load(std::memory_order_acquire) ||
        value.generation != pending_generation_.load(std::memory_order_acquire))
        return;

    /* BUG-IMG-01: the directory page is ~8 KiB and this runs on the 6144-byte
     * `image_load` stack.  It therefore lives on the heap (PSRAM on the target)
     * through `directory_workspace`; the frame only keeps the small holder. */
    directory_workspace workspace;
    const decode_ops allocation = default_decode_ops();
    if (!workspace.acquire({allocation.context, allocation.allocate, allocation.deallocate}) ||
        workspace.get() == nullptr) {
        fail(value.generation, "image: out of memory\n");
        return;
    }
    image_directory &listing = *workspace.get();
    std::array<char, k_max_path_bytes + 1> directory_path{};
    std::array<char, k_max_name_bytes + 1> name{};
    std::size_t index = 0;
    if (value.by_index) {
        {
            std::lock_guard<std::mutex> lock(publication_mutex_);
            if (!has_directory_ || value.index >= directory_.count) {
                listing.clear();
                index = 0;
            } else {
                listing = directory_;
                directory_path = directory_path_;
                index = value.index;
            }
        }
        if (listing.count == 0 || index >= listing.count) {
            fail(value.generation, "image: no image selected\n");
            return;
        }
        std::memcpy(name.data(), listing.entries[index].name.data(), k_max_name_bytes + 1);
    } else {
        listing_ops scan = default_listing_ops();
        scan.cancel_context = this;
        scan.cancelled = [](void *context) {
            return static_cast<application *>(context)->cancel_.load(std::memory_order_acquire);
        };
        /* The requested file is offered even when it sorts beyond the ordinary
         * window; the scan itself proves it is an eligible BMP of this
         * directory, so the pin cannot bypass path/BMP/regular-size filters. */
        if (!list_images(value.directory.data(), listing, scan, value.name.data())) {
            /* Cancellation is not a diagnostic: stop() already owns the state. */
            if (cancel_.load(std::memory_order_acquire))
                return;
            fail(value.generation, "image: directory unavailable\n");
            return;
        }
        if (listing.count == 0) {
            fail(value.generation, "image: no BMP in directory\n");
            return;
        }
        if (!find_image(listing, value.name.data(), index)) {
            fail(value.generation, "image: not found in directory\n");
            return;
        }
        directory_path = value.directory;
        std::memcpy(name.data(), value.name.data(), k_max_name_bytes + 1);
    }
    if (cancel_.load(std::memory_order_acquire) ||
        value.generation != pending_generation_.load(std::memory_order_acquire))
        return;

    const std::string path = join_image_path(directory_path.data(), name.data());
    cyberdeck_apps::storage_facade storage(app_grant());
    std::size_t file_bytes = 0;
    const int descriptor = storage.open_readonly_stream(path, k_max_file_bytes, file_bytes);
    if (descriptor < 0) {
        const int error = errno;
        fail(value.generation, error == EFBIG ? "image: file larger than 5 MiB\n" : "image: cannot open file\n");
        return;
    }
    decoded_image decoded{};
    const decode_result result = decode_descriptor(ops, descriptor, file_bytes, decoded);
    (void)close(descriptor);
    if (result.state == decode_status::cancelled)
        return;
    if (result.state != decode_status::ok) {
        switch (result.state) {
        case decode_status::too_large:
            fail(value.generation, "image: file larger than 5 MiB\n");
            return;
        case decode_status::read_failed:
            fail(value.generation, "image: read failed\n");
            return;
        case decode_status::allocation_failed:
            fail(value.generation, "image: out of memory\n");
            return;
        case decode_status::invalid_argument:
            fail(value.generation, "image: invalid request\n");
            return;
        case decode_status::format: {
            std::string message("image: ");
            message += bmp_status_reason(result.format);
            message.push_back('\n');
            fail(value.generation, message.c_str());
            return;
        }
        case decode_status::cancelled:
        case decode_status::ok:
            break;
        }
        return;
    }

    published_image image{};
    image.pixels = decoded.pixels;
    image.pixel_bytes = decoded.pixel_bytes;
    image.width = decoded.width;
    image.height = decoded.height;
    image.index = index;
    image.count = listing.count;
    image.truncated = listing.truncated;
    std::memcpy(image.name.data(), name.data(), k_max_name_bytes + 1);
    publish(value.generation, image, listing, directory_path, index);
}

void application::publish(std::uint64_t generation, published_image &image, const image_directory &listing,
                          const std::array<char, k_max_path_bytes + 1> &directory_path, std::size_t index) {
    published_image replaced{};
    {
        std::lock_guard<std::mutex> lock(publication_mutex_);
        if (cancel_.load(std::memory_order_acquire) ||
            generation != pending_generation_.load(std::memory_order_acquire)) {
            replaced = image;
        } else {
            replaced = published_;
            published_ = image;
            has_published_ = true;
            directory_ = listing;
            directory_path_ = directory_path;
            current_index_ = index;
            has_directory_ = true;
            published_generation_.store(generation, std::memory_order_release);
            notice_.clear();
            state_.store(viewer_state::ready, std::memory_order_release);
        }
    }
    release(replaced);
}

void application::fail(std::uint64_t generation, const char *reason) {
    published_image replaced{};
    {
        std::lock_guard<std::mutex> lock(publication_mutex_);
        if (cancel_.load(std::memory_order_acquire) ||
            generation != pending_generation_.load(std::memory_order_acquire))
            return;
        /* A failure never leaves a partial frame or an obsolete position: the
         * pending slot is dropped and the UI returns to the shell. */
        replaced = published_;
        published_ = published_image{};
        has_published_ = false;
        has_directory_ = false;
        directory_.clear();
        notice_ = reason != nullptr ? reason : "image: load failed\n";
        state_.store(viewer_state::error, std::memory_order_release);
    }
    release(replaced);
}

bool application::take_published(published_image &out) {
    std::lock_guard<std::mutex> lock(publication_mutex_);
    if (!has_published_)
        return false;
    out = published_;
    published_ = published_image{};
    has_published_ = false;
    return true;
}

bool application::take_notice(std::string &out) {
    std::lock_guard<std::mutex> lock(publication_mutex_);
    if (notice_.empty())
        return false;
    out = std::move(notice_);
    notice_.clear();
    return true;
}

void application::release(published_image &image) const {
    if (image.pixels == nullptr) {
        image = published_image{};
        return;
    }
    decoded_image owned{};
    owned.pixels = image.pixels;
    owned.pixel_bytes = image.pixel_bytes;
    owned.width = image.width;
    owned.height = image.height;
    image = published_image{};
    release_decoded(owned);
}

bool application::navigate(int direction) {
    if (direction == 0)
        return false;
    job value{};
    {
        std::lock_guard<std::mutex> lock(publication_mutex_);
        if (!has_directory_ || directory_.count == 0)
            return false;
        const std::size_t next = step_image_index(current_index_, directory_.count, direction);
        if (next == current_index_)
            return false;
        value.by_index = true;
        value.index = next;
        copy_bounded(value.directory, directory_path_.data());
    }
    return request(value);
}

application &global_application() {
    static application instance;
    return instance;
}

} // namespace cyberdeck_image
