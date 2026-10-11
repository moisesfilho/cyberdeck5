#include "event_log_shim/event_log_shim.h"
#include "platform/logging/event_log.h"
#include "esp_log.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <limits.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
std::mutex audit_mutex;
std::condition_variable audit_cv;
std::vector<std::string> audit;
std::mutex fsync_mutex;
std::condition_variable fsync_cv;
bool block_fsync = false, fsync_entered = false, release_fsync = false;
bool fail_next_binary_write = false, fail_next_binary_fsync = false;
unsigned durable_release_count = 0;
std::mutex projection_read_mutex;
std::condition_variable projection_read_cv;
bool block_projection_read = false, projection_read_entered = false, release_projection_read = false;
std::mutex serial_mutex;
std::string serial_output;
} // namespace

namespace {
int capture_serial(const char *format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    const int required = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (required < 0) return required;
    std::string text(static_cast<size_t>(required), '\0');
    va_copy(copy, args);
    std::vsnprintf(text.data(), text.size() + 1, format, copy);
    va_end(copy);
    std::lock_guard<std::mutex> lock(serial_mutex);
    serial_output += text;
    return required;
}
bool is_projection_file(FILE *file) {
    char link[64]{}, path[PATH_MAX + 1]{}, expected[PATH_MAX + 1]{};
    const char *root = std::getenv("EVENT_LOG_TEST_ROOT");
    if (!root || !realpath(root, expected)) return false;
    std::strcat(expected, "/logs/events.txt");
    std::snprintf(link, sizeof(link), "/proc/self/fd/%d", fileno(file));
    const auto count = readlink(link, path, PATH_MAX);
    if (count <= 0) return false;
    path[count] = '\0';
    char canonical[PATH_MAX + 1]{};
    return realpath(path, canonical) && std::strcmp(canonical, expected) == 0;
}
}

extern "C" size_t __real_fread(void *, size_t, size_t, FILE *);
extern "C" size_t __wrap_fread(void *buffer, size_t size, size_t count, FILE *file) {
    if (is_projection_file(file)) {
        std::unique_lock<std::mutex> lock(projection_read_mutex);
        if (block_projection_read) {
            projection_read_entered = true;
            projection_read_cv.notify_all();
            if (!projection_read_cv.wait_for(lock, std::chrono::seconds(5), [] { return release_projection_read; }))
                std::abort();
        }
    }
    return __real_fread(buffer, size, count, file);
}

extern "C" void event_log_test_durable_request_released() {
    {
        std::lock_guard<std::mutex> lock(audit_mutex);
        ++durable_release_count;
    }
    audit_cv.notify_all();
}

