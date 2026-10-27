#include "apps/music/cyberdeck_music_app.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/shell/cyberdeck_local_shell.h"
#include "freertos/task.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <vector>

namespace {
int checks = 0;
int failures = 0;
namespace fs = std::filesystem;
std::string scan_root;
std::mutex scan_gate_mutex;
std::condition_variable scan_gate_changed;
bool hold_scan_open = false;
bool scan_open_entered = false;
bool release_scan_open = false;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #condition "\n"; } \
} while (false)

class fake_player final : public cyberdeck_music::player_interface {
  public:
    bool start(cyberdeck_sd_io::coordinator *value, cyberdeck_apps::grant access) override {
        ++starts; storage = value; grant_valid = access.valid(); active = start_result; return active;
    }
    bool stop(std::uint32_t timeout) override { ++stops; last_timeout = timeout; active = false; return stop_result; }
    bool play(std::string_view path) override { played.emplace_back(path); return active && command_result; }
    bool pause() override { ++pauses; return active && command_result; }
    bool resume() override { ++resumes; return active && command_result; }
    bool halt() override { ++halts; return active && command_result; }
    bool seek(std::uint32_t value) override { seeks.push_back(value); return active && command_result; }
    bool set_volume(std::uint8_t value) override { volumes.push_back(value); return active && command_result; }
    bool set_queue(const cyberdeck_music::track *tracks, std::size_t count, std::size_t selected) override {
        ++queue_updates; queue_count = count; selected_track = selected; state.track_count = count;
        last_queue.clear();
        if (tracks != nullptr) last_queue.assign(tracks, tracks + count);
        return active && command_result;
    }
    void select(std::size_t value) override { selected_track = value; state.selected = value; }
    void set_repeat(cyberdeck_music::repeat_mode value) override { repeat = value; }
    void set_shuffle(bool value) override { shuffle = value; }
    bool status(cyberdeck_music::snapshot &out) const override { if (!active) return false; out = state; return true; }
    bool running() const override { return active; }

    int starts = 0, stops = 0, pauses = 0, resumes = 0, halts = 0, queue_updates = 0;
    std::uint32_t last_timeout = 0;
    cyberdeck_sd_io::coordinator *storage = nullptr;
    bool grant_valid = false, active = false, start_result = true, stop_result = true, command_result = true;
    std::size_t queue_count = 0, selected_track = 0;
    cyberdeck_music::repeat_mode repeat = cyberdeck_music::repeat_mode::off;
    bool shuffle = false;
    cyberdeck_music::snapshot state{};
    std::vector<cyberdeck_music::track> last_queue;
    std::vector<std::string> played;
    std::vector<std::uint32_t> seeks;
    std::vector<std::uint8_t> volumes;
};

class event_log_service final : public cyberdeck_apps::application {
  public:
    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override { started = true; return true; }
    bool stop() override { started = false; return true; }
    bool running() const override { return started; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override { return {}; }
  private:
    cyberdeck_apps::manifest manifest_{"cyberdeck.event_log", "event log", "1", "fixture", "", {}, 0,
        {}, 0, 1000, "1", cyberdeck_apps::app_type::service, {}, 0, 0, 0, {}, 0};
    bool started = false;
};

std::string map_sd_path(const char *path) {
    if (path == nullptr) return {};
    const std::string value(path);
    if (value == "/sdcard") return scan_root;
    if (value.rfind("/sdcard/", 0) == 0) return scan_root + value.substr(7);
    return value;
}

template <typename Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

void write_file(const fs::path &path, std::string_view content) {
    std::ofstream output(path, std::ios::binary);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
}
}

DIR *music_test_secure_opendir(const char *root, const char *cwd, std::string_view path) {
    if (root == nullptr || std::string_view(root) != "/sdcard" || cwd == nullptr ||
        std::string_view(cwd) != "/") return nullptr;
    return cyberdeck_local_shell_opendir(scan_root.c_str(), cwd, path);
}

