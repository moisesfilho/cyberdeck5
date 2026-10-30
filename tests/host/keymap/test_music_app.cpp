#include "apps/music/cyberdeck_music_app.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/shell/cyberdeck_local_shell.h"
#include "platform/display/cyberdeck_music_command_input.h"
#include "freertos/task.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <cerrno>
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
std::atomic<bool> fail_next_readdir_after_entry{false};
bool readdir_failure_entered = false;
bool release_readdir_failure = false;
struct command_app_probe {
    cyberdeck_music::application *app;
    int dispatches = 0;
};
std::string dispatch_music_command(std::string_view command, void *context) {
    auto &probe = *static_cast<command_app_probe *>(context);
    ++probe.dispatches;
    return probe.app->player_command(command).output;
}
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #condition "\n"; } \
} while (false)

class fake_player final : public cyberdeck_music::player_interface {
  public:
    bool start(cyberdeck_sd_io::coordinator *value, cyberdeck_apps::grant access) override {
        ++starts; storage = value; grant_valid = access.valid(); active = start_result; return active;
    }
    bool stop(std::uint32_t timeout) override {
        ++stops;
        last_timeout = timeout;
        if (stop_result)
            active = false;
        return stop_result;
    }
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

/* Minimal ID3v2.3 tag with UTF-8 TIT2 (and optional TPE1) frames, enough to
 * prove the scan fills the track title/artist from metadata instead of the file
 * name. */
std::string id3_utf8_frame(const char *id, std::string_view text) {
    std::string payload(1, static_cast<char>(3));
    payload.append(text);
    std::string frame(id, 4);
    const auto frame_size = static_cast<std::uint32_t>(payload.size());
    frame.push_back(static_cast<char>((frame_size >> 24) & 0xff));
    frame.push_back(static_cast<char>((frame_size >> 16) & 0xff));
    frame.push_back(static_cast<char>((frame_size >> 8) & 0xff));
    frame.push_back(static_cast<char>(frame_size & 0xff));
    frame.append(2, '\0');
    frame += payload;
    return frame;
}

std::string id3_title_tag(std::string_view title, std::string_view artist = {}) {
    std::string frame = id3_utf8_frame("TIT2", title);
    if (!artist.empty())
        frame += id3_utf8_frame("TPE1", artist);
    std::string tag = "ID3";
    tag.push_back(static_cast<char>(3));
    tag.append(2, '\0');
    const auto tag_size = static_cast<std::uint32_t>(frame.size());
    tag.push_back(static_cast<char>((tag_size >> 21) & 0x7f));
    tag.push_back(static_cast<char>((tag_size >> 14) & 0x7f));
    tag.push_back(static_cast<char>((tag_size >> 7) & 0x7f));
    tag.push_back(static_cast<char>(tag_size & 0x7f));
    tag += frame;
    return tag;
}
}

DIR *music_test_secure_opendir(const char *root, const char *cwd, std::string_view path) {
    if (root == nullptr || std::string_view(root) != "/sdcard" || cwd == nullptr ||
        std::string_view(cwd) != "/") return nullptr;
    return cyberdeck_local_shell_opendir(scan_root.c_str(), cwd, path);
}

