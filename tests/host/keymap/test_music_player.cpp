#include "apps/music/cyberdeck_music_player.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/runtime/cyberdeck_app_storage.h"
#include "apps/shell/cyberdeck_local_shell.h"
#include "platform/audio/cyberdeck_audio_output.h"
#include "third_party/minimp3/minimp3.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
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
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

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
std::string expected_secure_open_path = "/stream.mp3";
std::vector<std::string> secure_open_paths;
bool gate_audio_write = false;
bool audio_write_entered = false;
bool release_audio_write = false;
bool gate_each_write = false;
bool write_waiting = false;
bool release_write = false;
unsigned write_gate_count = 0;
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

/* VBR fixture: 60 frames alternating 417 bytes (128 kbps) and 1044 bytes
 * (320 kbps) at 44.1 kHz.  A per-frame bitrate estimate changes on every frame,
 * which is exactly the progress-bar jitter REQ-MUSIC-06 fixes. */
bool write_vbr_mp3(const std::string &path) {
    std::vector<unsigned char> frames;
    for (unsigned frame = 0; frame < 60; ++frame) {
        const bool high = (frame % 2) == 1;
        const std::size_t bytes = high ? 1044 : 417;
        const std::size_t offset = frames.size();
        frames.resize(offset + bytes, 0);
        frames[offset] = 0xFF;
        frames[offset + 1] = 0xFB;
        frames[offset + 2] = high ? 0xE0 : 0x90;
        frames[offset + 3] = 0x64;
    }
    const int fd = ::open(path.c_str(), O_WRONLY | O_TRUNC);
    if (fd < 0) return false;
    const bool written = ::write(fd, frames.data(), frames.size()) ==
                         static_cast<ssize_t>(frames.size());
    return ::close(fd) == 0 && written;
}

/* Same CBR stream preceded by three stray bytes: the duration probe cannot
 * parse the first header, so the player must freeze the estimate taken from the
 * first decoded frame instead of re-estimating later. */
bool write_garbage_prefixed_mp3(const std::string &path) {
    constexpr std::size_t frame_bytes = 417;
    constexpr unsigned frame_count = 24;
    std::vector<unsigned char> frames(3 + frame_bytes * frame_count, 0);
    frames[0] = 0x12;
    frames[1] = 0x34;
    frames[2] = 0x56;
    for (unsigned frame = 0; frame < frame_count; ++frame) {
        const std::size_t offset = 3 + frame * frame_bytes;
        frames[offset] = 0xFF;
        frames[offset + 1] = 0xFB;
        frames[offset + 2] = 0x90;
        frames[offset + 3] = 0x64;
    }
    const int fd = ::open(path.c_str(), O_WRONLY | O_TRUNC);
    if (fd < 0) return false;
    const bool written = ::write(fd, frames.data(), frames.size()) ==
                         static_cast<ssize_t>(frames.size());
    return ::close(fd) == 0 && written;
}

/* Four decodable silent frames followed by valid-looking headers whose payload
 * cannot be decoded: minimp3 reports a plausible frame (rate/channels/bitrate)
 * and zero samples for the garbage frames.  Because the prefix already decoded,
 * only the bounded consecutive-empty-frame ceiling can end this stream — a
 * corrupt tail must still fail with `error_stage=decode` instead of reaching EOF
 * as a clean stop. */
bool write_zero_sample_mp3(const std::string &path) {
    constexpr std::size_t frame_bytes = 417;
    constexpr unsigned silent_frames = 4;
    constexpr unsigned garbage_frames = 200;
    std::vector<unsigned char> frames(frame_bytes * (silent_frames + garbage_frames), 0);
    for (unsigned frame = 0; frame < silent_frames; ++frame) {
        const std::size_t offset = frame * frame_bytes;
        frames[offset] = 0xFF;
        frames[offset + 1] = 0xFB;
        frames[offset + 2] = 0x90;
        frames[offset + 3] = 0x64;
    }
    std::uint32_t state = 12345;
    for (unsigned frame = silent_frames; frame < silent_frames + garbage_frames; ++frame) {
        const std::size_t offset = frame * frame_bytes;
        frames[offset] = 0xFF;
        frames[offset + 1] = 0xFB;
        frames[offset + 2] = 0x90;
        frames[offset + 3] = 0x40;
        for (std::size_t byte = offset + 4; byte < offset + frame_bytes; ++byte) {
            state = state * 1664525U + 1013904223U;
            frames[byte] = static_cast<unsigned char>(state >> 24);
        }
    }
    const int fd = ::open(path.c_str(), O_WRONLY | O_TRUNC);
    if (fd < 0) return false;
    const bool written = ::write(fd, frames.data(), frames.size()) ==
                         static_cast<ssize_t>(frames.size());
    return ::close(fd) == 0 && written;
}

/* ID3v2 skip and Xing/Info TOC offset of the committed fixture, plus the
 * consecutive zero-sample frames decoded from a byte offset.  The helpers keep
 * TEST-MUSIC-DUR-13 tied to the real fixture instead of a hardcoded layout. */
std::size_t fixture_audio_offset(const std::vector<char> &bytes) {
    if (bytes.size() < 10 || std::memcmp(bytes.data(), "ID3", 3) != 0)
        return 0;
    const auto byte = [&bytes](std::size_t index) {
        return static_cast<std::size_t>(static_cast<std::uint8_t>(bytes[index]));
    };
    return 10 + ((byte(6) << 21) | (byte(7) << 14) | (byte(8) << 7) | byte(9));
}