extern "C" int __real_fsync(int);
static bool is_binary_event_file(int fd) {
    char descriptor_link[64]{};
    char descriptor_path[PATH_MAX + 1]{};
    char expected_path[PATH_MAX + 1]{};
    const char *root = std::getenv("EVENT_LOG_TEST_ROOT");
    if (!root || !realpath(root, expected_path))
        return false;
    const auto root_length = std::strlen(expected_path);
    if (root_length + sizeof("/logs/events.log") > sizeof(expected_path))
        return false;
    std::strcat(expected_path, "/logs/events.log");
    std::snprintf(descriptor_link, sizeof(descriptor_link), "/proc/self/fd/%d", fd);
    const auto path_length = readlink(descriptor_link, descriptor_path, PATH_MAX);
    if (path_length <= 0)
        return false;
    descriptor_path[path_length] = '\0';
    char canonical_descriptor[PATH_MAX + 1]{};
    return realpath(descriptor_path, canonical_descriptor) && std::strcmp(canonical_descriptor, expected_path) == 0;
}
extern "C" int __wrap_fsync(int fd) {
    const bool is_binary = is_binary_event_file(fd);
    {
        std::lock_guard<std::mutex> l(audit_mutex);
        audit.emplace_back(is_binary ? "binary-fsync" : "projection-fsync");
        audit_cv.notify_all();
    }
    {
        std::unique_lock<std::mutex> l(fsync_mutex);
        if (block_fsync && is_binary) {
            fsync_entered = true;
            fsync_cv.notify_all();
            if (!fsync_cv.wait_for(l, std::chrono::seconds(5), [] { return release_fsync; }))
                std::abort(); // A blocked fsync is a harness failure, never an injected ESP_FAIL.
        }
    }
    if (is_binary) {
        std::lock_guard<std::mutex> l(audit_mutex);
        if (fail_next_binary_fsync) {
            fail_next_binary_fsync = false;
            errno = EIO;
            return -1;
        }
    }
    int result = __real_fsync(fd);
    fsync_cv.notify_all();
    return result;
}
extern "C" size_t __real_fwrite(const void *, size_t, size_t, FILE *);
extern "C" size_t __wrap_fwrite(const void *data, size_t size, size_t count, FILE *file) {
    bool fail = false;
    {
        std::lock_guard<std::mutex> lock(audit_mutex);
        audit.emplace_back("write");
        if (size == 256 && count == 1 && fail_next_binary_write) {
            fail_next_binary_write = false;
            fail = true;
        }
    }
    if (fail) {
        errno = EIO;
        return 0;
    }
    const auto written = __real_fwrite(data, size, count, file);
    if (size == 256 && count == 1 && written == count) {
        std::lock_guard<std::mutex> lock(audit_mutex);
        audit.emplace_back("binary-write");
        audit_cv.notify_all();
    }
    return written;
}
extern "C" void __gcov_dump(void);

