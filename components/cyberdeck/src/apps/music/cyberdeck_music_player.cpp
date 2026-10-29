#include "apps/music/cyberdeck_music_player.h"
#include "apps/music/cyberdeck_music_metadata.h"
#include "apps/runtime/cyberdeck_app_storage.h"
#include "apps/shell/cyberdeck_local_shell.h"
#include "platform/audio/cyberdeck_audio_output.h"
#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3/minimp3.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <sys/types.h>
#include <unistd.h>

namespace cyberdeck_music {
namespace {
constexpr char k_tag[] = "music";
constexpr std::size_t k_io_size = 4096;
/* Queue items contain a bounded path and are not passed to peripherals or
 * DMA. Keep their bulk storage in PSRAM when available; queue control remains
 * allocated by FreeRTOS with the required internal-memory capabilities. */
constexpr UBaseType_t k_queue_depth = 8;
constexpr std::uint32_t k_worker_stack = 8192;
enum class request_kind : std::uint8_t { play, stop };
struct request {
    request_kind kind;
    std::uint32_t generation;
    char path[k_max_path_bytes + 1];
};

std::uint32_t next_generation(std::uint32_t current) {
    ++current;
    return current == 0 ? 1 : current;
}

bool id3_offset(int fd, std::size_t file_size, std::size_t &offset) {
    offset = 0;
    unsigned char header[10]{};
    if (pread(fd, header, sizeof(header), 0) != static_cast<ssize_t>(sizeof(header)) ||
        std::memcmp(header, "ID3", 3) != 0)
        return true;
    if (header[3] < 2 || header[3] > 4 || (header[6] | header[7] | header[8] | header[9]) & 0x80)
        return false;
    const std::size_t bytes =
        (std::size_t(header[6]) << 21) | (std::size_t(header[7]) << 14) | (std::size_t(header[8]) << 7) | header[9];
    offset = 10 + bytes + ((header[5] & 0x10) ? 10 : 0);
    if (bytes > file_size || offset > file_size || offset < bytes)
        return false;
    return lseek(fd, static_cast<off_t>(offset), SEEK_SET) >= 0;
}

const char *failure_stage_name(error_stage stage) {
    switch (stage) {
    case error_stage::none:
        return "none";
    case error_stage::open:
        return "open";
    case error_stage::id3:
        return "id3";
    case error_stage::decode:
        return "decode";
    case error_stage::codec_start:
        return "codec_start";
    case error_stage::codec_write:
        return "codec_write";
    case error_stage::read:
        return "read";
    case error_stage::io:
        return "io";
    }
    return "unknown";
}
} // namespace

struct player::impl {
    cyberdeck_sd_io::coordinator *storage = nullptr;
    cyberdeck_apps::grant access{};
    QueueHandle_t queue = nullptr;
    SemaphoreHandle_t finished = nullptr;
    TaskHandle_t task = nullptr;
    mutable std::mutex state_mutex;
    mutable std::mutex lifecycle_mutex;
    snapshot state{};
    std::array<track, k_max_tracks> queue_tracks{};
    std::size_t queue_count = 0;
    std::size_t queue_selected = 0;
    std::uint32_t shuffle_counter = 1;
    std::atomic<std::uint32_t> playback_generation{0};
    std::atomic<bool> shutting_down{false};
    std::atomic<bool> paused{false};
    std::atomic<std::uint32_t> seek_to_ms{0};
    std::atomic<std::uint8_t> volume{20};
    bool initialized = false;
    std::atomic<bool> worker_exited{false};
    StaticTask_t *task_control = nullptr;
    StackType_t *task_stack = nullptr;

