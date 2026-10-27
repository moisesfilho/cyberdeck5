#include "apps/music/cyberdeck_music_app.h"
#include "apps/music/cyberdeck_music_player.h"
#include "apps/shell/cyberdeck_local_shell.h"
#include "esp_log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace cyberdeck_music {
namespace {
constexpr char k_tag[] = "music";
constexpr cyberdeck_apps::manifest k_manifest{
    "cyberdeck.music",
    "Music",
    "1.0.0",
    "Background local MP3 player",
    "",
    {"cyberdeck.event_log"},
    1,
    {"storage"},
    1,
    5000,
    "1",
    cyberdeck_apps::app_type::background,
    {"storage"},
    1,
    8192,
    8,
    {"music"},
    1,
    false,
    {{{"music queue <directory> | list | play [index] | pause | resume | stop | next | prev | seek <seconds> | volume "
       "<0-50> | shuffle | repeat | status",
       "MP3 playback from SD; volume is capped at 50\n"}}}};

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

bool validate_directory_path(std::string_view input) {
    if (input.empty() || input.size() > k_max_path_bytes || input.front() != '/' || input.find("//") != input.npos)
        return false;
    if (input == "/sdcard" || input.rfind("/sdcard/", 0) == 0)
        return false;
    std::size_t begin = 1;
    while (begin <= input.size()) {
        const auto end = input.find('/', begin);
        const auto part = input.substr(begin, end == input.npos ? input.size() - begin : end - begin);
        if (part == "." || part == "..")
            return false;
        if (end == input.npos)
            break;
        begin = end + 1;
    }
    return true;
}

bool parse_unsigned(std::string_view text, std::uint32_t maximum, std::uint32_t &out) {
    if (text.empty() || text.size() > 10)
        return false;
    const std::string copy(text);
    char *end = nullptr;
    const unsigned long value = std::strtoul(copy.c_str(), &end, 10);
    if (end == copy.c_str() || *end != '\0' || value > maximum)
        return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

const char *state_name(playback_state state) {
    switch (state) {
    case playback_state::stopped:
        return "stopped";
    case playback_state::playing:
        return "playing";
    case playback_state::paused:
        return "paused";
    case playback_state::error:
        return "error";
    }
    return "error";
}
const char *error_stage_name(error_stage stage) {
    switch (stage) {
    case error_stage::none: return "none";
    case error_stage::open: return "open";
    case error_stage::id3: return "id3";
    case error_stage::decode: return "decode";
    case error_stage::codec_start: return "codec_start";
    case error_stage::codec_write: return "codec_write";
    case error_stage::read: return "read";
    case error_stage::io: return "io";
    }
    return "unknown";
}
} // namespace

const cyberdeck_apps::manifest &application::get_manifest() const {
    return k_manifest;
}
application::application() : player_(global_player()) {}
application::application(player_interface &player) : player_(player) {}
bool application::start() {
    if (running_)
        return true;
    const cyberdeck_apps::grant grant = app_grant();
    if (!grant.allows(cyberdeck_apps::resource::storage)) {
        ESP_LOGE(k_tag, "app start failed stage=storage_grant sd_gate=%u",
                 app_sd_io() == nullptr ? 0U : static_cast<unsigned>(app_sd_io()->state()));
        return false;
    }
    if (!player_.start(app_sd_io(), grant)) {
        ESP_LOGE(k_tag, "app start failed stage=player");
        return false;
    }
    scan_done_ = xSemaphoreCreateBinary();
    if (scan_done_ == nullptr) {
        ESP_LOGE(k_tag, "app start failed stage=scan_completion_semaphore");
        (void)player_.stop(1000);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(scan_publication_mutex_);
        scan_cancel_.store(false, std::memory_order_release);
        scan_quiescing_.store(false, std::memory_order_release);
    }
    running_ = true;
    return true;
}
bool application::stop() {
    if (!running_)
        return true;
    {
        std::lock_guard<std::mutex> lock(scan_publication_mutex_);
        scan_quiescing_.store(true, std::memory_order_release);
        scan_cancel_.store(true, std::memory_order_release);
    }
    if (scan_join_pending_.load(std::memory_order_acquire)) {
        if (xSemaphoreTake(scan_done_, pdMS_TO_TICKS(5000)) != pdTRUE)
            return false;
        scan_join_pending_.store(false, std::memory_order_release);
    }
    if (!player_.stop(5000))
        return false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.clear();
    }
    if (scan_done_ != nullptr) {
        vSemaphoreDelete(scan_done_);
        scan_done_ = nullptr;
    }
    running_ = false;
    return true;
}