int main(int argc, char **argv) {
    char unique_root[] = "/tmp/cyberdeck-event-log-host-XXXXXX";
    char *root = std::getenv("EVENT_LOG_TEST_ROOT");
    if (!root)
        root = mkdtemp(unique_root);
    assert(root != nullptr);
    assert(setenv("EVENT_LOG_TEST_ROOT", root, 1) == 0);
    const auto logs_path = std::string(root) + "/logs";
    assert(mkdir(logs_path.c_str(), 0700) == 0);
    assert(esp_log_set_vprintf(capture_serial) == nullptr);
    const std::string scenario = argc > 1 ? argv[1] : "default";
    if (scenario == "preinit") {
        assert(event_log_quiesce(1) == ESP_ERR_INVALID_STATE);
        std::puts("PASS: pre-init quiesce rejected");
        std::fflush(stdout);
        _exit(0);
    }
    assert(scenario == "default" || scenario == "quiesce-fence");
    assert(event_log_init() == ESP_OK);
    if (scenario == "quiesce-fence") {
        event_log_write('I', "fence", "before-fence");
        const auto normal_path = std::string(root) + "/logs/events.log";
        {
            std::unique_lock<std::mutex> lock(audit_mutex);
            assert(audit_cv.wait_for(lock, std::chrono::seconds(5), [] {
                return std::find(audit.begin(), audit.end(), "binary-write") != audit.end();
            }));
        }
        const auto projection_path = std::string(root) + "/logs/events.txt";
        FILE *projection = std::fopen(projection_path.c_str(), "wb");
        assert(projection != nullptr && std::fputs("snapshot-line\n", projection) >= 0);
        assert(std::fclose(projection) == 0);
        {
            std::lock_guard<std::mutex> lock(projection_read_mutex);
            block_projection_read = true;
            projection_read_entered = false;
            release_projection_read = false;
        }
        esp_err_t reader_result = ESP_OK;
        uint8_t snapshot_buffer[32]{};
        size_t snapshot_count = 0;
        std::thread reader([&] {
            reader_result = event_log_text_read(0, snapshot_buffer, sizeof(snapshot_buffer), &snapshot_count);
        });
        {
            std::unique_lock<std::mutex> lock(projection_read_mutex);
            assert(projection_read_cv.wait_for(lock, std::chrono::seconds(5), [] { return projection_read_entered; }));
        }
        assert(event_log_quiesce(0) == ESP_ERR_TIMEOUT);
        {
            std::lock_guard<std::mutex> lock(projection_read_mutex);
            release_projection_read = true;
            block_projection_read = false;
        }
        projection_read_cv.notify_all();
        reader.join();
        assert(reader_result == ESP_OK && snapshot_count >= std::strlen("snapshot-line\n"));
        assert(std::memcmp(snapshot_buffer, "snapshot-line\n", std::strlen("snapshot-line\n")) == 0);
        assert(event_log_quiesce(100) == ESP_ERR_TIMEOUT);
        event_log_write('I', "fence", "after-fence-normal");
        assert(event_log_write_durable('W', "fence", "after-fence-durable") == ESP_ERR_INVALID_STATE);
        auto hooked_vprintf = event_log_shim_current_vprintf();
        assert(hooked_vprintf != nullptr);
        struct stat before_vprintf{};
        assert(stat(normal_path.c_str(), &before_vprintf) == 0);
        auto invoke_log = [&](const char *format, ...) {
            va_list args;
            va_start(args, format);
            const int result = hooked_vprintf(format, args);
            va_end(args);
            return result;
        };
        assert(invoke_log("fenced-serial-%d", 37) == 16);
        {
            std::lock_guard<std::mutex> lock(serial_mutex);
            assert(serial_output.find("fenced-serial-37") != std::string::npos);
        }
        struct stat after_vprintf{};
        assert(stat(normal_path.c_str(), &after_vprintf) == 0 && after_vprintf.st_size == before_vprintf.st_size);
        assert(event_log_text_size() == 0);
        uint8_t text_buffer[32]{};
        size_t out_read = 99;
        assert(event_log_text_read(0, text_buffer, sizeof(text_buffer), &out_read) == ESP_ERR_INVALID_STATE);
        std::string persisted;
        char chunk[512];
        FILE *file = std::fopen(normal_path.c_str(), "rb");
        assert(file != nullptr);
        while (const auto count = std::fread(chunk, 1, sizeof(chunk), file)) persisted.append(chunk, count);
        assert(std::fclose(file) == 0);
        assert(persisted.find("before-fence") != std::string::npos);
        assert(persisted.find("after-fence-normal") == std::string::npos);
        assert(persisted.find("after-fence-durable") == std::string::npos);
        std::puts("PASS: quiesce remains fenced and rejects writers/readers");
        std::fflush(stdout);
        __gcov_dump();
        _exit(0);
    }
    const auto queue_descriptors = event_log_shim::snapshot_queues();
    std::vector<event_log_shim::queue_snapshot> durable_descriptors;
    for (const auto &descriptor : queue_descriptors) {
        if (descriptor.capacity == 4 && descriptor.item_size == sizeof(void *))
            durable_descriptors.push_back(descriptor);
    }
    assert(durable_descriptors.size() == 1);
    const auto durable_queue = durable_descriptors.front().handle;
    {
        std::lock_guard<std::mutex> lock(fsync_mutex);
        block_fsync = true;
        fsync_entered = false;
        release_fsync = false;
    }
    event_log_write('I', "normal", "host-normal");
    const auto normal_path = std::string(root) + "/logs/events.log";
    {
        std::unique_lock<std::mutex> lock(audit_mutex);
        assert(audit_cv.wait_for(lock, std::chrono::seconds(5), [] {
            return std::find(audit.begin(), audit.end(), "binary-write") != audit.end() &&
                   std::find(audit.begin(), audit.end(), "binary-fsync") != audit.end();
        }));
    }
    {
        std::unique_lock<std::mutex> lock(fsync_mutex);
        assert(fsync_cv.wait_for(lock, std::chrono::seconds(5), [] { return fsync_entered; }));
    }
    // The projection wrapper also takes fsync_mutex; never invoke it while holding the gate.
    {
        const auto text_path = std::string(root) + "/logs/events.txt";
        const int text_fd = open(text_path.c_str(), O_CREAT | O_WRONLY, 0600);
        assert(text_fd >= 0);
        assert(__wrap_fsync(text_fd) == __real_fsync(text_fd));
        assert(close(text_fd) == 0);
        {
            std::lock_guard<std::mutex> audit_lock(audit_mutex);
            assert(std::find(audit.begin(), audit.end(), "projection-fsync") != audit.end());
        }
    }
    {
        std::lock_guard<std::mutex> lock(fsync_mutex);
        release_fsync = true;
        block_fsync = false;
    }
    fsync_cv.notify_all();
    {
        std::lock_guard<std::mutex> l(audit_mutex);
        audit.clear();
    }
    assert(event_log_write_durable('W', "durable", "host-durable") == ESP_OK);
    {
        std::unique_lock<std::mutex> lock(audit_mutex);
        assert(audit_cv.wait_for(lock, std::chrono::seconds(5), [] { return durable_release_count == 1; }));
    }
    {
        std::lock_guard<std::mutex> l(audit_mutex);
        audit.emplace_back("ACK");
        auto write = std::find(audit.begin(), audit.end(), "write");
        auto sync = std::find(audit.begin(), audit.end(), "binary-fsync");
        auto ack = std::find(audit.begin(), audit.end(), "ACK");
        assert(write != audit.end() && sync != audit.end() && ack != audit.end() && write < sync && sync < ack);
    }
    // An accepted request whose caller times out remains owned by the worker
    // until it releases the request after the blocked binary fsync completes.
    event_log_shim::reset_ticks();
    const auto releases_before_timeout = durable_release_count;
    const std::string timeout_tag = "timeout-lifetime-unique-tag";
    const std::string timeout_message = "timeout-lifetime-unique-message";
    {
        std::lock_guard<std::mutex> lock(fsync_mutex);
        block_fsync = true;
        fsync_entered = false;
        release_fsync = false;
    }
    esp_err_t timeout_result = ESP_OK;
    std::mutex caller_mutex;
    std::condition_variable caller_cv;
    bool caller_done = false;
    std::thread caller([&] {
        const auto result = event_log_write_durable('W', timeout_tag.c_str(), timeout_message.c_str());
        {
            std::lock_guard<std::mutex> lock(caller_mutex);
            timeout_result = result;
            caller_done = true;
        }
        caller_cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> lock(fsync_mutex);
        assert(fsync_cv.wait_for(lock, std::chrono::seconds(5), [] { return fsync_entered; }));
    }
    const auto caller_id = caller.get_id();
    event_log_shim::wait_for_timed_wait_registration(caller_id);
    {
        std::lock_guard<std::mutex> lock(caller_mutex);
        assert(!caller_done);
    }
    event_log_shim::advance_ticks(pdMS_TO_TICKS(250));
    {
        std::unique_lock<std::mutex> lock(caller_mutex);
        assert(caller_cv.wait_for(lock, std::chrono::seconds(5), [&] { return caller_done; }));
        assert(timeout_result == ESP_ERR_TIMEOUT);
    }
    {
        std::lock_guard<std::mutex> lock(audit_mutex);
        assert(durable_release_count == releases_before_timeout);
    }
    {
        std::lock_guard<std::mutex> lock(fsync_mutex);
        release_fsync = true;
        block_fsync = false;
    }
    fsync_cv.notify_all();
    {
        std::unique_lock<std::mutex> lock(audit_mutex);
        assert(audit_cv.wait_for(lock, std::chrono::seconds(5),
                                 [&] { return durable_release_count == releases_before_timeout + 1; }));
    }
    caller.join();
    assert(timeout_result == ESP_ERR_TIMEOUT);

    // Hold the real worker in binary fsync while saturating the production
    // durable queue. Each caller's semaphore wait is observed before advancing
    // fake time, so queue depth and ACK timeout are race-free observations.
    event_log_shim::reset_ticks();
    const auto releases_before_backpressure = durable_release_count;
    std::string persisted_bytes;
    char read_buffer[512];
    {
        std::lock_guard<std::mutex> lock(fsync_mutex);
        block_fsync = true;
        fsync_entered = false;
        release_fsync = false;
    }
    constexpr size_t accepted_count = 5;
    std::vector<std::string> accepted_tokens;
    std::vector<esp_err_t> accepted_results(accepted_count, ESP_FAIL);
    std::vector<unsigned char> accepted_done(accepted_count, 0);
    std::mutex accepted_mutex;
    std::condition_variable accepted_cv;
    std::vector<std::thread> accepted_callers;
    accepted_tokens.reserve(accepted_count);
    accepted_callers.reserve(accepted_count);
    accepted_callers.emplace_back([&] {
        accepted_results[0] = event_log_write_durable('W', "backpressure", "backpressure-accepted-0");
        {
            std::lock_guard<std::mutex> lock(accepted_mutex);
            accepted_done[0] = true;
        }
        accepted_cv.notify_all();
    });
    accepted_tokens.emplace_back("backpressure-accepted-0");
    {
        std::unique_lock<std::mutex> lock(fsync_mutex);
        assert(fsync_cv.wait_for(lock, std::chrono::seconds(5), [] { return fsync_entered; }));
    }
    event_log_shim::wait_for_timed_wait_registration(accepted_callers[0].get_id());
    for (size_t index = 1; index < accepted_count; ++index) {
        accepted_tokens.emplace_back("backpressure-accepted-" + std::to_string(index));
        accepted_callers.emplace_back([&, index] {
            accepted_results[index] = event_log_write_durable('W', "backpressure", accepted_tokens[index].c_str());
            {
                std::lock_guard<std::mutex> lock(accepted_mutex);
                accepted_done[index] = true;
            }
            accepted_cv.notify_all();
        });
        event_log_shim::wait_for_queue_depth(durable_queue, index);
        event_log_shim::wait_for_timed_wait_registration(accepted_callers.back().get_id());
    }
    assert(event_log_shim::snapshot_queue(durable_queue).depth == 4);
    const std::string overflow_token = "backpressure-overflow-unique";
    esp_err_t overflow_result = ESP_FAIL;
    bool overflow_done = false;
    std::mutex overflow_mutex;
    std::condition_variable overflow_cv;
    const auto overflow_generation = event_log_shim::timed_wait_registration_generation();
    std::thread overflow_caller([&] {
        const auto result = event_log_write_durable('E', "backpressure", overflow_token.c_str());
        {
            std::lock_guard<std::mutex> lock(overflow_mutex);
            overflow_result = result;
            overflow_done = true;
        }
        overflow_cv.notify_all();
    });
    const auto overflow_id = overflow_caller.get_id();
    auto observed_generation = overflow_generation;
    for (unsigned retry = 0; retry < 3; ++retry) {
        {
            std::lock_guard<std::mutex> lock(overflow_mutex);
            if (overflow_done)
                break;
        }
        observed_generation = event_log_shim::wait_for_next_timed_wait_registration(overflow_id, observed_generation);
        event_log_shim::advance_ticks(pdMS_TO_TICKS(20));
    }
    {
        std::unique_lock<std::mutex> lock(overflow_mutex);
        assert(overflow_cv.wait_for(lock, std::chrono::seconds(5), [&] { return overflow_done; }));
        assert(overflow_result == ESP_ERR_NO_MEM);
    }
    assert(event_log_shim::snapshot_queue(durable_queue).depth == 4);
    event_log_shim::advance_ticks(pdMS_TO_TICKS(250));
    for (size_t index = 0; index < accepted_count; ++index) {
        std::unique_lock<std::mutex> lock(accepted_mutex);
        assert(accepted_cv.wait_for(lock, std::chrono::seconds(5), [&, index] { return accepted_done[index]; }));
        assert(accepted_results[index] == ESP_ERR_TIMEOUT);
    }
    // Every accepted caller has recorded its timeout before joining.
    for (auto &caller_thread : accepted_callers)
        caller_thread.join();
    for (const auto result : accepted_results)
        assert(result == ESP_ERR_TIMEOUT);
    {
        std::lock_guard<std::mutex> lock(audit_mutex);
        assert(durable_release_count == releases_before_backpressure);
    }
    {
        std::lock_guard<std::mutex> lock(fsync_mutex);
        release_fsync = true;
        block_fsync = false;
    }
    fsync_cv.notify_all();
    {
        std::unique_lock<std::mutex> lock(audit_mutex);
        assert(audit_cv.wait_for(lock, std::chrono::seconds(5), [&] {
            return durable_release_count == releases_before_backpressure + accepted_count;
        }));
    }
    overflow_caller.join();
    assert(overflow_result == ESP_ERR_NO_MEM);
    FILE *backpressure_log = std::fopen(normal_path.c_str(), "rb");
    assert(backpressure_log != nullptr);
    persisted_bytes.clear();
    while (const auto bytes_read = std::fread(read_buffer, 1, sizeof(read_buffer), backpressure_log))
        persisted_bytes.append(read_buffer, bytes_read);
    assert(std::ferror(backpressure_log) == 0);
    assert(std::fclose(backpressure_log) == 0);
    const auto timeout_position = persisted_bytes.find(timeout_message);
    assert(timeout_position != std::string::npos);
    assert(persisted_bytes.find(timeout_message, timeout_position + timeout_message.size()) == std::string::npos);
    for (const auto &token : accepted_tokens) {
        const auto position = persisted_bytes.find(token);
        assert(position != std::string::npos);
        assert(persisted_bytes.find(token, position + token.size()) == std::string::npos);
    }
    assert(persisted_bytes.find(overflow_token) == std::string::npos);
    assert(access(normal_path.c_str(), F_OK) == 0);
    struct stat before_failed_write {};
    assert(stat(normal_path.c_str(), &before_failed_write) == 0);
    {
        std::lock_guard<std::mutex> l(audit_mutex);
        fail_next_binary_write = true;
    }
    assert(event_log_write_durable('E', "failure", "injected-write-failure") == ESP_FAIL);
    struct stat after_failed_write {};
    assert(stat(normal_path.c_str(), &after_failed_write) == 0 &&
           after_failed_write.st_size == before_failed_write.st_size);
    struct stat binary_info {};
    assert(stat(normal_path.c_str(), &binary_info) == 0);
    {
        std::lock_guard<std::mutex> l(audit_mutex);
        auto w = std::find(audit.begin(), audit.end(), "binary-fsync");
        assert(w != audit.end());
    }
    {
        std::lock_guard<std::mutex> l(audit_mutex);
        fail_next_binary_fsync = true;
        audit.clear();
    }
    assert(event_log_write_durable('E', "failure", "injected-fsync-failure") == ESP_FAIL);
    struct stat after_failed_fsync {};
    assert(stat(normal_path.c_str(), &after_failed_fsync) == 0 &&
           after_failed_fsync.st_size == binary_info.st_size + 256);
    {
        std::lock_guard<std::mutex> l(audit_mutex);
        assert(std::find(audit.begin(), audit.end(), "write") != audit.end());
        assert(std::find(audit.begin(), audit.end(), "binary-fsync") != audit.end());
        assert(std::find(audit.begin(), audit.end(), "ACK") == audit.end());
    }
    std::puts("PASS: real event_log TU init, durable ACK/failure and timeout lifetime/persistence");
    std::fflush(stdout);
    __gcov_dump();
    _exit(0); // Production exposes no worker shutdown; avoid host static teardown races.
}
