#include "apps/music/cyberdeck_music_player.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/runtime/cyberdeck_app_storage.h"
#include "apps/shell/cyberdeck_local_shell.h"
#include "platform/audio/cyberdeck_audio_output.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {
int checks = 0;
int failures = 0;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #condition "\n"; } \
} while (false)

std::mutex open_mutex;
std::mutex audio_mutex;
std::condition_variable open_changed;
std::condition_variable audio_changed;
bool hold_first_open = true;
bool first_open_entered = false;
bool release_first_open = false;
bool hold_fd_close = true;
bool fd_close_entered = false;
bool release_fd_close = false;
int blocked_fd = -1;
std::string fixture_path;
bool gate_audio_write = false;
bool audio_write_entered = false;
bool release_audio_write = false;
unsigned audio_write_count = 0;
bool fail_audio_start = false;
bool fail_audio_write = false;
bool fail_secure_open = false;
bool fail_player_read = false;
bool fail_player_close = false;

bool write_valid_silent_mp3(const std::string &path) {
    constexpr std::size_t frame_bytes = 417;
    constexpr unsigned frame_count = 24;
    std::array<unsigned char, frame_bytes * frame_count> frames{};
    for (unsigned frame = 0; frame < frame_count; ++frame) {
        const std::size_t offset = frame * frame_bytes;
        frames[offset] = 0xFF;
        frames[offset + 1] = 0xFB;
        frames[offset + 2] = 0x90;
        frames[offset + 3] = 0x64;
    }
    const int fd = ::open(path.c_str(), O_WRONLY | O_TRUNC);
    if (fd < 0) return false;
    const bool written = ::write(fd, frames.data(), frames.size()) ==
                         static_cast<ssize_t>(frames.size());
    const bool closed = ::close(fd) == 0;
    return written && closed;
}

bool write_large_id3_mp3(const std::string &path) {
    constexpr std::size_t tag_payload = 70 * 1024;
    constexpr std::size_t frame_bytes = 417;
    constexpr unsigned frame_count = 24;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    const unsigned char header[] = {'I','D','3',4,0,0,
        static_cast<unsigned char>((tag_payload >> 21) & 0x7f),
        static_cast<unsigned char>((tag_payload >> 14) & 0x7f),
        static_cast<unsigned char>((tag_payload >> 7) & 0x7f),
        static_cast<unsigned char>(tag_payload & 0x7f)};
    output.write(reinterpret_cast<const char *>(header), sizeof(header));
    std::string padding(tag_payload, '\0');
    output.write(padding.data(), static_cast<std::streamsize>(padding.size()));
    std::array<unsigned char, frame_bytes * frame_count> frames{};
    for (unsigned i = 0; i < frame_count; ++i) {
        frames[i * frame_bytes] = 0xff; frames[i * frame_bytes + 1] = 0xfb;
        frames[i * frame_bytes + 2] = 0x90; frames[i * frame_bytes + 3] = 0x64;
    }
    output.write(reinterpret_cast<const char *>(frames.data()), static_cast<std::streamsize>(frames.size()));
    return output.good();
}

class storage_owner final : public cyberdeck_apps::application {
  public:
    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override { started = true; return true; }
    bool stop() override { started = false; return true; }
    bool running() const override { return started; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override { return {}; }
  private:
    cyberdeck_apps::manifest manifest_{"music.test.storage", "storage", "1", "fixture", "", {}, 0,
        {"storage"}, 1, 1000, "1", cyberdeck_apps::app_type::service, {}, 0, 0, 0, {}, 0};
    bool started = false;
};

template <typename Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

}