    static void task_entry(void *arg) {
        auto *self = static_cast<impl *>(arg);
        self->run();
    }
    bool publish_if_current(const request &req, playback_state value, std::uint32_t position = 0,
                            std::uint32_t duration = 0) {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (shutting_down.load(std::memory_order_acquire) ||
            req.generation != playback_generation.load(std::memory_order_acquire))
            return false;
        state.state = value;
        if (value != playback_state::error)
            state.failure_stage = error_stage::none;
        state.position_ms = position;
        if (duration != 0)
            state.current.duration_ms = duration;
        return true;
    }
    void publish_error(const request &req, error_stage stage, std::uint32_t position = 0, std::uint32_t duration = 0) {
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (shutting_down.load(std::memory_order_acquire) ||
                req.generation != playback_generation.load(std::memory_order_acquire))
                return;
            state.state = playback_state::error;
            state.failure_stage = stage;
            state.position_ms = position;
            if (duration != 0)
                state.current.duration_ms = duration;
        }
        ESP_LOGE(k_tag, "playback failed generation=%lu stage=%s position_ms=%lu",
                 static_cast<unsigned long>(req.generation), failure_stage_name(stage),
                 static_cast<unsigned long>(position));
    }
    bool is_current(const request &req) const {
        return !shutting_down.load(std::memory_order_acquire) &&
               req.generation == playback_generation.load(std::memory_order_acquire);
    }
    void run() {
        for (;;) {
            request req{};
            if (xQueueReceive(queue, &req, portMAX_DELAY) != pdTRUE)
                continue;
            if (shutting_down.load(std::memory_order_acquire))
                break;
            if (req.kind == request_kind::stop) {
                (void)publish_if_current(req, playback_state::stopped);
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                if (req.generation != playback_generation.load(std::memory_order_acquire))
                    continue;
            }
            ESP_LOGI(k_tag, "playback worker started generation=%lu", static_cast<unsigned long>(req.generation));
            auto lease =
                storage == nullptr ? cyberdeck_sd_io::lease{} : storage->acquire(cyberdeck_sd_io::access::read);
            if (!lease || !access.allows(cyberdeck_apps::resource::storage)) {
                publish_error(req, error_stage::open);
                continue;
            }
            cyberdeck_apps::storage_facade facade(access);
            std::size_t file_size = 0;
            const int fd = facade.open_readonly_stream(req.path, k_max_track_bytes, file_size);
            if (fd < 0) {
                publish_error(req, error_stage::open);
                continue;
            }
            ESP_LOGI(k_tag, "playback file opened generation=%lu size_bytes=%lu",
                     static_cast<unsigned long>(req.generation), static_cast<unsigned long>(file_size));
            if (!is_current(req)) {
                close(fd);
                continue;
            }
            std::size_t data_offset = 0;
            if (!id3_offset(fd, file_size, data_offset)) {
                close(fd);
                publish_error(req, error_stage::id3);
                continue;
            }
            track current{};
            std::memcpy(current.path.data(), req.path, std::strlen(req.path) + 1);
            const char *base = std::strrchr(req.path, '/');
            base = base == nullptr ? req.path : base + 1;
            std::strncpy(current.title.data(), base, k_max_title_bytes);
            current.size_bytes = static_cast<std::uint32_t>(file_size);
            read_metadata(fd, file_size, current);
            bool request_is_current = false;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                request_is_current = !shutting_down.load(std::memory_order_acquire) &&
                                     req.generation == playback_generation.load(std::memory_order_acquire);
                if (request_is_current) {
                    state.current = current;
                    state.position_ms = 0;
                    state.volume = volume.load(std::memory_order_acquire);
                }
            }
            if (!request_is_current) {
                close(fd);
                continue;
            }
            std::unique_ptr<mp3dec_t> decoder(new (std::nothrow) mp3dec_t{});
            std::unique_ptr<mp3dec_scratch_t> scratch(new (std::nothrow) mp3dec_scratch_t);
            std::unique_ptr<unsigned char[]> input(new (std::nothrow) unsigned char[k_io_size * 2]{});
            std::unique_ptr<mp3d_sample_t[]> pcm(new (std::nothrow) mp3d_sample_t[MINIMP3_MAX_SAMPLES_PER_FRAME]{});
            if (!decoder || !scratch || !input || !pcm) {
                close(fd);
                publish_error(req, error_stage::decode);
                continue;
            }
            mp3dec_init(decoder.get());
            std::size_t buffered = 0;
            std::size_t file_position = data_offset;
            std::uint32_t played_ms = 0;
            std::uint32_t duration_ms = 0;
            unsigned decoded_frames = 0;
            bool failed = false;
            error_stage failure = error_stage::none;
            bool eof = false;
            bool continue_with_next = false;
            bool first_pcm_write = true;
            if (!publish_if_current(req, playback_state::playing)) {
                close(fd);
                continue;
            }
            while (req.generation == playback_generation.load(std::memory_order_acquire) &&
                   !shutting_down.load(std::memory_order_acquire)) {
                if (paused.load(std::memory_order_acquire)) {
                    cyberdeck_audio_output::stop();
                    (void)publish_if_current(req, playback_state::paused, played_ms, duration_ms);
                    vTaskDelay(pdMS_TO_TICKS(20));
                    continue;
                }
                const std::uint32_t seek_value = seek_to_ms.exchange(0, std::memory_order_acq_rel);
                if (seek_value != 0 && duration_ms != 0) {
                    played_ms = std::min(seek_value - 1, duration_ms);
                    const std::uint64_t target =
                        data_offset + (std::uint64_t(file_size - data_offset) * played_ms / duration_ms);
                    if (lseek(fd, static_cast<off_t>(target), SEEK_SET) < 0) {
                        failed = true;
                        failure = error_stage::io;
                        break;
                    }
                    file_position = static_cast<std::size_t>(target);
                    buffered = 0;
                    mp3dec_init(decoder.get());
                }
                if (buffered < k_io_size && file_position < file_size) {
                    const ssize_t got = read(fd, input.get() + buffered, k_io_size);
                    if (got < 0) {
                        failed = true;
                        failure = error_stage::read;
                        break;
                    }
                    if (got == 0) {
                        failed = decoded_frames == 0;
                        if (failed)
                            failure = error_stage::decode;
                        eof = !failed;
                        break;
                    }
                    file_position += static_cast<std::size_t>(got);
                    buffered += static_cast<std::size_t>(got);
                }
                if (buffered == 0) {
                    eof = true;
                    break;
                }
                mp3dec_frame_info_t info{};
                const int samples =
                    mp3dec_decode_frame_with_scratch(decoder.get(), input.get(), static_cast<int>(buffered),
                                                     pcm.get(), &info, scratch.get());
                if (info.frame_bytes <= 0) {
                    if (buffered >= k_io_size) {
                        std::memmove(input.get(), input.get() + k_io_size, buffered - k_io_size);
                        buffered -= k_io_size;
                    } else if (file_position >= file_size) {
                        failed = decoded_frames == 0;
                        if (failed)
                            failure = error_stage::decode;
                        eof = !failed;
                        break;
                    }
                    continue;
                }
                if (static_cast<std::size_t>(info.frame_bytes) > buffered) {
                    failed = true;
                    failure = error_stage::decode;
                    break;
                }
                std::memmove(input.get(), input.get() + info.frame_bytes,
                             buffered - static_cast<std::size_t>(info.frame_bytes));
                buffered -= static_cast<std::size_t>(info.frame_bytes);
                if (samples <= 0 || (info.channels != 1 && info.channels != 2) || info.hz < 8000 || info.hz > 48000) {
                    failed = true;
                    failure = error_stage::decode;
                    break;
                }
                const auto current_volume = volume.load(std::memory_order_acquire);
                if (decoded_frames == 0)
                    ESP_LOGI(k_tag, "playback codec_start begin generation=%lu rate=%d channels=%d",
                             static_cast<unsigned long>(req.generation), info.hz, info.channels);
                if (!cyberdeck_audio_output::start(info.hz, static_cast<std::uint8_t>(info.channels), current_volume)) {
                    failed = true;
                    failure = error_stage::codec_start;
                    break;
                }
                if (decoded_frames == 0)
                    ESP_LOGI(k_tag, "playback codec_start complete generation=%lu",
                             static_cast<unsigned long>(req.generation));
                if (first_pcm_write)
                    ESP_LOGI(k_tag, "playback codec_write begin generation=%lu samples=%d",
                             static_cast<unsigned long>(req.generation), samples * info.channels);
                if (!cyberdeck_audio_output::write(pcm.get(), static_cast<std::size_t>(samples * info.channels))) {
                    failed = true;
                    failure = error_stage::codec_write;
                    break;
                }
                if (first_pcm_write) {
                    ESP_LOGI(k_tag, "playback first_pcm_write complete generation=%lu",
                             static_cast<unsigned long>(req.generation));
                    first_pcm_write = false;
                }
                ++decoded_frames;
                played_ms += static_cast<std::uint32_t>(std::uint64_t(samples) * 1000 / info.hz);
                if (info.bitrate_kbps > 0)
                    duration_ms =
                        static_cast<std::uint32_t>(std::uint64_t(file_size - data_offset) * 8 / info.bitrate_kbps);
                (void)publish_if_current(req, playback_state::playing, played_ms, duration_ms);
            }
            cyberdeck_audio_output::stop();
            if (close(fd) != 0)
                failed = true;
            if (failed && failure == error_stage::none)
                failure = error_stage::io;
            if (failed) {
                publish_error(req, failure, played_ms, duration_ms);
            } else {
                (void)publish_if_current(req, playback_state::stopped, played_ms, duration_ms);
                std::lock_guard<std::mutex> lock(state_mutex);
                if (req.generation == playback_generation.load(std::memory_order_acquire) && eof &&
                    state.repeat != repeat_mode::off && queue_count != 0) {
                    if (state.repeat == repeat_mode::one) {
                        // Repeat the current track without adding another queued request.
                    } else if (state.shuffle && queue_count > 1) {
                        shuffle_counter = shuffle_counter * 1664525U + 1013904223U;
                        queue_selected = (queue_selected + 1 + shuffle_counter % (queue_count - 1)) % queue_count;
                    } else if (queue_selected + 1 < queue_count)
                        ++queue_selected;
                    else
                        queue_selected = 0;
                    state.selected = queue_selected;
                    const track &item = queue_tracks[queue_selected];
                    std::memcpy(req.path, item.path.data(), item.path.size());
                    req.path[k_max_path_bytes] = '\0';
                    continue_with_next = true;
                }
            }
            if (continue_with_next)
                (void)xQueueSend(queue, &req, 0);
        }
        cyberdeck_audio_output::stop();
        worker_exited.store(true, std::memory_order_release);
        xSemaphoreGive(finished);
    }
};