bool read_fixture_bytes(std::vector<char> &out) {
    const std::string source = __FILE__;
    const auto slash = source.find_last_of('/');
    const std::string directory =
        slash == std::string::npos ? std::string{} : source.substr(0, slash + 1);
    std::ifstream input(directory + "fixtures/music/vbr-1s.mp3", std::ios::binary);
    if (!input) return false;
    out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !out.empty();
}

bool write_fixture_bytes(const std::vector<char> &bytes, const std::string &path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

/* Offset of the first TOC entry inside the Xing/Info tag, or 0 when absent. */
std::size_t fixture_toc_offset(const std::vector<char> &bytes) {
    const std::size_t audio = fixture_audio_offset(bytes);
    /* MPEG-1 joint-stereo side info is 32 bytes: the tag starts at frame + 36. */
    const std::size_t tag = audio + 4 + 32;
    if (audio == 0 || bytes.size() < tag + 8)
        return 0;
    if (std::memcmp(bytes.data() + tag, "Xing", 4) != 0 &&
        std::memcmp(bytes.data() + tag, "Info", 4) != 0)
        return 0;
    const auto byte = [&bytes](std::size_t index) {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[index]));
    };
    const std::uint32_t flags = (byte(tag + 4) << 24) | (byte(tag + 5) << 16) |
                                (byte(tag + 6) << 8) | byte(tag + 7);
    if ((flags & 0x4) == 0)
        return 0;
    std::size_t cursor = tag + 8;
    if ((flags & 0x1) != 0) cursor += 4; /* frame count */
    if ((flags & 0x2) != 0) cursor += 4; /* stream size */
    return bytes.size() < cursor + 100 ? 0 : cursor;
}

/* Consecutive zero-sample frames the production decoder reports from `byte`.
 * A seek that lands in the middle of a VBR frame produces this sequence because
 * the bit reservoir of the previous frame is missing. */
unsigned zero_sample_prefix(const std::vector<char> &bytes, std::size_t byte) {
    if (byte >= bytes.size())
        return 0;
    mp3dec_t decoder{};
    mp3dec_init(&decoder);
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    unsigned zeros = 0;
    std::size_t cursor = byte;
    for (unsigned frame = 0; frame < 8 && cursor < bytes.size(); ++frame) {
        mp3dec_frame_info_t info{};
        const int samples = mp3dec_decode_frame(
            &decoder, reinterpret_cast<const std::uint8_t *>(bytes.data() + cursor),
            static_cast<int>(bytes.size() - cursor), pcm, &info);
        if (info.frame_bytes <= 0)
            break;
        cursor += static_cast<std::size_t>(info.frame_bytes);
        if (samples != 0)
            break;
        ++zeros;
    }
    return zeros;
}

/* Real 1 s VBR fixture committed at
 * tests/host/keymap/fixtures/music/vbr-1s.mp3 (5196 bytes, ID3v2 tag + Xing
 * TOC), generated with:
 *   ffmpeg -f lavfi -i "sine=frequency=440:duration=1" -ac 2 -ar 44100 \
 *          -codec:a libmp3lame -q:a 4 vbr-1s.mp3 */