int cyberdeck_local_shell_open_readonly(const char *root, const char *cwd, std::string_view path,
                                         std::size_t max_bytes, std::size_t &file_size) {
    if (root == nullptr || std::string_view(root) != "/sdcard" || cwd == nullptr ||
        std::string_view(cwd) != "/" || path != "/stream.mp3" || max_bytes == 0) return -1;
    if (fail_secure_open) return -1;
    {
        std::unique_lock<std::mutex> lock(open_mutex);
        if (hold_first_open) {
            first_open_entered = true;
            open_changed.notify_all();
            open_changed.wait(lock, [] { return release_first_open; });
            hold_first_open = false;
        }
    }
    const int fd = ::open(fixture_path.c_str(), O_RDONLY);
    if (fd < 0) return -1;
    struct stat info{};
    if (::fstat(fd, &info) != 0 || info.st_size < 0 || static_cast<std::size_t>(info.st_size) > max_bytes) {
        ::close(fd); return -1;
    }
    file_size = static_cast<std::size_t>(info.st_size);
    {
        std::lock_guard<std::mutex> lock(open_mutex);
        blocked_fd = fd;
    }
    return fd;
}

bool wait_for_failure(cyberdeck_music::player &subject, cyberdeck_music::error_stage expected) {
    return wait_until([&] {
        cyberdeck_music::snapshot state{};
        return subject.status(state) && state.state == cyberdeck_music::playback_state::error &&
               state.failure_stage == expected;
    }, std::chrono::seconds(2));
}

extern "C" int __real_close(int fd);
extern "C" int __wrap_close(int fd) {
    bool inject_failure = false;
    {
        std::unique_lock<std::mutex> lock(open_mutex);
        if (fd == blocked_fd && fail_player_close) {
            fail_player_close = false;
            inject_failure = true;
        }
        if (fd == blocked_fd && hold_fd_close) {
            fd_close_entered = true;
            open_changed.notify_all();
            open_changed.wait(lock, [] { return release_fd_close; });
            hold_fd_close = false;
            blocked_fd = -1;
        }
    }
    const int result = __real_close(fd);
    return inject_failure ? -1 : result;
}

extern "C" ssize_t __real_read(int fd, void *buffer, size_t count);
extern "C" ssize_t __wrap_read(int fd, void *buffer, size_t count) {
    {
        std::lock_guard<std::mutex> lock(open_mutex);
        if (fd == blocked_fd && fail_player_read) {
            fail_player_read = false;
            return -1;
        }
    }
    return __real_read(fd, buffer, count);
}

namespace cyberdeck_audio_output {
bool start(std::uint32_t rate, std::uint8_t channels, std::uint8_t) {
    return !fail_audio_start && rate >= 8000 && rate <= 48000 && (channels == 1 || channels == 2);
}
bool write(const std::int16_t *samples, std::size_t count) {
    if (samples == nullptr || count == 0) return false;
    std::unique_lock<std::mutex> lock(audio_mutex);
    ++audio_write_count;
    if (gate_audio_write && !audio_write_entered) {
        audio_write_entered = true;
        audio_changed.notify_all();
        audio_changed.wait(lock, [] { return release_audio_write; });
    }
    return !fail_audio_write;
}
bool set_volume(std::uint8_t) { return false; }
void stop() {}
}