bool player::start(cyberdeck_sd_io::coordinator *storage, cyberdeck_apps::grant access) {
    if (impl_ == nullptr)
        impl_ = new (std::nothrow) impl{};
    if (impl_ == nullptr)
        return false;
    std::lock_guard<std::mutex> lifecycle_lock(impl_->lifecycle_mutex);
    if (impl_->initialized)
        return true;
    impl_->storage = storage;
    impl_->access = access;
    impl_->queue = xQueueCreateWithCaps(k_queue_depth, sizeof(request), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (impl_->queue == nullptr) {
        ESP_LOGE(k_tag, "player start failed stage=queue depth=%u item_bytes=%u",
                 static_cast<unsigned>(k_queue_depth), static_cast<unsigned>(sizeof(request)));
        return false;
    }
    impl_->finished = xSemaphoreCreateBinary();
    if (impl_->finished == nullptr) {
        ESP_LOGE(k_tag, "player start failed stage=completion_semaphore");
        if (impl_->queue != nullptr)
            vQueueDeleteWithCaps(impl_->queue);
        impl_->queue = nullptr;
        return false;
    }
    impl_->shutting_down = false;
    impl_->worker_exited.store(false, std::memory_order_release);
    impl_->task_control = static_cast<StaticTask_t *>(heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (impl_->task_control == nullptr) {
        ESP_LOGE(k_tag, "player start failed stage=worker_tcb");
        vQueueDeleteWithCaps(impl_->queue);
        vSemaphoreDelete(impl_->finished);
        impl_->queue = nullptr;
        impl_->finished = nullptr;
        return false;
    }
    impl_->task_stack = static_cast<StackType_t *>(heap_caps_malloc(k_worker_stack * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (impl_->task_stack != nullptr) {
        impl_->task = xTaskCreateStatic(impl::task_entry, "music_player", k_worker_stack, impl_, 5,
                                        impl_->task_stack, impl_->task_control);
    }
    if (impl_->task == nullptr) {
        ESP_LOGE(k_tag, "player start failed stage=worker_task stack_bytes=%lu stack=%u",
                 static_cast<unsigned long>(k_worker_stack), impl_->task_stack != nullptr);
        heap_caps_free(impl_->task_stack);
        impl_->task_stack = nullptr;
        heap_caps_free(impl_->task_control);
        impl_->task_control = nullptr;
        vQueueDeleteWithCaps(impl_->queue);
        vSemaphoreDelete(impl_->finished);
        impl_->queue = nullptr;
        impl_->finished = nullptr;
        return false;
    }
    impl_->initialized = true;
    return true;
}

bool player::stop(std::uint32_t timeout_ms) {
    if (impl_ == nullptr)
        return true;
    std::unique_lock<std::mutex> lifecycle_lock(impl_->lifecycle_mutex);
    if (!impl_->initialized)
        return true;
    impl_->shutting_down.store(true, std::memory_order_release);
    std::uint32_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        generation = next_generation(impl_->playback_generation.load(std::memory_order_relaxed));
        impl_->playback_generation.store(generation, std::memory_order_release);
    }
    xQueueReset(impl_->queue);
    request req{};
    req.kind = request_kind::stop;
    req.generation = generation;
    if (xQueueSend(impl_->queue, &req, 0) != pdTRUE)
        return false;
    if (xSemaphoreTake(impl_->finished, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        return false;
    if (!impl_->worker_exited.load(std::memory_order_acquire) || impl_->task == nullptr)
        return false;
    vTaskDelete(impl_->task);
    impl_->task = nullptr;
    heap_caps_free(impl_->task_stack);
    impl_->task_stack = nullptr;
    heap_caps_free(impl_->task_control);
    impl_->task_control = nullptr;
    vQueueDeleteWithCaps(impl_->queue);
    vSemaphoreDelete(impl_->finished);
    impl_->queue = nullptr;
    impl_->finished = nullptr;
    impl_->initialized = false;
    return true;
}

bool player::play(std::string_view path) {
    if (impl_ == nullptr || !is_mp3_path(path))
        return false;
    std::lock_guard<std::mutex> lifecycle_lock(impl_->lifecycle_mutex);
    if (!impl_->initialized || impl_->shutting_down.load(std::memory_order_acquire))
        return false;
    request req{};
    req.kind = request_kind::play;
    std::memcpy(req.path, path.data(), path.size());
    req.path[path.size()] = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        req.generation = next_generation(impl_->playback_generation.load(std::memory_order_relaxed));
        if (xQueueSend(impl_->queue, &req, 0) != pdTRUE)
            return false;
        impl_->playback_generation.store(req.generation, std::memory_order_release);
    }
    impl_->paused.store(false, std::memory_order_release);
    return true;
}
bool player::pause() {
    if (!running())
        return false;
    impl_->paused.store(true, std::memory_order_release);
    return true;
}
bool player::resume() {
    if (!running())
        return false;
    impl_->paused.store(false, std::memory_order_release);
    return true;
}
bool player::halt() {
    if (impl_ == nullptr)
        return false;
    std::lock_guard<std::mutex> lifecycle_lock(impl_->lifecycle_mutex);
    if (!impl_->initialized || impl_->shutting_down.load(std::memory_order_acquire))
        return false;
    std::uint32_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        generation = next_generation(impl_->playback_generation.load(std::memory_order_relaxed));
        impl_->playback_generation.store(generation, std::memory_order_release);
    }
    xQueueReset(impl_->queue);
    request req{};
    req.kind = request_kind::stop;
    req.generation = generation;
    return xQueueSend(impl_->queue, &req, 0) == pdTRUE;
}
bool player::seek(std::uint32_t position_ms) {
    if (!running() || position_ms == UINT32_MAX)
        return false;
    impl_->seek_to_ms.store(position_ms + 1, std::memory_order_release);
    return true;
}
bool player::set_volume(std::uint8_t volume) {
    if (!running() || volume > 50)
        return false;
    impl_->volume.store(volume, std::memory_order_release);
    return true;
}
bool player::set_queue(const track *tracks, std::size_t count, std::size_t selected) {
    if (!running() || count > k_max_tracks || (tracks == nullptr && count != 0) || (count != 0 && selected >= count))
        return false;
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    impl_->queue_tracks = {};
    if (count != 0)
        std::copy(tracks, tracks + count, impl_->queue_tracks.begin());
    impl_->queue_count = count;
    impl_->queue_selected = selected;
    impl_->state.track_count = count;
    impl_->state.selected = selected;
    return true;
}
void player::select(std::size_t index) {
    if (impl_ == nullptr)
        return;
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    if (index < impl_->queue_count) {
        impl_->queue_selected = index;
        impl_->state.selected = index;
    }
}
void player::set_repeat(repeat_mode mode) {
    if (impl_ != nullptr) {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        impl_->state.repeat = mode;
    }
}
void player::set_shuffle(bool enabled) {
    if (impl_ != nullptr) {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        impl_->state.shuffle = enabled;
    }
}
bool player::status(snapshot &out) const {
    if (impl_ == nullptr)
        return false;
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    out = impl_->state;
    out.volume = impl_->volume.load(std::memory_order_acquire);
    return true;
}
bool player::running() const {
    if (impl_ == nullptr)
        return false;
    std::lock_guard<std::mutex> lifecycle_lock(impl_->lifecycle_mutex);
    return impl_->initialized;
}
player &global_player() {
    static player instance;
    return instance;
}
} // namespace cyberdeck_music