bool copy_fixture_to(const std::string &destination) {
    const std::string source = __FILE__;
    const auto slash = source.find_last_of('/');
    const std::string directory =
        slash == std::string::npos ? std::string{} : source.substr(0, slash + 1);
    std::ifstream input(directory + "fixtures/music/vbr-1s.mp3", std::ios::binary);
    if (!input) return false;
    const std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
    if (bytes.empty()) return false;
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

/* ID3v2.3 tag with UTF-8 TIT2/TPE1 frames (any of them optional) followed by
 * valid frames, so the player decodes the tag and still reaches real playback. */
std::string id3_utf8_frame(const char *id, std::string_view text) {
    std::string payload(1, '\x03');
    payload.append(text);
    std::string frame(id, 4);
    const auto size = static_cast<std::uint32_t>(payload.size());
    frame.push_back(static_cast<char>((size >> 24) & 0xff));
    frame.push_back(static_cast<char>((size >> 16) & 0xff));
    frame.push_back(static_cast<char>((size >> 8) & 0xff));
    frame.push_back(static_cast<char>(size & 0xff));
    frame.append(2, '\0');
    frame += payload;
    return frame;
}

bool write_id3_tagged_mp3(const std::string &path, std::string_view title, std::string_view artist) {
    std::string frames;
    if (!title.empty()) frames += id3_utf8_frame("TIT2", title);
    if (!artist.empty()) frames += id3_utf8_frame("TPE1", artist);
    std::string tag = "ID3";
    tag.push_back('\x03');
    tag.append(2, '\0');
    const auto tag_size = static_cast<std::uint32_t>(frames.size());
    tag.push_back(static_cast<char>((tag_size >> 21) & 0x7f));
    tag.push_back(static_cast<char>((tag_size >> 14) & 0x7f));
    tag.push_back(static_cast<char>((tag_size >> 7) & 0x7f));
    tag.push_back(static_cast<char>(tag_size & 0x7f));
    tag += frames;
    constexpr std::size_t frame_bytes = 417;
    constexpr unsigned frame_count = 24;
    std::array<unsigned char, frame_bytes * frame_count> audio{};
    for (unsigned frame = 0; frame < frame_count; ++frame) {
        audio[frame * frame_bytes] = 0xff;
        audio[frame * frame_bytes + 1] = 0xfb;
        audio[frame * frame_bytes + 2] = 0x90;
        audio[frame * frame_bytes + 3] = 0x64;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(tag.data(), static_cast<std::streamsize>(tag.size()));
    output.write(reinterpret_cast<const char *>(audio.data()), static_cast<std::streamsize>(audio.size()));
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

/* TEST-MUSIC-ESC-02: the shim records the teardown order so a test can prove
 * that no handle is released before the completion take. */
bool trailing_events_are(std::initializer_list<const char *> expected) {
    const auto events = music_test_teardown_events();
    if (events.size() < expected.size()) return false;
    const std::size_t offset = events.size() - expected.size();
    std::size_t index = 0;
    for (const char *name : expected) {
        if (events[offset + index] != name) return false;
        ++index;
    }
    return true;
}

}

int cyberdeck_local_shell_open_readonly(const char *root, const char *cwd, std::string_view path,
                                         std::size_t max_bytes, std::size_t &file_size) {
    if (root == nullptr || std::string_view(root) != "/sdcard" || cwd == nullptr ||
        std::string_view(cwd) != "/" || max_bytes == 0) return -1;
    {
        std::unique_lock<std::mutex> lock(open_mutex);
        if (path != expected_secure_open_path)
            return -1;
        secure_open_paths.emplace_back(path);
        if (hold_first_open) {
            first_open_entered = true;
            open_changed.notify_all();
            open_changed.wait(lock, [] { return release_first_open; });
            hold_first_open = false;
        }
    }
    if (fail_secure_open) return -1;
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
    if (gate_each_write) {
        ++write_gate_count;
        write_waiting = true;
        audio_changed.notify_all();
        audio_changed.wait(lock, [] { return release_write; });
        release_write = false;
        write_waiting = false;
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
    /* TEST-MUSIC-ESC-02/04: a successful stop parks the worker once and
     * releases task, queue and semaphore exactly once, in that order, after the
     * completion take; the following start must be able to play again. */
    const auto stop_releases_once = [&]() -> bool {
        const unsigned parks = music_test_park_count();
        const unsigned deletes = music_test_delete_with_caps_count;
        const unsigned queue_deletes = music_test_queue_delete_count();
        const unsigned semaphore_deletes = music_test_semaphore_delete_count();
        music_test_clear_teardown_events();
        if (!subject.stop(2000)) return false;
        return music_test_park_count() == parks + 1 &&
               music_test_delete_with_caps_count == deletes + 1 &&
               music_test_queue_delete_count() == queue_deletes + 1 &&
               music_test_semaphore_delete_count() == semaphore_deletes + 1 &&
               music_test_invalid_task_delete_count == 0 &&
               music_test_entry_returned_without_delete_count() == 0 &&
               trailing_events_are({"park", "semaphore_take", "task_delete_with_caps", "queue_delete",
                                    "semaphore_delete"});
    };
    /* Host playback is unpaced: a whole track can finish between two polls.
     * Hold the worker inside its first PCM write so `playing` is observable
     * instead of relying on a stale snapshot from the previous session. */
    const auto begin_gated_playback = [&](std::string_view path) -> bool {
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            gate_audio_write = true;
            audio_write_entered = false;
            release_audio_write = false;
        }
        if (!subject.play(path)) return false;
        std::unique_lock<std::mutex> lock(audio_mutex);
        return audio_changed.wait_for(lock, std::chrono::seconds(2), [] { return audio_write_entered; });
    };
    const auto release_playback = [&]() {
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            gate_audio_write = false;
            release_audio_write = true;
        }
        audio_changed.notify_all();
    };
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
    /* TEST-MUSIC-ESC-05: a failing worker creation must not leave the queue or
     * the completion semaphore alive, and must never park or delete a task. */
    const unsigned parks_before_task_failure = music_test_park_count();
    const unsigned deletes_before_task_failure = music_test_delete_with_caps_count;
    const unsigned queue_deletes_before_task_failure = music_test_queue_delete_count();
    const unsigned semaphore_deletes_before_task_failure = music_test_semaphore_delete_count();
    music_test_fail_task_create_with_caps = true;
    CHECK(!subject.start(&storage, access));
    CHECK(music_test_last_task_caps == static_cast<unsigned>(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    CHECK(music_test_last_task_stack_bytes == 8192);
    music_test_fail_task_create_with_caps = false;
    CHECK(!subject.running());
    CHECK(music_test_park_count() == parks_before_task_failure);
    CHECK(music_test_delete_with_caps_count == deletes_before_task_failure);
    CHECK(music_test_queue_delete_count() == queue_deletes_before_task_failure + 1);
    CHECK(music_test_semaphore_delete_count() == semaphore_deletes_before_task_failure + 1);
    CHECK(music_test_invalid_task_delete_count == 0);
    CHECK(music_test_delete_without_completion_count() == 0);
    CHECK(subject.start(&storage, access));
    CHECK(music_test_last_task_caps == static_cast<unsigned>(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    CHECK(music_test_last_task_stack_bytes == 8192);
    CHECK(music_test_last_queue_caps == static_cast<unsigned>(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    CHECK(subject.running());
    /* TEST-MUSIC-ESC-08: a session that never played anything reports stopped
     * with an empty current track and no position. */
    {
        cyberdeck_music::snapshot pristine{};
        CHECK(subject.status(pristine));
        CHECK(pristine.state == cyberdeck_music::playback_state::stopped);
        CHECK(std::string_view(pristine.current.title.data()).empty());
        CHECK(std::string_view(pristine.current.path.data()).empty());
        CHECK(pristine.position_ms == 0);
    }
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

    /* TEST-MUSIC-ESC-03: a timed-out stop keeps the worker, queue, semaphore
     * and initialized state untouched so the same call can be retried. */
    const unsigned parks_before_timeouts = music_test_park_count();
    const unsigned deletes_before_timeouts = music_test_delete_with_caps_count;
    const unsigned queue_deletes_before_timeouts = music_test_queue_delete_count();
    const unsigned semaphore_deletes_before_timeouts = music_test_semaphore_delete_count();
    CHECK(!subject.stop(10));
    CHECK(subject.running());
    CHECK(storage.active_leases() == 1);
    CHECK(music_test_park_count() == parks_before_timeouts);
    CHECK(music_test_delete_with_caps_count == deletes_before_timeouts);
    CHECK(music_test_queue_delete_count() == queue_deletes_before_timeouts);
    CHECK(music_test_semaphore_delete_count() == semaphore_deletes_before_timeouts);
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
    CHECK(subject.running());
    CHECK(music_test_park_count() == parks_before_timeouts);
    CHECK(music_test_delete_with_caps_count == deletes_before_timeouts);
    CHECK(music_test_queue_delete_count() == queue_deletes_before_timeouts);
    CHECK(music_test_semaphore_delete_count() == semaphore_deletes_before_timeouts);
    {
        std::lock_guard<std::mutex> lock(open_mutex);
        release_fd_close = true;
    }
    open_changed.notify_all();
    /* The retry waits for the parked worker and releases each handle exactly
     * once, after the completion take.  The park itself may already have
     * happened before this call, so the check is on the trailing order. */
    CHECK(wait_until([&] { return music_test_park_count() == parks_before_timeouts + 1; },
                     std::chrono::seconds(2)));
    music_test_clear_teardown_events();
    CHECK(subject.stop(2000));
    CHECK(!subject.running());
    CHECK(storage.active_leases() == 0);
    CHECK(music_test_park_count() == parks_before_timeouts + 1);
    CHECK(music_test_delete_with_caps_count == deletes_before_timeouts + 1);
    CHECK(music_test_queue_delete_count() == queue_deletes_before_timeouts + 1);
    CHECK(music_test_semaphore_delete_count() == semaphore_deletes_before_timeouts + 1);
    CHECK(music_test_invalid_task_delete_count == 0);
    CHECK(music_test_entry_returned_without_delete_count() == 0);
    CHECK(trailing_events_are({"semaphore_take", "task_delete_with_caps", "queue_delete", "semaphore_delete"}));
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
    CHECK(stop_releases_once());
    CHECK(!subject.running());
    CHECK(storage.active_leases() == 0);
    CHECK(wait_until([] { return music_test_delete_without_completion_count() == 0; }, std::chrono::seconds(1)));
    CHECK(music_test_delete_without_completion_count() == 0);

    /* TEST-MUSIC-ESC-03: once the teardown succeeded, a repeated stop is a
     * no-op, so the released task/queue/semaphore can never be freed twice. */
    {
        const unsigned deletes_after_teardown = music_test_delete_with_caps_count;
        const unsigned queue_deletes_after_teardown = music_test_queue_delete_count();
        const unsigned semaphore_deletes_after_teardown = music_test_semaphore_delete_count();
        CHECK(subject.stop(0));
        CHECK(music_test_delete_with_caps_count == deletes_after_teardown);
        CHECK(music_test_queue_delete_count() == queue_deletes_after_teardown);
        CHECK(music_test_semaphore_delete_count() == semaphore_deletes_after_teardown);
        CHECK(music_test_invalid_task_delete_count == 0);
    }

    CHECK(subject.start(&storage, access));
    /* The current-track title is also rendered by the UI, so the basename
     * fallback must use the same bounded UTF-8 sanitizer as the queue scan. */
    const auto check_title_fallback = [&](std::string_view path, std::string_view expected) {
        std::size_t previous_open_count = 0;
        {
            std::lock_guard<std::mutex> lock(open_mutex);
            expected_secure_open_path.assign(path);
            previous_open_count = secure_open_paths.size();
        }
        CHECK(subject.play(path));
        CHECK(wait_until([&] {
            cyberdeck_music::snapshot state{};
            return subject.status(state) && state.state == cyberdeck_music::playback_state::error &&
                   state.failure_stage == cyberdeck_music::error_stage::decode &&
                   std::string_view(state.current.title.data()) == expected;
        }, std::chrono::seconds(2)));
        cyberdeck_music::snapshot state{};
        CHECK(subject.status(state));
        CHECK(std::string_view(state.current.title.data()) == expected);
        {
            std::lock_guard<std::mutex> lock(open_mutex);
            CHECK(secure_open_paths.size() == previous_open_count + 1 &&
                  secure_open_paths.back() == path);
            expected_secure_open_path = "/stream.mp3";
        }
    };
    {
        std::ofstream malformed(fixture_path, std::ios::binary | std::ios::trunc);
        malformed.write(reinterpret_cast<const char *>(invalid_mp3), sizeof(invalid_mp3));
    }
    check_title_fallback(std::string_view("/bad\xff.mp3", 9), "bad?.mp3");
    check_title_fallback("/Caf\xc3\xa9.mp3", "Caf\xc3\xa9.mp3");
    const std::string long_basename = "/" + std::string(cyberdeck_music::k_max_title_bytes - 1, 'a') +
                                      "\xc3\xa7.mp3";
    const std::string expected_truncated(cyberdeck_music::k_max_title_bytes - 1, 'a');
    check_title_fallback(long_basename, expected_truncated);

    /* The tag wins over the file name, and the artist only comes from the tag:
     * the basename fallback never fills `artist`. A missing TPE1 frame must
     * leave the artist empty instead of inheriting a stale value. */
    CHECK(write_id3_tagged_mp3(fixture_path, "Tag title", "Tag artist"));
    CHECK(begin_gated_playback("/stream.mp3"));
    cyberdeck_music::snapshot tagged{};
    CHECK(subject.status(tagged));
    CHECK(tagged.state == cyberdeck_music::playback_state::playing);
    CHECK(std::string_view(tagged.current.title.data()) == "Tag title");
    CHECK(std::string_view(tagged.current.artist.data()) == "Tag artist");
    CHECK(std::string_view(tagged.current.path.data()) == "/stream.mp3");
    CHECK(tagged.failure_stage == cyberdeck_music::error_stage::none);
    release_playback();
    CHECK(stop_releases_once());
    CHECK(subject.start(&storage, access));

    CHECK(write_id3_tagged_mp3(fixture_path, "Only title", std::string_view{}));
    CHECK(begin_gated_playback("/stream.mp3"));
    cyberdeck_music::snapshot title_only_tag{};
    CHECK(subject.status(title_only_tag));
    CHECK(title_only_tag.state == cyberdeck_music::playback_state::playing);
    CHECK(std::string_view(title_only_tag.current.title.data()) == "Only title");
    CHECK(std::string_view(title_only_tag.current.artist.data()).empty());
    release_playback();
    CHECK(stop_releases_once());
    CHECK(subject.start(&storage, access));

    CHECK(write_large_id3_mp3(fixture_path));
    struct stat large_info{};
    CHECK(::stat(fixture_path.c_str(), &large_info) == 0);
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        audio_write_count = 0;
    }
    CHECK(stop_releases_once());
    CHECK(subject.start(&storage, access));
    CHECK(begin_gated_playback("/stream.mp3"));
    cyberdeck_music::snapshot large_tag_playback{};
    CHECK(subject.status(large_tag_playback));
    CHECK(large_tag_playback.state == cyberdeck_music::playback_state::playing);
    CHECK(large_tag_playback.failure_stage == cyberdeck_music::error_stage::none);
    release_playback();
    CHECK(wait_until([&] {
        std::lock_guard<std::mutex> lock(audio_mutex);
        return audio_write_count > 1;
    }, std::chrono::seconds(2)));
    CHECK(subject.status(large_tag_playback));
    CHECK(large_tag_playback.failure_stage == cyberdeck_music::error_stage::none);
    CHECK(large_tag_playback.current.size_bytes == static_cast<std::uint32_t>(large_info.st_size));
    CHECK(stop_releases_once());

    /* REQ-MUSIC-06 / TEST-MUSIC-DUR-05: the published total is a session
     * constant.  This VBR fixture alternates 128 kbps (417 B) and 320 kbps
     * (1044 B) frames, so the old per-frame estimate changed on every frame and
     * made elapsed/total jump back and forth. */
    const auto sample_durations = [&](std::array<std::uint32_t, 3> &out) -> bool {
        bool ok = true;
        unsigned observed = 0;
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            observed = write_gate_count;
        }
        for (std::size_t index = 0; index < out.size(); ++index) {
            {
                std::unique_lock<std::mutex> lock(audio_mutex);
                ok = ok && audio_changed.wait_for(lock, std::chrono::seconds(2),
                                                  [&observed] { return write_gate_count > observed; });
                observed = write_gate_count;
            }
            cyberdeck_music::snapshot state{};
            const bool sampled = subject.status(state);
            ok = ok && sampled && state.state == cyberdeck_music::playback_state::playing;
            out[index] = sampled ? state.current.duration_ms : 0;
            {
                std::lock_guard<std::mutex> lock(audio_mutex);
                release_write = true;
            }
            audio_changed.notify_all();
        }
        return ok;
    };
    const auto gate_playback_writes = [&] {
        std::lock_guard<std::mutex> lock(audio_mutex);
        gate_each_write = true;
        write_waiting = false;
        release_write = false;
    };
    const auto release_each_write = [&] {
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            gate_each_write = false;
            release_write = true;
        }
        audio_changed.notify_all();
    };
    const auto wait_for_eof = [&] {
        return wait_until([&] {
            cyberdeck_music::snapshot state{};
            return subject.status(state) && state.state == cyberdeck_music::playback_state::stopped &&
                   state.position_ms > 0;
        }, std::chrono::seconds(5));
    };
    const auto eof_drift = [&](std::uint32_t expected_duration) -> long long {
        cyberdeck_music::snapshot end{};
        if (!subject.status(end))
            return 1LL << 40;
        if (end.current.duration_ms != expected_duration)
            return 1LL << 40;
        return static_cast<long long>(end.position_ms) - static_cast<long long>(end.current.duration_ms);
    };

    CHECK(write_vbr_mp3(fixture_path));
    CHECK(subject.start(&storage, access));
    CHECK(subject.set_queue(queue, 1, 0));
    subject.set_repeat(cyberdeck_music::repeat_mode::off);
    gate_playback_writes();
    CHECK(subject.play("/stream.mp3"));
    std::array<std::uint32_t, 3> vbr_durations{};
    CHECK(sample_durations(vbr_durations));
    CHECK(vbr_durations[0] > 0);
    CHECK(vbr_durations[0] == vbr_durations[1]);
    CHECK(vbr_durations[1] == vbr_durations[2]);
    release_each_write();
    CHECK(wait_for_eof());
    {
        const long long drift = eof_drift(vbr_durations[0]);
        CHECK(drift >= -1000 && drift <= 1000);
    }
    CHECK(stop_releases_once());

    /* TEST-MUSIC-DUR-06: the probe cannot parse a stream that starts with stray
     * bytes, so the player freezes the estimate from the first decoded frame
     * and keeps it constant; the EOF position still matches the total. */
    CHECK(write_garbage_prefixed_mp3(fixture_path));
    CHECK(subject.start(&storage, access));
    CHECK(subject.set_queue(queue, 1, 0));
    gate_playback_writes();
    CHECK(subject.play("/stream.mp3"));
    std::array<std::uint32_t, 3> fallback_durations{};
    CHECK(sample_durations(fallback_durations));
    CHECK(fallback_durations[0] == 0);
    CHECK(fallback_durations[1] > 0);
    CHECK(fallback_durations[1] == fallback_durations[2]);
    release_each_write();
    CHECK(wait_for_eof());
    {
        const long long drift = eof_drift(fallback_durations[1]);
        CHECK(drift >= -1000 && drift <= 1000);
    }
    CHECK(stop_releases_once());

    /* AC-MUSIC-16 / TEST-MUSIC-DUR-07: seeking into a VBR frame must not kill
     * the playback.  The old seek mapped the target proportionally by byte, so
     * it landed in the middle of a frame; minimp3 answered with zero samples
     * for an already-consumed frame and the loop treated that as fatal
     * (`error_stage=decode`). */
    CHECK(copy_fixture_to(fixture_path));
    {
        struct stat fixture_info{};
        CHECK(::stat(fixture_path.c_str(), &fixture_info) == 0);
        CHECK(fixture_info.st_size == 5196);
    }
    CHECK(subject.start(&storage, access));
    CHECK(subject.set_queue(queue, 1, 0));
    subject.set_repeat(cyberdeck_music::repeat_mode::off);
    gate_playback_writes();
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_until([] {
        std::lock_guard<std::mutex> lock(audio_mutex);
        return write_waiting;
    }, std::chrono::seconds(2)));
    {
        cyberdeck_music::snapshot before_seek{};
        CHECK(subject.status(before_seek));
        CHECK(before_seek.state == cyberdeck_music::playback_state::playing);
        CHECK(before_seek.current.duration_ms > 0);
    }
    unsigned writes_before_seek = 0;
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        writes_before_seek = write_gate_count;
    }
    CHECK(subject.seek(400));
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        release_write = true;
    }
    audio_changed.notify_all();
    CHECK(wait_until([&] {
        std::lock_guard<std::mutex> lock(audio_mutex);
        return write_gate_count > writes_before_seek;
    }, std::chrono::seconds(2)));
    {
        cyberdeck_music::snapshot after_seek{};
        CHECK(subject.status(after_seek));
        CHECK(after_seek.state == cyberdeck_music::playback_state::playing);
        CHECK(after_seek.failure_stage == cyberdeck_music::error_stage::none);
        CHECK(std::abs(static_cast<long>(after_seek.position_ms) - 400L) <= 1000L);
    }
    release_each_write();
    CHECK(wait_for_eof());
    CHECK(stop_releases_once());

    /* TEST-MUSIC-DUR-13 / AC-MUSIC-16: the bounded empty-frame tolerance must be
     * really exercised, not only assumed.  A real Xing TOC is accurate to ~1 %,
     * so a seek can start in the middle of a VBR frame; minimp3 then answers a
     * run of zero-sample frames (the bit reservoir of the previous frame is
     * missing) and playback used to die with `error_stage=decode`.  The
     * committed fixture is patched at `toc[50] = 150`, which maps to byte 3062
     * (44 + 150 * 5152 / 256) and was measured to yield four consecutive
     * zero-sample frames.  The runtime probe below re-checks that property, so a
     * regenerated fixture cannot make this block vacuous, and the seek targets
     * 50 % of the published total because `500 ms` of this 1044 ms fixture
     * resolves to TOC entry 47 and would never touch the patched entry. */
    {
        std::vector<char> patched;
        CHECK(read_fixture_bytes(patched));
        const std::size_t audio = fixture_audio_offset(patched);
        const std::size_t toc = fixture_toc_offset(patched);
        CHECK(audio == 44);
        CHECK(toc != 0);
        if (toc != 0) {
            patched[toc + 50] = static_cast<char>(150);
            const std::size_t audio_bytes = patched.size() - audio;
            const std::size_t seek_byte = audio + std::size_t(150) * audio_bytes / 256;
            CHECK(seek_byte == 3062);
            CHECK(zero_sample_prefix(patched, seek_byte) >= 2);
            CHECK(write_fixture_bytes(patched, fixture_path));

            CHECK(subject.start(&storage, access));
            CHECK(subject.set_queue(queue, 1, 0));
            subject.set_repeat(cyberdeck_music::repeat_mode::off);
            gate_playback_writes();
            CHECK(subject.play("/stream.mp3"));
            CHECK(wait_until([] {
                std::lock_guard<std::mutex> lock(audio_mutex);
                return write_waiting;
            }, std::chrono::seconds(2)));
            cyberdeck_music::snapshot before_patched{};
            CHECK(subject.status(before_patched));
            CHECK(before_patched.state == cyberdeck_music::playback_state::playing);
            CHECK(before_patched.current.duration_ms > 0);
            const std::uint32_t target = before_patched.current.duration_ms / 2 +
                                         before_patched.current.duration_ms / 200;
            CHECK(target * 100 / before_patched.current.duration_ms == 50);
            unsigned writes_before_patched = 0;
            {
                std::lock_guard<std::mutex> lock(audio_mutex);
                writes_before_patched = write_gate_count;
            }
            CHECK(subject.seek(target));
            {
                std::lock_guard<std::mutex> lock(audio_mutex);
                release_write = true;
            }
            audio_changed.notify_all();
            CHECK(wait_until([&] {
                std::lock_guard<std::mutex> lock(audio_mutex);
                return write_gate_count > writes_before_patched;
            }, std::chrono::seconds(2)));
            cyberdeck_music::snapshot after_patched{};
            CHECK(subject.status(after_patched));
            CHECK(after_patched.state == cyberdeck_music::playback_state::playing);
            CHECK(after_patched.failure_stage == cyberdeck_music::error_stage::none);
            CHECK(std::abs(static_cast<long>(after_patched.position_ms) -
                           static_cast<long>(target)) <= 1000L);
            release_each_write();
            CHECK(wait_for_eof());
            CHECK(stop_releases_once());
        }
    }

    /* TEST-MUSIC-DUR-10: two seeks on the same track.  Each seek restarts the
     * sample accumulator at the resolved position, so the elapsed time never
     * rewinds below the target, never double-counts the audio played before the
     * seek and still matches the total at EOF. */
    /* Releases gated writes one frame at a time until the published position
     * reaches `target` (the accumulator restarts at the seek point, so the first
     * frame decoded after the seek publishes `target + one frame`).  The write
     * gate publishes only after the released frame, so a single release is not
     * enough to observe the seek. */
    const auto seek_and_observe = [&](std::uint32_t target, std::uint32_t window,
                                      cyberdeck_music::snapshot &out) {
        if (!subject.seek(target))
            return false;
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            unsigned writes_before = 0;
            {
                std::lock_guard<std::mutex> lock(audio_mutex);
                writes_before = write_gate_count;
            }
            {
                std::lock_guard<std::mutex> lock(audio_mutex);
                release_write = true;
            }
            audio_changed.notify_all();
            if (!wait_until([&] {
                    std::lock_guard<std::mutex> lock(audio_mutex);
                    return write_gate_count > writes_before;
                }, std::chrono::seconds(2)))
                return false;
            if (!subject.status(out))
                return false;
            const long long delta =
                static_cast<long long>(out.position_ms) - static_cast<long long>(target);
            if (delta >= 0 && delta <= static_cast<long long>(window))
                return true;
        }
        return false;
    };
    CHECK(copy_fixture_to(fixture_path));
    CHECK(subject.start(&storage, access));
    CHECK(subject.set_queue(queue, 1, 0));
    subject.set_repeat(cyberdeck_music::repeat_mode::off);
    gate_playback_writes();
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_until([] {
        std::lock_guard<std::mutex> lock(audio_mutex);
        return write_waiting;
    }, std::chrono::seconds(2)));
    cyberdeck_music::snapshot first_seek{};
    CHECK(seek_and_observe(300, 200, first_seek));
    CHECK(first_seek.state == cyberdeck_music::playback_state::playing);
    CHECK(first_seek.failure_stage == cyberdeck_music::error_stage::none);
    CHECK(first_seek.position_ms >= 300 && first_seek.position_ms <= 500);
    cyberdeck_music::snapshot second_seek{};
    CHECK(seek_and_observe(700, 200, second_seek));
    CHECK(second_seek.state == cyberdeck_music::playback_state::playing);
    CHECK(second_seek.failure_stage == cyberdeck_music::error_stage::none);
    CHECK(second_seek.position_ms >= 700 && second_seek.position_ms <= 900);
    CHECK(second_seek.position_ms > first_seek.position_ms);
    CHECK(second_seek.current.duration_ms == first_seek.current.duration_ms);
    release_each_write();
    CHECK(wait_for_eof());
    {
        const long long drift = eof_drift(second_seek.current.duration_ms);
        CHECK(drift >= -1000 && drift <= 1000);
    }
    CHECK(stop_releases_once());

    /* TEST-MUSIC-DUR-11: seeking to the first millisecond and to the reported
     * total both stay inside the payload.  The rewind publishes a position near
     * zero, and the seek to the end reaches EOF without a decode/seek error. */
    CHECK(copy_fixture_to(fixture_path));
    CHECK(subject.start(&storage, access));
    CHECK(subject.set_queue(queue, 1, 0));
    subject.set_repeat(cyberdeck_music::repeat_mode::off);
    gate_playback_writes();
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_until([] {
        std::lock_guard<std::mutex> lock(audio_mutex);
        return write_waiting;
    }, std::chrono::seconds(2)));
    cyberdeck_music::snapshot before_rewind{};
    CHECK(seek_and_observe(600, 200, before_rewind));
    CHECK(before_rewind.position_ms >= 600 && before_rewind.position_ms <= 800);
    cyberdeck_music::snapshot rewound{};
    CHECK(seek_and_observe(0, 200, rewound));
    CHECK(rewound.state == cyberdeck_music::playback_state::playing);
    CHECK(rewound.failure_stage == cyberdeck_music::error_stage::none);
    CHECK(rewound.position_ms <= 200);
    CHECK(rewound.position_ms < before_rewind.position_ms);
    CHECK(rewound.current.duration_ms == before_rewind.current.duration_ms);
    cyberdeck_music::snapshot at_end{};
    /* Seeking to the reported total starts at the last TOC entry (99 %), so the
     * track finishes almost immediately: the requirement is a clean stop near
     * the end, not a continuing playback. */
    CHECK(subject.seek(rewound.current.duration_ms));
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        release_write = true; /* let the worker reach the top of the loop */
    }
    audio_changed.notify_all();
    CHECK(wait_until([&] {
        cyberdeck_music::snapshot state{};
        return subject.status(state) && state.state == cyberdeck_music::playback_state::stopped &&
               state.position_ms > 0;
    }, std::chrono::seconds(5)));
    CHECK(subject.status(at_end));
    CHECK(at_end.failure_stage == cyberdeck_music::error_stage::none);
    CHECK(at_end.position_ms + 200 >= rewound.current.duration_ms);
    release_each_write();
    CHECK(wait_for_eof());
    {
        cyberdeck_music::snapshot ended{};
        CHECK(subject.status(ended));
        CHECK(ended.failure_stage == cyberdeck_music::error_stage::none);
        const long long drift = eof_drift(rewound.current.duration_ms);
        CHECK(drift >= -1000 && drift <= 1000);
    }
    CHECK(stop_releases_once());

    /* TEST-MUSIC-DUR-12 / AC-MUSIC-16 negative: the empty-frame tolerance is
     * bounded, so it cannot hide a corrupt stream.  Headers are valid and the
     * payloads are not, so minimp3 answers zero samples for every frame; past
     * `k_max_empty_frames` consecutive frames the player must still fail with
     * `error_stage=decode` instead of "playing" silence forever. */
    CHECK(write_zero_sample_mp3(fixture_path));
    CHECK(subject.start(&storage, access));
    CHECK(subject.set_queue(queue, 1, 0));
    subject.set_repeat(cyberdeck_music::repeat_mode::off);
    CHECK(subject.play("/stream.mp3"));
    CHECK(wait_until([&] {
        cyberdeck_music::snapshot state{};
        return subject.status(state) && state.state == cyberdeck_music::playback_state::error &&
               state.failure_stage == cyberdeck_music::error_stage::decode;
    }, std::chrono::seconds(5)));
    {
        cyberdeck_music::snapshot corrupt{};
        CHECK(subject.status(corrupt));
        CHECK(corrupt.state == cyberdeck_music::playback_state::error);
        CHECK(corrupt.failure_stage == cyberdeck_music::error_stage::decode);
    }
    CHECK(stop_releases_once());

    /* TEST-MUSIC-ESC-08: reopening the player after a real playback session
     * must reset the snapshot; only the configured volume survives. */
    CHECK(write_id3_tagged_mp3(fixture_path, "Session title", "Session artist"));
    CHECK(subject.start(&storage, access));
    CHECK(begin_gated_playback("/stream.mp3"));
    {
        cyberdeck_music::snapshot playing{};
        CHECK(subject.status(playing));
        CHECK(playing.state == cyberdeck_music::playback_state::playing);
        CHECK(std::string_view(playing.current.title.data()) == "Session title");
        CHECK(std::string_view(playing.current.artist.data()) == "Session artist");
    }
    release_playback();
    CHECK(wait_until([&] {
        cyberdeck_music::snapshot state{};
        return subject.status(state) && state.position_ms > 0;
    }, std::chrono::seconds(2)));
    CHECK(subject.set_volume(31));
    cyberdeck_music::snapshot before_stop{};
    CHECK(subject.status(before_stop));
    CHECK(before_stop.position_ms > 0);
    CHECK(std::string_view(before_stop.current.title.data()) == "Session title");
    CHECK(stop_releases_once());
    CHECK(subject.start(&storage, access));
    {
        cyberdeck_music::snapshot reopened{};
        CHECK(subject.status(reopened));
        CHECK(reopened.state == cyberdeck_music::playback_state::stopped);
        CHECK(reopened.failure_stage == cyberdeck_music::error_stage::none);
        CHECK(std::string_view(reopened.current.title.data()).empty());
        CHECK(std::string_view(reopened.current.artist.data()).empty());
        CHECK(std::string_view(reopened.current.path.data()).empty());
        CHECK(reopened.current.size_bytes == 0);
        CHECK(reopened.position_ms == 0);
        CHECK(reopened.volume == 31);
    }
    /* TEST-MUSIC-ESC-08: a session that played and then closed must not come
     * back as playing when the next start fails; the stale snapshot has to be
     * cleared before any allocation can bail out. */
    CHECK(begin_gated_playback("/stream.mp3"));
    release_playback();
    CHECK(stop_releases_once());
    music_test_fail_caps_queue_create = true;
    CHECK(!subject.start(&storage, access));
    music_test_fail_caps_queue_create = false;
    {
        cyberdeck_music::snapshot failed_start{};
        CHECK(subject.status(failed_start));
        CHECK(failed_start.state == cyberdeck_music::playback_state::stopped);
        CHECK(std::string_view(failed_start.current.path.data()).empty());
        CHECK(failed_start.position_ms == 0);
        CHECK(failed_start.volume == 31);
    }
    CHECK(subject.stop(0));
    CHECK(!subject.running());
    CHECK(wait_until([] { return music_test_delete_without_completion_count() == 0; }, std::chrono::seconds(1)));
    CHECK(music_test_entry_returned_without_delete_count() == 0);
    CHECK(music_test_invalid_task_delete_count == 0);
    CHECK(runtime.stop_all());
    CHECK(::unlink(path) == 0);
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