int main() {
    char path[] = "/tmp/music-player-test-XXXXXX";
    const int temp = ::mkstemp(path);
    CHECK(temp >= 0);
    fixture_path = path;
    const unsigned char invalid_mp3[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                                         0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    CHECK(::write(temp, invalid_mp3, sizeof(invalid_mp3)) == static_cast<ssize_t>(sizeof(invalid_mp3)));
    CHECK(::close(temp) == 0);

    storage_owner owner;
    cyberdeck_apps::runtime runtime;
    CHECK(runtime.register_application(owner));
    CHECK(runtime.start_application("music.test.storage"));
    const auto access = runtime.app_grant("music.test.storage");
    CHECK(access.valid() && access.allows(cyberdeck_apps::resource::storage));

    cyberdeck_sd_io::coordinator storage;
    cyberdeck_music::player subject;
    CHECK(!subject.running());
    CHECK(!subject.play("/outside.mp3"));
    CHECK(!subject.pause());
    CHECK(!subject.set_volume(51));
    music_test_fail_caps_queue_create = true;
    CHECK(!subject.start(&storage, access));
    CHECK(music_test_last_queue_caps == static_cast<unsigned>(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    music_test_fail_caps_queue_create = false;
    music_test_fail_binary_semaphore_create = true;
    CHECK(!subject.start(&storage, access));
    music_test_fail_binary_semaphore_create = false;
    CHECK(music_test_delete_without_completion_count() == 0);
    music_test_fail_internal_allocation = true;
    CHECK(!subject.start(&storage, access));
    CHECK(music_test_last_heap_caps == static_cast<unsigned>(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    music_test_fail_internal_allocation = false;
    music_test_fail_static_task_create = true;
    CHECK(!subject.start(&storage, access));
    music_test_fail_static_task_create = false;
    CHECK(music_test_delete_without_completion_count() == 0);
    CHECK(subject.start(&storage, access));
    CHECK(music_test_last_queue_caps == static_cast<unsigned>(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    CHECK(subject.running());
    CHECK(subject.start(&storage, access));
    CHECK(subject.running());
    CHECK(!subject.play("/folder/../stream.mp3"));
    CHECK(!subject.seek(UINT32_MAX));
    CHECK(!subject.set_volume(51));

    cyberdeck_music::track current{};
    CHECK(subject.set_queue(nullptr, 0, 0));
    std::strcpy(current.path.data(), "/stream.mp3");
    std::strcpy(current.title.data(), "stream");
    current.size_bytes = sizeof(invalid_mp3);
    const cyberdeck_music::track queue[]{current, current};
    CHECK(subject.set_queue(queue, 1, 0));
    CHECK(!subject.set_queue(queue, 1, 1));
    CHECK(!subject.set_queue(nullptr, 1, 0));
    CHECK(subject.play("/stream.mp3"));

    {
        std::unique_lock<std::mutex> lock(open_mutex);
        CHECK(open_changed.wait_for(lock, std::chrono::seconds(2), [] { return first_open_entered; }));
    }
    CHECK(storage.active_leases() == 1);
    for (int i = 0; i < 8; ++i) CHECK(subject.play("/stream.mp3"));
    CHECK(!subject.play("/stream.mp3"));

    CHECK(!subject.stop(10));
    CHECK(subject.running());
    CHECK(storage.active_leases() == 1);
    {
        std::lock_guard<std::mutex> lock(open_mutex);
        release_first_open = true;
    }
    open_changed.notify_all();
    {
        std::unique_lock<std::mutex> lock(open_mutex);
        CHECK(open_changed.wait_for(lock, std::chrono::seconds(2), [] { return fd_close_entered; }));
    }
    CHECK(storage.active_leases() == 1);
    std::atomic<bool> stop_result{true};
    const unsigned resets_before_race = music_test_queue_reset_count();
    std::thread stopper([&] { stop_result.store(subject.stop(25), std::memory_order_release); });
    CHECK(wait_until([&] { return music_test_queue_reset_count() > resets_before_race; },
                     std::chrono::seconds(1)));
    CHECK(!subject.play("/stream.mp3"));
    stopper.join();
    CHECK(!stop_result.load(std::memory_order_acquire));
    CHECK(storage.active_leases() == 1);
    {
        std::lock_guard<std::mutex> lock(open_mutex);
        release_fd_close = true;
    }
    open_changed.notify_all();
    CHECK(subject.stop(2000));
    CHECK(!subject.running());
    CHECK(storage.active_leases() == 0);
    CHECK(wait_until([] { return music_test_delete_without_completion_count() == 0; }, std::chrono::seconds(1)));

    CHECK(subject.start(&storage, access));
    CHECK(subject.set_queue(queue, 1, 0));
    fail_secure_open = true;
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_for_failure(subject, cyberdeck_music::error_stage::open));
    fail_secure_open = false;
    {
        std::ofstream malformed(fixture_path, std::ios::binary | std::ios::trunc);
        malformed.write("ID3\x01\0\0\0\0\0\0", 10);
    }
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_for_failure(subject, cyberdeck_music::error_stage::id3));
    {
        std::ofstream invalid(fixture_path, std::ios::binary | std::ios::trunc);
        invalid.write(reinterpret_cast<const char *>(invalid_mp3), sizeof(invalid_mp3));
    }
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_for_failure(subject, cyberdeck_music::error_stage::decode));

    CHECK(write_valid_silent_mp3(fixture_path));
    fail_player_read = true;
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_for_failure(subject, cyberdeck_music::error_stage::read));
    fail_player_close = true;
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_for_failure(subject, cyberdeck_music::error_stage::io));
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        gate_audio_write = true;
        audio_write_entered = false;
        release_audio_write = false;
        audio_write_count = 0;
    }
    CHECK(subject.start(&storage, access));
    CHECK(subject.set_queue(queue, 2, 0));
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        release_audio_write = true;
    }
    fail_audio_start = true;
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_for_failure(subject, cyberdeck_music::error_stage::codec_start));
    fail_audio_start = false;
    fail_audio_write = true;
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_for_failure(subject, cyberdeck_music::error_stage::codec_write));
    fail_audio_write = false;
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        audio_write_entered = false;
        release_audio_write = false;
    }
    subject.set_repeat(cyberdeck_music::repeat_mode::all);
    CHECK(subject.play("/stream.mp3"));
    {
        std::unique_lock<std::mutex> lock(audio_mutex);
        CHECK(audio_changed.wait_for(lock, std::chrono::seconds(2), [] { return audio_write_entered; }));
    }
    CHECK(subject.pause());
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        release_audio_write = true;
    }
    audio_changed.notify_all();
    CHECK(wait_until([&] {
        cyberdeck_music::snapshot state{};
        return subject.status(state) && state.state == cyberdeck_music::playback_state::paused;
    }, std::chrono::seconds(2)));
    cyberdeck_music::snapshot paused{};
    CHECK(subject.status(paused) && paused.state == cyberdeck_music::playback_state::paused && paused.position_ms > 0);
    CHECK(subject.seek(500));
    CHECK(subject.resume());
    CHECK(wait_until([&] {
        cyberdeck_music::snapshot state{};
        return subject.status(state) && state.state == cyberdeck_music::playback_state::playing &&
               state.selected == 1;
    }, std::chrono::seconds(2)));
    cyberdeck_music::snapshot completed{};
    CHECK(subject.status(completed) && completed.state == cyberdeck_music::playback_state::playing &&
          completed.selected == 1);
    CHECK(audio_write_count > 1);
    CHECK(storage.active_leases() == 1);
    std::ifstream persisted(fixture_path, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(persisted)), std::istreambuf_iterator<char>());
    CHECK(after.size() == 24U * 417U);
    CHECK(after[0] == static_cast<char>(0xFF) && static_cast<unsigned char>(after[1]) == 0xFB);
    CHECK(subject.stop(2000));
    CHECK(!subject.running());
    CHECK(storage.active_leases() == 0);
    CHECK(wait_until([] { return music_test_delete_without_completion_count() == 0; }, std::chrono::seconds(1)));
    CHECK(music_test_delete_without_completion_count() == 0);

    CHECK(write_large_id3_mp3(fixture_path));
    struct stat large_info{};
    CHECK(::stat(fixture_path.c_str(), &large_info) == 0);
    current.size_bytes = static_cast<std::uint32_t>(large_info.st_size);
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        audio_write_count = 0;
    }
    CHECK(subject.start(&storage, access));
    CHECK(subject.set_queue(&current, 1, 0));
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_until([&] {
        cyberdeck_music::snapshot state{};
        return subject.status(state) && state.state == cyberdeck_music::playback_state::playing &&
               state.failure_stage == cyberdeck_music::error_stage::none;
    }, std::chrono::seconds(2)));
    CHECK(wait_until([&] {
        std::lock_guard<std::mutex> lock(audio_mutex);
        return audio_write_count > 1;
    }, std::chrono::seconds(2)));
    cyberdeck_music::snapshot large_tag_playback{};
    CHECK(subject.status(large_tag_playback));
    CHECK(large_tag_playback.failure_stage == cyberdeck_music::error_stage::none);
    CHECK(large_tag_playback.current.size_bytes == static_cast<std::uint32_t>(large_info.st_size));
    CHECK(subject.stop(2000));
    CHECK(wait_until([] { return music_test_delete_without_completion_count() == 0; }, std::chrono::seconds(1)));
    CHECK(runtime.stop_all());
    CHECK(::unlink(path) == 0);
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