bool application::scan_directory(std::string_view path) {
    if (path.empty() || path.size() > k_max_path_bytes || scan_done_ == nullptr)
        return false;
    cyberdeck_sd_io::coordinator *storage = app_sd_io();
    if (storage == nullptr || storage->state() != cyberdeck_sd_io::gate_state::open)
        return false;
    if (scan_join_pending_.load(std::memory_order_acquire)) {
        if (scan_running_.load(std::memory_order_acquire) || xSemaphoreTake(scan_done_, 0) != pdTRUE)
            return false;
        scan_join_pending_.store(false, std::memory_order_release);
    }
    std::lock_guard<std::mutex> publication_lock(scan_publication_mutex_);
    if (scan_quiescing_.load(std::memory_order_acquire))
        return false;
    bool expected = false;
    if (!scan_running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;
    std::memcpy(scan_path_.data(), path.data(), path.size());
    scan_path_[path.size()] = '\0';
    scan_cancel_.store(false, std::memory_order_release);
    scan_succeeded_.store(false, std::memory_order_release);
    scan_attempted_.store(true, std::memory_order_release);
    scan_join_pending_.store(true, std::memory_order_release);
    if (xTaskCreate(scan_entry, "music_scan", 6144, this, 4, &scan_task_) != pdPASS) {
        scan_task_ = nullptr;
        scan_running_.store(false, std::memory_order_release);
        scan_join_pending_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void application::scan_entry(void *context) {
    auto *self = static_cast<application *>(context);
    self->scan_succeeded_.store(self->scan_directory_sync(self->scan_path_.data()), std::memory_order_release);
    self->scan_task_ = nullptr;
    self->scan_running_.store(false, std::memory_order_release);
    const SemaphoreHandle_t completed = self->scan_done_;
    xSemaphoreGive(completed);
    vTaskDelete(nullptr);
}

bool application::scan_directory_sync(std::string_view path) {
    auto lease =
        app_sd_io() == nullptr ? cyberdeck_sd_io::lease{} : app_sd_io()->acquire(cyberdeck_sd_io::access::read);
    if (!lease || !app_grant().allows(cyberdeck_apps::resource::storage))
        return false;
    if (!validate_directory_path(path))
        return false;
    const std::string virtual_dir(path);
    DIR *dir = cyberdeck_local_shell_opendir("/sdcard", "/", virtual_dir);
    if (dir == nullptr)
        return false;
    std::unique_ptr<std::array<track, k_max_tracks>> tracks(new (std::nothrow) std::array<track, k_max_tracks>{});
    if (!tracks) {
        (void)closedir(dir);
        return false;
    }
    std::size_t count = 0;
    bool overflow = false;
    while (dirent *entry = readdir(dir)) {
        if (scan_cancel_.load(std::memory_order_acquire) || !app_grant().valid()) {
            overflow = true;
            break;
        }
        const std::string_view name(entry->d_name);
        if (name.empty() || name == "." || name == ".." || name.size() > 128)
            continue;
        const std::string virtual_path = (virtual_dir == "/" ? "" : virtual_dir) + "/" + std::string(name);
        if (!is_mp3_path(virtual_path) || virtual_path.size() > k_max_path_bytes)
            continue;
        std::size_t verified_size = 0;
        const int descriptor =
            cyberdeck_local_shell_open_readonly("/sdcard", "/", virtual_path, k_max_track_bytes, verified_size);
        if (descriptor < 0)
            continue;
        if (close(descriptor) != 0)
            continue;
        if (count == tracks->size()) {
            overflow = true;
            break;
        }
        track &item = (*tracks)[count++];
        std::memcpy(item.path.data(), virtual_path.data(), virtual_path.size());
        item.path[virtual_path.size()] = '\0';
        item.size_bytes = static_cast<std::uint32_t>(verified_size);
        const std::size_t title_bytes = std::min(name.size(), k_max_title_bytes);
        std::memcpy(item.title.data(), name.data(), title_bytes);
        item.title[title_bytes] = '\0';
    }
    const bool closed = closedir(dir) == 0;
    if (overflow || !closed || count == 0 || !app_grant().valid())
        return false;
    std::lock_guard<std::mutex> publication_lock(scan_publication_mutex_);
    if (scan_cancel_.load(std::memory_order_acquire) || scan_quiescing_.load(std::memory_order_acquire) ||
        !app_grant().valid())
        return false;
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!queue_.replace_queue(tracks->data(), count) || !player_.set_queue(tracks->data(), count, 0))
        return false;
    return true;
}

cyberdeck_apps::result application::status() const {
    snapshot info{};
    if (!player_.status(info))
        return {cyberdeck_apps::result_status::rejected, "music: player unavailable\n"};
    std::lock_guard<std::mutex> lock(queue_mutex_);
    std::string text = "[ MUSIC / TUI ]\nstate: ";
    text += state_name(info.state);
    if (info.state == playback_state::error)
        text += std::string("\nerror_stage: ") + error_stage_name(info.failure_stage);
    text += "\ntrack: " + std::to_string(info.selected + 1) + "/" + std::to_string(info.track_count);
    if (info.current.title[0] != '\0')
        text += "\n" + std::string(info.current.title.data());
    if (info.current.artist[0] != '\0')
        text += " — " + std::string(info.current.artist.data());
    text += "\nprogress: " + std::to_string(info.position_ms / 1000) + "s / " +
            std::to_string(info.current.duration_ms / 1000) + "s";
    text += "\nvolume: " + std::to_string(info.volume) + "  shuffle: ";
    text += info.shuffle ? "on  repeat: " : "off  repeat: ";
    text += info.repeat == repeat_mode::one ? "one\n" : info.repeat == repeat_mode::all ? "all\n" : "off\n";
    if (scan_running_.load(std::memory_order_acquire))
        text += "queue: scanning...\n";
    else if (scan_attempted_.load(std::memory_order_acquire))
        text += scan_succeeded_.load(std::memory_order_acquire) ? "queue: scan complete\n" : "queue: scan failed\n";
    text += "queue: " + std::to_string(queue_.status().track_count) + " tracks\n";
    text += "controls: queue /dir | play [n] | pause | resume | stop | next | prev | seek <seconds> | volume <0-50> | "
            "shuffle | repeat\n";
    return {cyberdeck_apps::result_status::handled, std::move(text)};
}

cyberdeck_apps::result application::execute(std::string_view command, std::string_view args) {
    if (command != "music")
        return {};
    if (!running_ || !app_grant().valid())
        return {cyberdeck_apps::result_status::rejected, "music: service unavailable\n"};
    args = trim(args);
    if (args.empty() || args == "status" || args == "help")
        return status();
    if (args.rfind("queue ", 0) == 0) {
        if (!app_grant().allows(cyberdeck_apps::resource::storage))
            return {cyberdeck_apps::result_status::rejected, "music: storage unavailable\n"};
        if (!scan_directory(trim(args.substr(6))))
            return {cyberdeck_apps::result_status::rejected, "music: scan unavailable or another scan is active\n"};
        return {cyberdeck_apps::result_status::handled, "music: directory scan started\n"};
    }
    if (args == "list") {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::string text = "music queue:\n";
        const std::size_t count = std::min<std::size_t>(queue_.status().track_count, 16);
        for (std::size_t index = 0; index < count; ++index) {
            const track *item = queue_.at(index);
            if (item != nullptr)
                text += std::to_string(index + 1) + ". " + item->title.data() + "\n";
        }
        if (queue_.status().track_count > count)
            text += "... list limited to 16 of 64 tracks\n";
        return {cyberdeck_apps::result_status::handled, std::move(text)};
    }
    if (args == "pause" || args == "resume" || args == "stop") {
        const bool ok = args == "pause" ? player_.pause() : args == "resume" ? player_.resume() : player_.halt();
        return {ok ? cyberdeck_apps::result_status::handled : cyberdeck_apps::result_status::rejected,
                ok ? "music: command accepted\n" : "music: command unavailable\n"};
    }
    if (args.rfind("play", 0) == 0) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        const auto tail = trim(args.substr(4));
        if (!tail.empty()) {
            std::uint32_t index = 0;
            if (!parse_unsigned(tail, k_max_tracks, index) || index == 0 || !queue_.select(index - 1))
                return {cyberdeck_apps::result_status::rejected, "music: invalid track index\n"};
            player_.select(index - 1);
        }
        const track *item = queue_.current();
        const bool ok = item != nullptr && player_.play(item->path.data());
        return {ok ? cyberdeck_apps::result_status::handled : cyberdeck_apps::result_status::rejected,
                ok ? "music: playback requested\n" : "music: queue empty or playback busy\n"};
    }
    if (args == "next" || args == "prev") {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        snapshot playback{};
        if (player_.status(playback) && playback.track_count == queue_.status().track_count &&
            playback.selected < queue_.status().track_count) {
            if (playback.selected != queue_.status().selected)
                (void)queue_.select(playback.selected);
            queue_.set_position(playback.position_ms);
        }
        const bool moved = args == "next" ? queue_.next() : queue_.previous();
        const track *item = moved ? queue_.current() : nullptr;
        if (moved)
            player_.select(queue_.status().selected);
        const bool ok = item != nullptr && player_.play(item->path.data());
        return {ok ? cyberdeck_apps::result_status::handled : cyberdeck_apps::result_status::rejected,
                ok ? "music: playback requested\n" : "music: no adjacent track\n"};
    }
    if (args == "shuffle") {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.toggle_shuffle();
        player_.set_shuffle(queue_.status().shuffle);
        return {cyberdeck_apps::result_status::handled, "music: shuffle toggled\n"};
    }
    if (args == "repeat") {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.cycle_repeat();
        player_.set_repeat(queue_.status().repeat);
        return {cyberdeck_apps::result_status::handled, "music: repeat cycled\n"};
    }
    if (args.rfind("seek ", 0) == 0 || args.rfind("volume ", 0) == 0) {
        const bool seeking = args.rfind("seek ", 0) == 0;
        std::uint32_t value = 0;
        if (!parse_unsigned(trim(args.substr(seeking ? 5 : 7)), seeking ? UINT32_MAX / 1000 : 50, value))
            return {cyberdeck_apps::result_status::rejected, "music: invalid value\n"};
        const bool ok = seeking ? player_.seek(value * 1000) : player_.set_volume(static_cast<std::uint8_t>(value));
        return {ok ? cyberdeck_apps::result_status::handled : cyberdeck_apps::result_status::rejected,
                ok ? "music: command accepted\n" : "music: command unavailable\n"};
    }
    return {cyberdeck_apps::result_status::rejected, "music: unknown command; use music help\n"};
}

bool application::has_playback() const {
    snapshot info{};
    return player_.status(info) && (info.state == playback_state::playing || info.state == playback_state::paused);
}
bool application::poweroff_quiesce(std::uint32_t timeout_ms) {
    const TickType_t started = xTaskGetTickCount();
    const TickType_t budget = pdMS_TO_TICKS(timeout_ms);
    {
        std::lock_guard<std::mutex> lock(scan_publication_mutex_);
        scan_quiescing_.store(true, std::memory_order_release);
        scan_cancel_.store(true, std::memory_order_release);
    }
    if (scan_join_pending_.load(std::memory_order_acquire)) {
        if (scan_done_ == nullptr || xSemaphoreTake(scan_done_, budget) != pdTRUE)
            return false;
        scan_join_pending_.store(false, std::memory_order_release);
    }
    const TickType_t elapsed = xTaskGetTickCount() - started;
    if (elapsed >= budget)
        return false;
    const TickType_t remaining = budget - elapsed;
    if (!player_.stop(static_cast<std::uint32_t>(remaining * portTICK_PERIOD_MS)))
        return false;
    {
        std::lock_guard<std::mutex> lock(scan_publication_mutex_);
        scan_quiescing_.store(false, std::memory_order_release);
    }
    return true;
}
application &global_application() {
    static application instance;
    return instance;
}
} // namespace cyberdeck_music

extern "C" bool cyberdeck_music_poweroff_quiesce(std::uint32_t timeout_ms) {
    return cyberdeck_music::global_application().poweroff_quiesce(timeout_ms);
}