int music_test_secure_open(const char *root, const char *cwd, std::string_view path,
                           std::size_t max_bytes, std::size_t &size) {
    if (root == nullptr || std::string_view(root) != "/sdcard" || cwd == nullptr ||
        std::string_view(cwd) != "/") return -1;
    {
        std::unique_lock<std::mutex> lock(scan_gate_mutex);
        if (hold_scan_open) {
            scan_open_entered = true;
            scan_gate_changed.notify_all();
            scan_gate_changed.wait(lock, [] { return release_scan_open; });
            hold_scan_open = false;
        }
    }
    return cyberdeck_local_shell_open_readonly(scan_root.c_str(), cwd, path, max_bytes, size);
}

extern "C" int __real_lstat(const char *, struct stat *);
extern "C" int __wrap_lstat(const char *path, struct stat *info) {
    const std::string mapped = map_sd_path(path);
    return __real_lstat(mapped.c_str(), info);
}

int main() {
    char temp_root[] = "/tmp/music-app-test-XXXXXX";
    const char *created_root = ::mkdtemp(temp_root);
    CHECK(created_root != nullptr);
    if (created_root == nullptr) return 1;
    scan_root = created_root;

    fake_player player;
    cyberdeck_music::application music(player);
    event_log_service log;
    cyberdeck_sd_io::coordinator storage;
    cyberdeck_apps::runtime runtime;
    runtime.set_sd_io(&storage);
    CHECK(runtime.register_application(log));
    CHECK(runtime.register_application(music));
    CHECK(music.execute("music", "status").status == cyberdeck_apps::result_status::rejected);
    CHECK(runtime.start_application("cyberdeck.music"));
    CHECK(music.running() && player.active);
    CHECK(player.starts == 1 && player.storage == &storage && player.grant_valid);

    CHECK(music.execute("other", "status").status == cyberdeck_apps::result_status::not_handled);
    auto result = music.execute("music", "help");
    CHECK(result.status == cyberdeck_apps::result_status::handled);
    CHECK(result.output.find("controls:") != std::string::npos);
    CHECK(music.execute("music", "play").status == cyberdeck_apps::result_status::rejected);
    CHECK(music.execute("music", "play 0").status == cyberdeck_apps::result_status::rejected);
    CHECK(music.execute("music", "play 65").status == cyberdeck_apps::result_status::rejected);
    CHECK(music.execute("music", "play 1").status == cyberdeck_apps::result_status::rejected);
    CHECK(player.played.empty());
    CHECK(music.execute("music", "playback").status == cyberdeck_apps::result_status::rejected);

    CHECK(music.execute("music", "pause").status == cyberdeck_apps::result_status::handled);
    CHECK(music.execute("music", "resume").status == cyberdeck_apps::result_status::handled);
    CHECK(music.execute("music", "stop").status == cyberdeck_apps::result_status::handled);
    CHECK(player.pauses == 1 && player.resumes == 1 && player.halts == 1);
    CHECK(music.execute("music", "seek 0").status == cyberdeck_apps::result_status::handled);
    CHECK(music.execute("music", "seek 4294967").status == cyberdeck_apps::result_status::handled);
    CHECK(player.seeks.size() == 2 && player.seeks[0] == 0 && player.seeks[1] == 4294967000U);
    CHECK(music.execute("music", "seek 4294968").status == cyberdeck_apps::result_status::rejected);
    CHECK(music.execute("music", "seek -1").status == cyberdeck_apps::result_status::rejected);
    CHECK(music.execute("music", "seek 1x").status == cyberdeck_apps::result_status::rejected);
    CHECK(music.execute("music", "volume 0").status == cyberdeck_apps::result_status::handled);
    CHECK(music.execute("music", "volume 50").status == cyberdeck_apps::result_status::handled);
    CHECK(music.execute("music", "volume 51").status == cyberdeck_apps::result_status::rejected);
    CHECK(music.execute("music", "volume 1 ").status == cyberdeck_apps::result_status::handled);
    CHECK(music.execute("music", "volume +1").status == cyberdeck_apps::result_status::handled);
    CHECK(player.volumes.size() == 4 && player.volumes[0] == 0 && player.volumes[1] == 50 &&
          player.volumes[3] == 1);

    CHECK(music.execute("music", "shuffle").status == cyberdeck_apps::result_status::handled);
    CHECK(player.shuffle);
    CHECK(music.execute("music", "repeat").status == cyberdeck_apps::result_status::handled);
    CHECK(player.repeat == cyberdeck_music::repeat_mode::all);
    player.command_result = false;
    CHECK(music.execute("music", "pause").status == cyberdeck_apps::result_status::rejected);
    player.command_result = true;

    const fs::path music_dir = fs::path(scan_root) / "music";
    fs::create_directories(music_dir);
    write_file(music_dir / "alpha.mp3", "alpha-data");
    write_file(music_dir / "Beta.MP3", "beta-data");
    write_file(music_dir / "ignored.txt", "not audio");
    write_file(music_dir / "empty.mp3", "");
    fs::create_directory(music_dir / "directory.mp3");
    write_file(fs::path(scan_root) / "outside.mp3", "outside");
    fs::create_symlink(fs::path(scan_root) / "outside.mp3", music_dir / "link.mp3");

    result = music.execute("music", "queue /music");
    CHECK(result.status == cyberdeck_apps::result_status::handled);
    CHECK(wait_until([&] {
        const auto current = music.execute("music", "status");
        return current.output.find("queue: scan complete") != std::string::npos;
    }, std::chrono::seconds(2)));
    CHECK(player.queue_updates == 1 && player.queue_count == 2);
    CHECK(player.last_queue.size() == 2);
    CHECK(music.execute("music", "list").output.find("alpha.mp3") != std::string::npos);
    CHECK(music.execute("music", "list").output.find("Beta.MP3") != std::string::npos);
    CHECK(music.execute("music", "list").output.find("ignored.txt") == std::string::npos);
    CHECK(music.execute("music", "list").output.find("link.mp3") == std::string::npos);

    fs::remove(fs::path(scan_root) / "outside.mp3");
    write_file(fs::path(scan_root) / "root-track.mp3", "root-track-data");
    {
        std::lock_guard<std::mutex> lock(scan_gate_mutex);
        hold_scan_open = true;
        scan_open_entered = false;
        release_scan_open = false;
    }
    CHECK(music.execute("music", "queue /").status == cyberdeck_apps::result_status::handled);
    {
        std::unique_lock<std::mutex> lock(scan_gate_mutex);
        CHECK(scan_gate_changed.wait_for(lock, std::chrono::seconds(2), [] { return scan_open_entered; }));
        release_scan_open = true;
    }
    scan_gate_changed.notify_all();
    CHECK(wait_until([&] {
        const auto current = music.execute("music", "status").output;
        return current.find("queue: scan complete") != std::string::npos &&
               current.find("queue: 1 tracks") != std::string::npos;
    }, std::chrono::seconds(2)));
    CHECK(player.queue_updates == 2 && player.queue_count == 1);
    CHECK(player.last_queue.size() == 1);
    CHECK(std::string_view(player.last_queue[0].path.data()) == "/root-track.mp3");
    CHECK(music.execute("music", "list").output.find("root-track.mp3") != std::string::npos);
    CHECK(music.execute("music", "list").output.find("alpha.mp3") == std::string::npos);

    fs::remove_all(music_dir);
    fs::create_directories(music_dir);
    for (int i = 0; i < 65; ++i) {
        char name[32]{};
        std::snprintf(name, sizeof(name), "track-%02d.mp3", i);
        write_file(music_dir / name, "x");
    }
    CHECK(music.execute("music", "queue /music").status == cyberdeck_apps::result_status::handled);
    CHECK(wait_until([&] {
        const auto current = music.execute("music", "status");
        return current.output.find("queue: scan failed") != std::string::npos;
    }, std::chrono::seconds(2)));
    CHECK(player.queue_updates == 2 && player.queue_count == 1);
    CHECK(music.execute("music", "list").output.find("root-track.mp3") != std::string::npos);

    player.state.state = cyberdeck_music::playback_state::playing;
    player.state.track_count = 2;
    player.state.selected = 0;
    player.state.position_ms = 4200;
    player.state.current.duration_ms = 12000;
    player.state.volume = 27;
    player.state.current.title = {'T','r','a','c','k','\0'};
    player.state.current.artist = {'A','r','t','i','s','t','\0'};
    player.state.shuffle = true;
    player.state.repeat = cyberdeck_music::repeat_mode::one;
    CHECK(music.has_playback());
    result = music.execute("music", "status");
    CHECK(result.status == cyberdeck_apps::result_status::handled);
    CHECK(result.output.find("state: playing") != std::string::npos);
    CHECK(result.output.find("track: 1/2") != std::string::npos);
    CHECK(result.output.find("Track — Artist") != std::string::npos);
    CHECK(result.output.find("progress: 4s / 12s") != std::string::npos);
    CHECK(result.output.find("volume: 27  shuffle: on  repeat: one") != std::string::npos);
    player.state.state = cyberdeck_music::playback_state::error;
    player.state.failure_stage = cyberdeck_music::error_stage::id3;
    result = music.execute("music", "status");
    CHECK(result.output.find("state: error") != std::string::npos);
    CHECK(result.output.find("error_stage: id3") != std::string::npos);
    player.state.state = cyberdeck_music::playback_state::playing;
    player.state.failure_stage = cyberdeck_music::error_stage::none;
    result = music.execute("music", "status");
    CHECK(result.output.find("error_stage:") == std::string::npos);
    player.state.selected = 1;
    result = music.execute("music", "status");
    CHECK(result.output.find("track: 2/2") != std::string::npos);
    result = music.execute("music", "next");
    CHECK(result.status == cyberdeck_apps::result_status::handled);
    CHECK(player.selected_track == 0);
    CHECK(music.execute("music", "status").output.find("track: 1/2") != std::string::npos);
    player.state.state = cyberdeck_music::playback_state::paused;
    CHECK(music.has_playback());
    player.state.state = cyberdeck_music::playback_state::stopped;
    CHECK(!music.has_playback());

    player.stop_result = false;
    CHECK(!music.poweroff_quiesce(100));
    CHECK(player.stops == 1);
    player.stop_result = true;
    CHECK(music.poweroff_quiesce(100));
    CHECK(player.stops == 2 && player.last_timeout <= 100);

    fs::remove_all(music_dir);
    fs::create_directories(music_dir);
    write_file(music_dir / "cancel.mp3", "cancel-data");
    {
        std::lock_guard<std::mutex> lock(scan_gate_mutex);
        hold_scan_open = true;
        scan_open_entered = false;
        release_scan_open = false;
    }
    CHECK(music.execute("music", "queue /music").status == cyberdeck_apps::result_status::handled);
    {
        std::unique_lock<std::mutex> lock(scan_gate_mutex);
        CHECK(scan_gate_changed.wait_for(lock, std::chrono::seconds(2), [] { return scan_open_entered; }));
    }
    CHECK(storage.active_leases() == 1);
    CHECK(!music.poweroff_quiesce(1));
    CHECK(storage.active_leases() == 1);
    {
        std::lock_guard<std::mutex> lock(scan_gate_mutex);
        release_scan_open = true;
    }
    scan_gate_changed.notify_all();
    CHECK(music.poweroff_quiesce(2000));
    CHECK(storage.active_leases() == 0);
    CHECK(player.queue_updates == 2 && player.queue_count == 1);
    CHECK(music.execute("music", "list").output.find("root-track.mp3") != std::string::npos);
    CHECK(player.stops == 3);

    CHECK(runtime.stop_application("cyberdeck.music"));
    CHECK(!music.running() && player.stops == 4);
    CHECK(!music.execute("music", "pause").output.empty());
    CHECK(runtime.stop_all());
    fs::remove_all(scan_root);
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