extern "C" dirent *__real_readdir(DIR *);
extern "C" dirent *__wrap_readdir(DIR *directory) {
    dirent *entry = __real_readdir(directory);
    bool expected = true;
    if (entry != nullptr && fail_next_readdir_after_entry.compare_exchange_strong(expected, false)) {
        std::unique_lock<std::mutex> lock(scan_gate_mutex);
        readdir_failure_entered = true;
        scan_gate_changed.notify_all();
        scan_gate_changed.wait(lock, [] { return release_readdir_failure; });
        errno = EIO;
        return nullptr;
    }
    return entry;
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
    fs::create_directories(fs::path(scan_root) / "musica");
    write_file(fs::path(scan_root) / "musica" / "opening.mp3", "opening-data");
    cyberdeck_apps::result result = music.execute("music", "");
    CHECK(result.status == cyberdeck_apps::result_status::handled);
    CHECK(music.consume_player_request());
    CHECK(wait_until([&] {
        return music.execute("music", "status").output.find("queue: scan complete") != std::string::npos;
    }, std::chrono::seconds(2)));
    CHECK(music.execute("music", "status").status == cyberdeck_apps::result_status::handled);
    cyberdeck_music_command_input::controller command_input;
    command_app_probe command_probe{&music};
    command_input.open();
    command_input.insert_text("music status");
    CHECK(command_input.handle(cyberdeck_music_command_input::key::enter, dispatch_music_command, &command_probe,
                               nullptr, nullptr) == cyberdeck_music_command_input::key_result::executed);
    CHECK(command_probe.dispatches == 1 &&
          command_input.feedback().find("queue: scan complete") != std::string_view::npos);
    CHECK(player.played.empty());
    CHECK(music.execute("music", "select 1").status == cyberdeck_apps::result_status::handled);
    CHECK(player.played.empty());
    CHECK(player.selected_track == 0);
    CHECK(music.execute("music", "select 0").status == cyberdeck_apps::result_status::rejected);

    CHECK(music.execute("other", "status").status == cyberdeck_apps::result_status::not_handled);
    result = music.execute("music", "help");
    CHECK(result.status == cyberdeck_apps::result_status::handled);
    CHECK(result.output.find("music [status|help|") != std::string::npos);
    CHECK(music.execute("music", "status").status == cyberdeck_apps::result_status::handled);
    CHECK(music.execute("music", "play").status == cyberdeck_apps::result_status::handled);
    CHECK(music.execute("music", "play 0").status == cyberdeck_apps::result_status::rejected);
    CHECK(music.execute("music", "play 65").status == cyberdeck_apps::result_status::rejected);
    CHECK(music.execute("music", "play 1").status == cyberdeck_apps::result_status::handled);
    CHECK(player.played.size() == 2);
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
    fs::create_directories(music_dir / "nested");
    write_file(music_dir / "nested" / "inside.mp3", "nested-data");
    fs::path too_deep = music_dir;
    for (int depth = 1; depth <= 9; ++depth) {
        too_deep /= "level";
        fs::create_directory(too_deep);
    }
    write_file(too_deep / "too-deep.mp3", "too-deep-data");
    write_file(music_dir / "ignored.txt", "not audio");
    write_file(music_dir / "empty.mp3", "");
    write_file(music_dir / "tagged.mp3", id3_title_tag("Caf\xc3\xa9"));
    fs::create_directory(music_dir / "directory.mp3");
    write_file(fs::path(scan_root) / "outside.mp3", "outside");
    fs::create_symlink(fs::path(scan_root) / "outside.mp3", music_dir / "link.mp3");

    result = music.execute("music", "queue /music");
    CHECK(result.status == cyberdeck_apps::result_status::handled);
    CHECK(wait_until([&] {
        const auto current = music.execute("music", "status");
        return current.output.find("queue: scan complete") != std::string::npos;
    }, std::chrono::seconds(2)));
    CHECK(player.queue_updates == 4 && player.queue_count == 4);
    CHECK(player.last_queue.size() == 4);
    CHECK(std::string_view(player.last_queue[0].path.data()) == "/music/Beta.MP3");
    CHECK(std::string_view(player.last_queue[1].path.data()) == "/music/alpha.mp3");
    CHECK(std::string_view(player.last_queue[2].path.data()) == "/music/nested/inside.mp3");
    CHECK(std::string_view(player.last_queue[3].path.data()) == "/music/tagged.mp3");
    /* ID3 title for a tagged file, file-name fallback (with its extension) for
     * the rest. */
    CHECK(std::string_view(player.last_queue[3].title.data()) == "Caf\xc3\xa9");
    CHECK(std::string_view(player.last_queue[1].title.data()) == "alpha.mp3");
    CHECK(music.execute("music", "list").output.find("Caf\xc3\xa9") != std::string::npos);
    CHECK(music.execute("music", "list").output.find("alpha.mp3") != std::string::npos);
    CHECK(music.execute("music", "list").output.find("Beta.MP3") != std::string::npos);
    CHECK(music.execute("music", "list").output.find("inside.mp3") != std::string::npos);
    CHECK(music.execute("music", "list").output.find("too-deep.mp3") == std::string::npos);
    CHECK(music.execute("music", "list").output.find("ignored.txt") == std::string::npos);
    CHECK(music.execute("music", "list").output.find("link.mp3") == std::string::npos);

    fs::remove(fs::path(scan_root) / "outside.mp3");
    write_file(fs::path(scan_root) / "root-track.mp3", "root-track-data");
    player.state.state = cyberdeck_music::playback_state::playing;
    const std::size_t play_requests_before_scan = player.played.size();
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
               current.find("queue: 6 tracks") != std::string::npos;
    }, std::chrono::seconds(2)));
    CHECK(player.queue_updates == 6 && player.queue_count == 6);
    CHECK(player.last_queue.size() == 6);
    CHECK(std::string_view(player.last_queue[0].path.data()) == "/music/Beta.MP3");
    /* The recursive root scan reaches the tagged fixture too, and the ID3 title
     * survives the queue replacement the same way the file-name fallback does. */
    CHECK(std::string_view(player.last_queue[3].path.data()) == "/music/tagged.mp3");
    CHECK(std::string_view(player.last_queue[3].title.data()) == "Caf\xc3\xa9");
    CHECK(std::string_view(player.last_queue[4].path.data()) == "/musica/opening.mp3");
    CHECK(std::string_view(player.last_queue[5].path.data()) == "/root-track.mp3");
    CHECK(std::string_view(player.last_queue[5].title.data()) == "root-track.mp3");
    CHECK(player.state.state == cyberdeck_music::playback_state::playing);
    CHECK(player.played.size() == play_requests_before_scan);
    CHECK(music.execute("music", "list").output.find("root-track.mp3") != std::string::npos);
    CHECK(music.execute("music", "list").output.find("alpha.mp3") != std::string::npos);

    fs::remove_all(music_dir);
    fs::create_directories(music_dir);
    for (int i = 0; i < 65; ++i) {
        char name[32]{};
        std::snprintf(name, sizeof(name), "track-%02d.mp3", i);
        write_file(music_dir / name, "x");
    }
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
        release_scan_open = true;
    }
    scan_gate_changed.notify_all();
    CHECK(wait_until([&] {
        const auto current = music.execute("music", "status");
        return current.output.find("queue: scan complete") != std::string::npos &&
               current.output.find("queue: truncated at 64 tracks") != std::string::npos;
    }, std::chrono::seconds(2)));
    CHECK(player.queue_updates == 8 && player.queue_count == 64);
    CHECK(music.execute("music", "list").output.find("root-track.mp3") == std::string::npos);
    player.state.state = cyberdeck_music::playback_state::playing;
    player.state.track_count = 2;
    player.state.selected = 0;
    player.state.position_ms = 4200;
    player.state.current.duration_ms = 12000;
    player.state.volume = 27;
    player.state.current.title = {'T','r','a','c','k','\0'};
    player.state.current.artist = {'A', 'r', 't', 'i', 's', 't', '\0'};
    player.state.shuffle = true;
    player.state.repeat = cyberdeck_music::repeat_mode::one;
    CHECK(music.has_playback());
    result = music.execute("music", "status");
    CHECK(result.status == cyberdeck_apps::result_status::handled);
    CHECK(result.output.find("state: playing") != std::string::npos);
    CHECK(result.output.find("track: 1/2") != std::string::npos);
    CHECK(result.output.find("Track - Artist") != std::string::npos);
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
    CHECK(player.selected_track == 1);
    CHECK(music.execute("music", "status").output.find("track: 2/2") != std::string::npos);

    {
        std::lock_guard<std::mutex> lock(scan_gate_mutex);
        readdir_failure_entered = false;
        release_readdir_failure = false;
    }
    fail_next_readdir_after_entry.store(true);
    CHECK(music.execute("music", "queue /music").status == cyberdeck_apps::result_status::handled);
    {
        std::unique_lock<std::mutex> lock(scan_gate_mutex);
        CHECK(scan_gate_changed.wait_for(lock, std::chrono::seconds(2), [] { return readdir_failure_entered; }));
        release_readdir_failure = true;
    }
    scan_gate_changed.notify_all();
    CHECK(wait_until(
        [&] { return music.execute("music", "status").output.find("queue: scan failed") != std::string::npos; },
        std::chrono::seconds(2)));
    CHECK(player.queue_count == 0);
    CHECK(music.execute("music", "list").output.find("track-") == std::string::npos);

    player.state.state = cyberdeck_music::playback_state::paused;
    CHECK(music.has_playback());
    player.state.state = cyberdeck_music::playback_state::stopped;
    CHECK(!music.has_playback());

    CHECK(music.execute("music", "queue /missing").status == cyberdeck_apps::result_status::handled);
    CHECK(wait_until(
        [&] { return music.execute("music", "status").output.find("queue: scan failed") != std::string::npos; },
        std::chrono::seconds(2)));
    CHECK(player.queue_count == 0);
    CHECK(music.execute("music", "list").output.find("track-") == std::string::npos);

    /* TEST-MUSIC-NAME-01: a file name that is not valid UTF-8 must reach the
     * queue through the same model sanitizer as the ID3 UTF-8 path, so the
     * panel never receives a raw, undecodable byte. The bytes on disk stay
     * untouched; only the displayed title is re-encoded. */
    fs::remove_all(music_dir);
    fs::create_directories(music_dir);
    const std::string invalid_name("bad\xff.mp3", 8);
    write_file(music_dir / invalid_name, "invalid-name-data");
    CHECK(music.execute("music", "queue /music").status == cyberdeck_apps::result_status::handled);
    CHECK(wait_until([&] {
        const auto current = music.execute("music", "status").output;
        return current.find("queue: scan complete") != std::string::npos &&
               current.find("queue: 1 tracks") != std::string::npos;
    }, std::chrono::seconds(2)));
    CHECK(player.last_queue.size() == 1);
    CHECK(std::string_view(player.last_queue[0].path.data()) == "/music/bad\xff.mp3");
    CHECK(std::string_view(player.last_queue[0].title.data()) == "bad?.mp3");
    CHECK(music.execute("music", "list").output.find("bad?.mp3") != std::string::npos);
    CHECK(fs::exists(music_dir / invalid_name));

    /* The scan reuses one heap candidate for every file, so a tagged file that
     * sorts first must not leak its title/artist into the untagged file after
     * it (the reuse reset is the only thing standing between them). */
    fs::remove_all(music_dir);
    fs::create_directories(music_dir);
    write_file(music_dir / "tagged.mp3", id3_title_tag("Tagged", "Artist"));
    write_file(music_dir / "zeta.mp3", "zeta-data");
    CHECK(music.execute("music", "queue /music").status == cyberdeck_apps::result_status::handled);
    CHECK(wait_until([&] {
        const auto current = music.execute("music", "status").output;
        return current.find("queue: scan complete") != std::string::npos &&
               current.find("queue: 2 tracks") != std::string::npos;
    }, std::chrono::seconds(2)));
    CHECK(player.last_queue.size() == 2);
    CHECK(std::string_view(player.last_queue[0].path.data()) == "/music/tagged.mp3");
    CHECK(std::string_view(player.last_queue[0].title.data()) == "Tagged");
    CHECK(std::string_view(player.last_queue[0].artist.data()) == "Artist");
    CHECK(std::string_view(player.last_queue[1].path.data()) == "/music/zeta.mp3");
    CHECK(std::string_view(player.last_queue[1].title.data()) == "zeta.mp3");
    CHECK(std::string_view(player.last_queue[1].artist.data()).empty());

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
    CHECK(player.queue_count == 0);
    CHECK(music.execute("music", "list").output.find("cancel.mp3") == std::string::npos);
    CHECK(!music.poweroff_quiesce(1));
    CHECK(storage.active_leases() == 1);
    {
        std::lock_guard<std::mutex> lock(scan_gate_mutex);
        release_scan_open = true;
    }
    scan_gate_changed.notify_all();
    CHECK(music.poweroff_quiesce(2000));
    CHECK(storage.active_leases() == 0);
    /* 9 scans / 6 published queues: the TEST-MUSIC-NAME-01 sanitized-name scan
     * and the candidate-reuse scan each add one scan and one publication. */
    constexpr int scan_count = 9;
    constexpr int published_queue_count = 6;
    CHECK(player.queue_updates == scan_count + published_queue_count);
    CHECK(player.queue_count == 0);
    CHECK(music.execute("music", "list").output.find("cancel.mp3") == std::string::npos);

    player.stop_result = false;
    CHECK(!music.poweroff_quiesce(100));
    CHECK(player.stops == 2);
    player.stop_result = true;
    CHECK(music.poweroff_quiesce(100));
    CHECK(player.stops == 3 && player.last_timeout <= 100);
    CHECK(runtime.stop_application("cyberdeck.music"));
    CHECK(runtime.start_application("cyberdeck.music"));

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
    const int stops_before_escape = player.stops;
    player.stop_result = false;
    std::atomic<bool> escape_stop_done{false};
    std::atomic<bool> escape_stop_result{true};
    std::thread escape_stop([&] {
        escape_stop_result.store(runtime.stop_application("cyberdeck.music"), std::memory_order_release);
        escape_stop_done.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!escape_stop_done.load(std::memory_order_acquire));
    CHECK(player.stops == stops_before_escape);
    CHECK(storage.active_leases() == 1);
    {
        std::lock_guard<std::mutex> lock(scan_gate_mutex);
        release_scan_open = true;
    }
    scan_gate_changed.notify_all();
    CHECK(wait_until([&] { return escape_stop_done.load(std::memory_order_acquire); }, std::chrono::seconds(2)));
    escape_stop.join();
    CHECK(!escape_stop_result.load(std::memory_order_acquire));
    CHECK(player.stops == stops_before_escape + 1);
    CHECK(music.running() && runtime.active_foreground() == "cyberdeck.music");
    CHECK(storage.active_leases() == 0 && player.queue_count == 0);
    player.stop_result = true;
    CHECK(runtime.stop_application("cyberdeck.music"));
    CHECK(!music.running() && runtime.active_foreground().empty());

    CHECK(runtime.start_application("cyberdeck.music"));
    const auto retried_music_grant = music.app_grant();
    player.stop_result = false;
    CHECK(!runtime.stop_application("cyberdeck.music"));
    CHECK(music.running() && runtime.active_foreground() == "cyberdeck.music");
    CHECK(retried_music_grant.valid() && retried_music_grant.allows(cyberdeck_apps::resource::storage) &&
          !retried_music_grant.allows(cyberdeck_apps::resource::input));
    player.stop_result = true;
    CHECK(runtime.stop_application("cyberdeck.music"));
    CHECK(!music.running() && runtime.active_foreground().empty() && !retried_music_grant.valid());
    CHECK(!music.execute("music", "pause").output.empty());

    write_file(fs::path(scan_root) / "musica" / "reopened.mp3", "reopened-data");
    CHECK(runtime.start_application("cyberdeck.music"));
    CHECK(music.execute("music", "").status == cyberdeck_apps::result_status::handled);
    CHECK(music.consume_player_request());
    CHECK(wait_until(
        [&] { return music.execute("music", "status").output.find("queue: scan complete") != std::string::npos; },
        std::chrono::seconds(2)));
    CHECK(music.execute("music", "list").output.find("reopened.mp3") != std::string::npos);

    /* TEST-MUSIC-ESC-04: closing the player and reopening it must start a new
     * scan and list the tracks again, twice in a row, without a stale queue. */
    for (int cycle = 0; cycle < 2; ++cycle) {
        const int stops_before_cycle = player.stops;
        const int starts_before_cycle = player.starts;
        CHECK(runtime.stop_application("cyberdeck.music"));
        CHECK(!music.running() && runtime.active_foreground().empty());
        CHECK(player.stops == stops_before_cycle + 1 && !player.active);
        CHECK(runtime.start_application("cyberdeck.music"));
        CHECK(music.running() && runtime.active_foreground() == "cyberdeck.music");
        CHECK(player.starts == starts_before_cycle + 1 && player.active);
        CHECK(music.execute("music", "").status == cyberdeck_apps::result_status::handled);
        CHECK(music.consume_player_request());
        CHECK(wait_until(
            [&] { return music.execute("music", "status").output.find("queue: scan complete") != std::string::npos; },
            std::chrono::seconds(2)));
        CHECK(music.execute("music", "list").output.find("reopened.mp3") != std::string::npos);
    }
    CHECK(runtime.stop_application("cyberdeck.music"));
    CHECK(runtime.stop_all());
    fs::remove_all(scan_root);
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
