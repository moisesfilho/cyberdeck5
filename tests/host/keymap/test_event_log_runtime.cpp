#include "event_log_shim/event_log_shim.h"
#include "platform/logging/event_log.h"
#include "esp_log.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
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
bool fail_next_binary_close = false;
bool fail_next_projection_write = false, fail_next_projection_fsync = false;
bool fail_next_projection_close = false;
unsigned durable_release_count = 0;
std::mutex projection_read_mutex;
std::condition_variable projection_read_cv;
bool block_projection_read = false, projection_read_entered = false, release_projection_read = false;
std::mutex projection_write_mutex;
std::condition_variable projection_write_cv;
bool block_projection_write = false, projection_write_entered = false, release_projection_write = false;
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
static bool is_projection_event_fd(int fd) {
    char link[64]{}, path[PATH_MAX + 1]{}, expected[PATH_MAX + 1]{}, canonical[PATH_MAX + 1]{};
    const char *root = std::getenv("EVENT_LOG_TEST_ROOT");
    if (!root || !realpath(root, expected)) return false;
    std::strcat(expected, "/logs/events.txt");
    std::snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    const auto count = readlink(link, path, PATH_MAX);
    if (count <= 0) return false;
    path[count] = '\0';
    return realpath(path, canonical) && std::strcmp(canonical, expected) == 0;
}
extern "C" int __wrap_fsync(int fd) {
    const bool is_binary = is_binary_event_file(fd);
    if (is_binary) event_log_shim::record_audit_event("binary-fsync", reinterpret_cast<void *>(static_cast<intptr_t>(fd)));
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
            audit.emplace_back("binary-fsync-failed");
            audit_cv.notify_all();
            return -1;
        }
    }
    if (is_projection_event_fd(fd)) {
        std::lock_guard<std::mutex> lock(audit_mutex);
        if (fail_next_projection_fsync) {
            fail_next_projection_fsync = false;
            errno = EIO;
            audit.emplace_back("projection-fsync-failed");
            audit_cv.notify_all();
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
        const bool projection = is_projection_file(file);
        audit.emplace_back(projection ? "projection-write" : "write");
        if (projection && fail_next_projection_write) {
            fail_next_projection_write = false;
            errno = EIO;
            audit.emplace_back("projection-write-failed");
            audit_cv.notify_all();
            return 0;
        }
        if (size == 256 && count == 1 && fail_next_binary_write) {
            fail_next_binary_write = false;
            fail = true;
            audit.emplace_back("binary-write-failed");
            audit_cv.notify_all();
        }
    }
    if (is_projection_file(file)) {
        std::unique_lock<std::mutex> lock(projection_write_mutex);
        if (block_projection_write) {
            projection_write_entered = true;
            projection_write_cv.notify_all();
            if (!projection_write_cv.wait_for(lock, std::chrono::seconds(5), [] { return release_projection_write; }))
                std::abort();
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
extern "C" int __real_fclose(FILE *);
static std::string descriptor_path(FILE *file) {
    char link[64]{}, path[PATH_MAX + 1]{};
    std::snprintf(link, sizeof(link), "/proc/self/fd/%d", fileno(file));
    const auto count = readlink(link, path, PATH_MAX);
    if (count <= 0) return {};
    path[count] = '\0';
    return path;
}
extern "C" int __wrap_fclose(FILE *file) {
    if (is_projection_file(file)) {
        event_log_shim::record_file_event("text-close", file, descriptor_path(file).c_str());
        std::lock_guard<std::mutex> lock(audit_mutex);
        audit.emplace_back("projection-close");
        audit_cv.notify_all();
        if (fail_next_projection_close) {
            fail_next_projection_close = false;
            errno = EIO;
            (void)__real_fclose(file);
            audit.emplace_back("projection-close-failed");
            audit_cv.notify_all();
            return EOF;
        }
    } else if (is_binary_event_file(fileno(file))) {
        event_log_shim::record_file_event("binary-close", file, descriptor_path(file).c_str());
        std::lock_guard<std::mutex> lock(audit_mutex);
        audit.emplace_back("binary-close");
        audit_cv.notify_all();
        if (fail_next_binary_close) {
            fail_next_binary_close = false;
            errno = EIO;
            (void)__real_fclose(file);
            return EOF;
        }
    }
    return __real_fclose(file);
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
    if (scenario == "partial-init") {
        event_log_shim::fail_next_task_creation("event_log");
        assert(event_log_init() == ESP_ERR_NO_MEM);
        event_log_shim::wait_for_task_starts(1);
        const auto tasks = event_log_shim::task_start_history();
        assert(tasks.size() == 1 && tasks.front().name == "event_text");
        assert(esp_log_set_vprintf(capture_serial) == capture_serial);
        assert(event_log_init() == ESP_ERR_INVALID_STATE);
        assert(event_log_write_durable('I', "partial-init", "rejected") == ESP_ERR_INVALID_STATE);
        uint8_t buffer[16]{};
        size_t count = 99;
        assert(event_log_text_read(0, buffer, sizeof(buffer), &count) == ESP_ERR_INVALID_STATE);
        assert(count == 99);
        const auto queues = event_log_shim::snapshot_queues();
        assert(queues.size() == 3);
        event_log_shim::wait_for_timed_wait_registration(tasks.front().owner);
        assert(event_log_quiesce(0) == ESP_ERR_TIMEOUT);
        event_log_shim::advance_ticks(pdMS_TO_TICKS(50));
        assert(event_log_shim::wait_for_test_task(tasks.front().handle, std::chrono::seconds(5)));
        assert(test_task_self_delete_requested(tasks.front().handle));
        assert(event_log_shim::join_test_task(tasks.front().handle));
        assert(event_log_quiesce(0) == ESP_OK);
        assert(event_log_quiesce(0) == ESP_OK);
        assert(event_log_init() == ESP_ERR_INVALID_STATE);
        const auto preserved = event_log_shim::snapshot_queues();
        assert(preserved.size() == queues.size());
        for (size_t index = 0; index < queues.size(); ++index) {
            assert(preserved[index].handle == queues[index].handle);
            assert(preserved[index].depth == 0);
        }
        assert(access((logs_path + "/events.log").c_str(), F_OK) != 0 && errno == ENOENT);
        assert(access((logs_path + "/events.txt").c_str(), F_OK) != 0 && errno == ENOENT);
        event_log_shim::cleanup_test_tasks();
        std::puts("PASS: partial init preserves resources, restores serial hook and quiesces sole text worker");
        std::fflush(stdout);
        __gcov_dump();
        _exit(0);
    }
    if (scenario == "init-text-failure") {
        event_log_shim::fail_next_task_creation("event_text");
        assert(event_log_init() == ESP_ERR_NO_MEM);
        assert(event_log_shim::task_start_history().empty());
        assert(event_log_quiesce(0) == ESP_ERR_INVALID_STATE);
        assert(esp_log_set_vprintf(capture_serial) == capture_serial);
        assert(event_log_init() == ESP_OK);
        event_log_shim::wait_for_task_starts(2);
        const auto tasks = event_log_shim::task_start_history();
        const auto binary = std::find_if(tasks.begin(), tasks.end(), [](const auto &task) { return task.name == "event_log"; });
        const auto text = std::find_if(tasks.begin(), tasks.end(), [](const auto &task) { return task.name == "event_text"; });
        assert(binary != tasks.end() && text != tasks.end());
        event_log_shim::wait_for_timed_wait_registration(text->owner);
        assert(event_log_quiesce(0) == ESP_ERR_TIMEOUT);
        assert(event_log_shim::wait_for_test_task(binary->handle, std::chrono::seconds(5)));
        event_log_shim::advance_ticks(pdMS_TO_TICKS(50));
        assert(event_log_shim::wait_for_test_task(text->handle, std::chrono::seconds(5)));
        assert(event_log_shim::join_test_task(binary->handle));
        assert(event_log_shim::join_test_task(text->handle));
        assert(event_log_quiesce(0) == ESP_OK);
        event_log_shim::cleanup_test_tasks();
        std::puts("PASS: text worker creation failure rolls back before workers and permits safe reinit/drain");
        std::fflush(stdout); __gcov_dump(); _exit(0);
    }
    assert(scenario == "default" || scenario == "quiesce-race" || scenario == "quiesce-fence" || scenario == "durable-timeout-quiesce" || scenario == "worker-completion" || scenario == "projection-fwrite" ||
           scenario == "projection-fsync" || scenario == "projection-fclose" || scenario == "sticky-close" ||
           scenario == "sticky-write" || scenario == "sticky-fsync" || scenario == "worker-timeout-retry");
    assert(event_log_init() == ESP_OK);
    if (scenario == "quiesce-race") {
        event_log_shim::wait_for_task_starts(2);
        const auto tasks = event_log_shim::task_start_history();
        const auto text_task = std::find_if(tasks.begin(), tasks.end(), [](const auto &task) {
            return task.name == "event_text";
        });
        assert(text_task != tasks.end());
        { std::lock_guard<std::mutex> lock(fsync_mutex); block_fsync = true; fsync_entered = false; release_fsync = false; }
        const std::string token = "quiesce-two-callers-held-fsync";
        event_log_write('I', "quiesce-race", token.c_str());
        { std::unique_lock<std::mutex> lock(fsync_mutex); assert(fsync_cv.wait_for(lock, std::chrono::seconds(5), [] { return fsync_entered; })); }
        event_log_shim::reset_ticks();
        struct caller_result { esp_err_t result = ESP_FAIL; bool done = false; std::mutex mutex; std::condition_variable cv; } a, b;
        std::thread caller_a([&] { const auto result = event_log_quiesce(20); { std::lock_guard<std::mutex> lock(a.mutex); a.result = result; a.done = true; } a.cv.notify_all(); });
        event_log_shim::wait_for_timed_wait_registration(caller_a.get_id());
        std::thread caller_b([&] { const auto result = event_log_quiesce(200); { std::lock_guard<std::mutex> lock(b.mutex); b.result = result; b.done = true; } b.cv.notify_all(); });
        event_log_shim::wait_for_timed_wait_registration(caller_b.get_id());
        { std::lock_guard<std::mutex> lock(a.mutex); assert(!a.done); }
        { std::lock_guard<std::mutex> lock(b.mutex); assert(!b.done); }
        event_log_shim::advance_ticks(pdMS_TO_TICKS(30));
        { std::unique_lock<std::mutex> lock(a.mutex); assert(a.cv.wait_for(lock, std::chrono::seconds(5), [&] { return a.done; })); assert(a.result == ESP_ERR_TIMEOUT); }
        { std::lock_guard<std::mutex> lock(b.mutex); assert(!b.done); }
        { std::lock_guard<std::mutex> lock(audit_mutex); assert(std::find(audit.begin(), audit.end(), "binary-close") == audit.end()); }
        event_log_shim::wait_for_timed_wait_registration(text_task->owner);
        const auto generation = event_log_shim::timed_wait_registration_generation();
        event_log_shim::advance_ticks(pdMS_TO_TICKS(50));
        // Even with an empty queue and stop requested, the projection owner
        // must remain available until the blocked binary owner finishes.
        event_log_shim::wait_for_next_timed_wait_registration(text_task->owner, generation);
        assert(!event_log_shim::wait_for_test_task(text_task->handle, std::chrono::milliseconds(0)));
        { std::lock_guard<std::mutex> lock(fsync_mutex); release_fsync = true; block_fsync = false; }
        fsync_cv.notify_all();
        {
            std::unique_lock<std::mutex> lock(audit_mutex);
            assert(audit_cv.wait_for(lock, std::chrono::seconds(5), [&] {
                return std::find(audit.begin(), audit.end(), "projection-close") != audit.end() &&
                       std::find(audit.begin(), audit.end(), "binary-close") != audit.end();
            }));
        }
        event_log_shim::wait_for_timed_wait_registration(text_task->owner);
        event_log_shim::advance_ticks(pdMS_TO_TICKS(50));
        { std::unique_lock<std::mutex> lock(b.mutex); assert(b.cv.wait_for(lock, std::chrono::seconds(5), [&] { return b.done; })); assert(b.result == ESP_OK); }
        caller_a.join(); caller_b.join();
        { std::lock_guard<std::mutex> lock(audit_mutex); assert(std::count(audit.begin(), audit.end(), "binary-close") == 1); }
        FILE *log = std::fopen((std::string(root) + "/logs/events.log").c_str(), "rb"); assert(log != nullptr);
        std::string persisted; char bytes[512]; while (const auto n = std::fread(bytes, 1, sizeof(bytes), log)) persisted.append(bytes, n); assert(std::fclose(log) == 0);
        const auto position = persisted.find(token); assert(position != std::string::npos); assert(persisted.find(token, position + token.size()) == std::string::npos);
        FILE *text = std::fopen((logs_path + "/events.txt").c_str(), "rb");
        assert(text != nullptr);
        std::string projection;
        while (const auto n = std::fread(bytes, 1, sizeof(bytes), text)) projection.append(bytes, n);
        assert(std::fclose(text) == 0);
        const auto projected = projection.find(token);
        assert(projected != std::string::npos);
        assert(projection.find(token, projected + token.size()) == std::string::npos);
        std::puts("PASS: concurrent quiesce callers retain independent deadlines; short timeout, long drain succeeds"); std::fflush(stdout); __gcov_dump(); _exit(0);
    }
    if (scenario == "worker-completion") {
        event_log_shim::wait_for_task_starts(2);
        const auto started = event_log_shim::task_start_history();
        assert(std::count_if(started.begin(), started.end(), [](const auto &task) { return task.name == "event_log"; }) == 1);
        assert(std::count_if(started.begin(), started.end(), [](const auto &task) { return task.name == "event_text"; }) == 1);
        const auto text_task = std::find_if(started.begin(), started.end(), [](const auto &task) { return task.name == "event_text"; });
        const auto binary_task = std::find_if(started.begin(), started.end(), [](const auto &task) { return task.name == "event_log"; });
        assert(text_task != started.end() && binary_task != started.end());
        {
            std::lock_guard<std::mutex> lock(projection_write_mutex);
            block_projection_write = true;
            projection_write_entered = false;
            release_projection_write = false;
        }
        event_log_write('I', "worker-completion", "accepted-projection-record");
        {
            std::unique_lock<std::mutex> lock(projection_write_mutex);
            assert(projection_write_cv.wait_for(lock, std::chrono::seconds(5), [] { return projection_write_entered; }));
        }
        const auto queues = event_log_shim::snapshot_queues();
        assert(queues.size() == 3);
        for (unsigned i = 0; i < 4; ++i) {
            const auto token = "queued-projection-" + std::to_string(i);
            event_log_write('I', "worker-completion", token.c_str());
        }
        event_log_shim::wait_for_queue_depth(queues[1].handle, 4);
        std::atomic<bool> advance_clock{true};
        std::thread clock_advancer([&] {
            while (advance_clock.load(std::memory_order_relaxed)) {
                event_log_shim::advance_ticks(pdMS_TO_TICKS(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        esp_err_t result = ESP_FAIL;
        std::thread::id observer_id;
        std::mutex observer_mutex;
        std::condition_variable observer_cv;
        bool observer_started = false, observer_done = false;
        std::thread observer([&] {
            {
                std::lock_guard<std::mutex> lock(observer_mutex);
                observer_id = std::this_thread::get_id();
                observer_started = true;
            }
            observer_cv.notify_all();
            result = event_log_quiesce(4000);
            { std::lock_guard<std::mutex> lock(observer_mutex); observer_done = true; }
            observer_cv.notify_all();
        });
        {
            std::unique_lock<std::mutex> lock(observer_mutex);
            assert(observer_cv.wait_for(lock, std::chrono::seconds(5), [&] { return observer_started; }));
            assert(!observer_done);
        }
        {
            std::lock_guard<std::mutex> lock(observer_mutex);
            assert(!observer_done);
        }
        { std::lock_guard<std::mutex> lock(projection_write_mutex); release_projection_write = true; block_projection_write = false; }
        projection_write_cv.notify_all();
        observer.join();
        advance_clock.store(false, std::memory_order_relaxed);
        clock_advancer.join();
        assert(result == ESP_OK);
        const auto history = event_log_shim::audit_history();
        const auto creations = event_log_shim::semaphore_creation_history();
        const auto quiesce_semaphore = std::find_if(creations.begin(), creations.end(), [](const auto &creation) {
            return creation.ordinal == 3 && creation.binary;
        }); // event_log_init creates recent mutex, text mutex, then s_quiesce_changed.
        assert(quiesce_semaphore != creations.end());
        void *observed_handle = quiesce_semaphore->handle;
        assert(std::any_of(history.begin(), history.end(), [&](const auto &event) {
            return event.kind == "semaphore-take" && event.handle == observed_handle && event.owner == observer_id;
        }));
        assert(observed_handle != nullptr);
        const auto assert_worker_completion = [&](std::thread::id worker, const char *close_kind, const std::string &path) {
            const auto give = std::find_if(history.begin(), history.end(), [&](const auto &event) {
                return event.kind == "semaphore-give" && event.handle == observed_handle && event.owner == worker;
            });
            assert(give != history.end());
            const auto close = std::find_if(history.begin(), history.end(), [&](const auto &event) {
                return event.kind == close_kind && event.path == path && event.owner == worker && event.sequence < give->sequence;
            });
            assert(close != history.end());
            for (const auto &event : history) {
                if (event.owner != worker || event.sequence <= give->sequence) continue;
                assert(event.kind != "binary-close" && event.kind != "text-close" &&
                       event.kind != "queue-send" && event.kind != "queue-receive");
            }
        };
        assert_worker_completion(text_task->owner, "text-close", std::string(root) + "/logs/events.txt");
        assert_worker_completion(binary_task->owner, "binary-close", std::string(root) + "/logs/events.log");
        FILE *projection_file = std::fopen((logs_path + "/events.txt").c_str(), "rb");
        assert(projection_file != nullptr);
        std::string projection;
        char bytes[512];
        while (const auto n = std::fread(bytes, 1, sizeof(bytes), projection_file)) projection.append(bytes, n);
        assert(std::fclose(projection_file) == 0);
        for (unsigned i = 0; i < 4; ++i) {
            const auto token = "queued-projection-" + std::to_string(i);
            const auto position = projection.find(token);
            assert(position != std::string::npos);
            assert(projection.find(token, position + token.size()) == std::string::npos);
        }
        assert(event_log_shim::snapshot_queue(queues[1].handle).depth == 0);
        std::puts("PASS: five accepted projections drain before successful quiesce; worker closes precede gives");
        std::fflush(stdout);
        __gcov_dump();
        _exit(0);
    }
    if (scenario == "projection-fwrite" || scenario == "projection-fsync" || scenario == "projection-fclose") {
        {
            std::lock_guard<std::mutex> lock(audit_mutex);
            fail_next_projection_write = scenario == "projection-fwrite";
            fail_next_projection_fsync = scenario == "projection-fsync";
            fail_next_projection_close = scenario == "projection-fclose";
        }
        event_log_write('E', "projection", "projection-failure-quiesce-record");
        const auto expected_failure = scenario == "projection-fwrite" ? "projection-write-failed" :
            scenario == "projection-fsync" ? "projection-fsync-failed" : "projection-close-failed";
        {
            std::unique_lock<std::mutex> lock(audit_mutex);
            assert(audit_cv.wait_for(lock, std::chrono::seconds(5), [&] {
                return std::find(audit.begin(), audit.end(), expected_failure) != audit.end();
            }));
        }
        std::atomic<bool> advance_clock{true};
        std::thread clock_advancer([&] {
            while (advance_clock.load(std::memory_order_relaxed)) {
                event_log_shim::advance_ticks(pdMS_TO_TICKS(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        const auto result = event_log_quiesce(1000);
        advance_clock.store(false, std::memory_order_relaxed);
        clock_advancer.join();
        assert(result == ESP_OK);
        assert(event_log_quiesce(0) == ESP_OK);
        assert(event_log_init() == ESP_ERR_INVALID_STATE);
        const auto binary_path = std::string(root) + "/logs/events.log";
        FILE *binary = std::fopen(binary_path.c_str(), "rb");
        assert(binary != nullptr);
        std::string persisted;
        char chunk[512];
        while (const auto count = std::fread(chunk, 1, sizeof(chunk), binary)) persisted.append(chunk, count);
        assert(std::fclose(binary) == 0);
        const auto record_position = persisted.find("projection-failure-quiesce-record");
        assert(record_position != std::string::npos);
        assert(persisted.find("projection-failure-quiesce-record", record_position + 1) == std::string::npos);
        {
            std::lock_guard<std::mutex> lock(audit_mutex);
            assert(std::find(audit.begin(), audit.end(), "binary-fsync") != audit.end());
            assert(std::find(audit.begin(), audit.end(), "binary-close") != audit.end());
            assert(std::find(audit.begin(), audit.end(), expected_failure) != audit.end());
        }
        std::puts("PASS: text projection failure is best effort; durable binary drain/close succeeds exactly once");
        std::fflush(stdout);
        __gcov_dump();
        _exit(0);
    }
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
        std::atomic<bool> advance_clock{true};
        std::thread clock_advancer([&] {
            while (advance_clock.load(std::memory_order_relaxed)) {
                event_log_shim::advance_ticks(pdMS_TO_TICKS(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        const auto retry_result = event_log_quiesce(1000);
        advance_clock.store(false, std::memory_order_relaxed);
        clock_advancer.join();
        assert(retry_result == ESP_OK);
        assert(event_log_quiesce(0) == ESP_OK);
        assert(event_log_init() == ESP_ERR_INVALID_STATE);
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
        { std::lock_guard<std::mutex> lock(audit_mutex); assert(std::find(audit.begin(), audit.end(), "binary-close") != audit.end()); }
        std::puts("PASS: reader lease timeout/retry drains, closes owner, CLOSED is idempotent/no-reopen");
        std::fflush(stdout);
        __gcov_dump();
        _exit(0);
    }
    if (scenario == "durable-timeout-quiesce") {
        {
            std::lock_guard<std::mutex> lock(fsync_mutex);
            block_fsync = true;
            fsync_entered = false;
            release_fsync = false;
        }
        const auto releases_before = durable_release_count;
        esp_err_t caller_result = ESP_OK;
        std::mutex caller_mutex;
        std::condition_variable caller_cv;
        bool caller_done = false;
        const std::string token = "quiesce-timeout-race-unique";
        std::thread caller([&] {
            const auto result = event_log_write_durable('W', "quiesce-race", token.c_str());
            {
                std::lock_guard<std::mutex> lock(caller_mutex);
                caller_result = result;
                caller_done = true;
            }
            caller_cv.notify_all();
        });
        {
            std::unique_lock<std::mutex> lock(fsync_mutex);
            assert(fsync_cv.wait_for(lock, std::chrono::seconds(5), [] { return fsync_entered; }));
        }
        event_log_shim::wait_for_timed_wait_registration(caller.get_id());
        event_log_shim::advance_ticks(pdMS_TO_TICKS(250));
        {
            std::unique_lock<std::mutex> lock(caller_mutex);
            assert(caller_cv.wait_for(lock, std::chrono::seconds(5), [&] { return caller_done; }));
            assert(caller_result == ESP_ERR_TIMEOUT);
        }
        {
            std::lock_guard<std::mutex> lock(audit_mutex);
            assert(durable_release_count == releases_before);
        }

        event_log_shim::reset_ticks();
        std::atomic<bool> advance_quiesce_clock{true};
        std::thread quiesce_clock([&] {
            while (advance_quiesce_clock.load(std::memory_order_relaxed)) {
                event_log_shim::advance_ticks(pdMS_TO_TICKS(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        const auto premature_quiesce = event_log_quiesce(20);
        advance_quiesce_clock.store(false, std::memory_order_relaxed);
        quiesce_clock.join();
        assert(premature_quiesce == ESP_ERR_TIMEOUT);
        {
            std::lock_guard<std::mutex> lock(audit_mutex);
            assert(durable_release_count == releases_before);
            assert(std::find(audit.begin(), audit.end(), "binary-close") == audit.end());
        }

        {
            std::lock_guard<std::mutex> lock(fsync_mutex);
            release_fsync = true;
            block_fsync = false;
        }
        fsync_cv.notify_all();
        caller.join();
        assert(caller_result == ESP_ERR_TIMEOUT);
        {
            std::unique_lock<std::mutex> lock(audit_mutex);
            assert(audit_cv.wait_for(lock, std::chrono::seconds(5), [&] {
                return durable_release_count == releases_before + 1;
            }));
        }
        std::atomic<bool> advance_retry_clock{true};
        std::thread retry_clock([&] {
            while (advance_retry_clock.load(std::memory_order_relaxed)) {
                event_log_shim::advance_ticks(pdMS_TO_TICKS(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        const auto drained_quiesce = event_log_quiesce(1000);
        advance_retry_clock.store(false, std::memory_order_relaxed);
        retry_clock.join();
        assert(drained_quiesce == ESP_OK);
        assert(event_log_quiesce(0) == ESP_OK);
        assert(caller_result == ESP_ERR_TIMEOUT);
        {
            std::lock_guard<std::mutex> lock(audit_mutex);
            assert(durable_release_count == releases_before + 1);
            assert(std::count(audit.begin(), audit.end(), "binary-close") == 1);
        }
        FILE *log_file = std::fopen((std::string(root) + "/logs/events.log").c_str(), "rb");
        assert(log_file != nullptr);
        std::string persisted;
        char chunk[512];
        while (const auto count = std::fread(chunk, 1, sizeof(chunk), log_file)) persisted.append(chunk, count);
        assert(std::fclose(log_file) == 0);
        const auto position = persisted.find(token);
        assert(position != std::string::npos);
        assert(persisted.find(token, position + token.size()) == std::string::npos);
        std::puts("PASS: timed-out accepted durable request fences quiesce until persistence and drain");
        std::fflush(stdout);
        __gcov_dump();
        _exit(0);
    }
    if (scenario == "sticky-close") {
        { std::lock_guard<std::mutex> lock(audit_mutex); fail_next_binary_close = true; }
        std::atomic<bool> advance_clock{true};
        std::thread clock_advancer([&] {
            while (advance_clock.load(std::memory_order_relaxed)) {
                event_log_shim::advance_ticks(pdMS_TO_TICKS(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        const auto close_result = event_log_quiesce(1000);
        advance_clock.store(false, std::memory_order_relaxed);
        clock_advancer.join();
        assert(close_result == ESP_FAIL);
        assert(event_log_quiesce(0) == ESP_FAIL);
        assert(event_log_init() == ESP_ERR_INVALID_STATE);
        std::puts("PASS: owner close failure remains sticky and never publishes CLOSED");
        std::fflush(stdout);
        __gcov_dump();
        _exit(0);
    }
    if (scenario == "sticky-write" || scenario == "sticky-fsync") {
        {
            std::lock_guard<std::mutex> lock(audit_mutex);
            if (scenario == "sticky-write") fail_next_binary_write = true;
            else fail_next_binary_fsync = true;
        }
        event_log_write('E', "sticky", scenario.c_str());
        const std::string observed_failure = scenario == "sticky-write" ? "binary-write-failed" : "binary-fsync-failed";
        {
            std::unique_lock<std::mutex> lock(audit_mutex);
            assert(audit_cv.wait_for(lock, std::chrono::seconds(5), [&] {
                return std::find(audit.begin(), audit.end(), observed_failure) != audit.end();
            }));
        }
        std::atomic<bool> advance_clock{true};
        std::thread clock_advancer([&] {
            while (advance_clock.load(std::memory_order_relaxed)) {
                event_log_shim::advance_ticks(pdMS_TO_TICKS(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        const auto close_result = event_log_quiesce(1000);
        advance_clock.store(false, std::memory_order_relaxed);
        clock_advancer.join();
        assert(close_result == ESP_FAIL);
        assert(event_log_quiesce(0) == ESP_FAIL);
        assert(event_log_init() == ESP_ERR_INVALID_STATE);
        std::puts(scenario == "sticky-write" ? "PASS: void binary write failure remains sticky through quiesce"
                                             : "PASS: binary fsync failure remains sticky through quiesce");
        std::fflush(stdout);
        __gcov_dump();
        _exit(0);
    }
    if (scenario == "worker-timeout-retry") {
        {
            std::lock_guard<std::mutex> lock(fsync_mutex);
            block_fsync = true;
            fsync_entered = false;
            release_fsync = false;
        }
        event_log_write('I', "quiesce", "held-worker-record");
        {
            std::unique_lock<std::mutex> lock(fsync_mutex);
            assert(fsync_cv.wait_for(lock, std::chrono::seconds(5), [] { return fsync_entered; }));
        }
        event_log_shim::reset_ticks();
        std::atomic<bool> advance_clock{true};
        std::thread clock_advancer([&] {
            while (advance_clock.load(std::memory_order_relaxed)) {
                event_log_shim::advance_ticks(pdMS_TO_TICKS(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        const auto first_result = event_log_quiesce(20);
        advance_clock.store(false, std::memory_order_relaxed);
        clock_advancer.join();
        assert(first_result == ESP_ERR_TIMEOUT);
        {
            std::lock_guard<std::mutex> lock(fsync_mutex);
            release_fsync = true;
            block_fsync = false;
        }
        fsync_cv.notify_all();
        std::atomic<bool> advance_retry_clock{true};
        std::thread retry_clock_advancer([&] {
            while (advance_retry_clock.load(std::memory_order_relaxed)) {
                event_log_shim::advance_ticks(pdMS_TO_TICKS(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        const auto retry_result = event_log_quiesce(1000);
        advance_retry_clock.store(false, std::memory_order_relaxed);
        retry_clock_advancer.join();
        assert(retry_result == ESP_OK);
        assert(event_log_quiesce(0) == ESP_OK);
        FILE *log_file = std::fopen((std::string(root) + "/logs/events.log").c_str(), "rb");
        assert(log_file != nullptr);
        std::string persisted;
        char chunk[512];
        while (const auto count = std::fread(chunk, 1, sizeof(chunk), log_file)) persisted.append(chunk, count);
        assert(std::fclose(log_file) == 0);
        const auto position = persisted.find("held-worker-record");
        assert(position != std::string::npos);
        assert(persisted.find("held-worker-record", position + 1) == std::string::npos);
        std::puts("PASS: blocked worker timeout followed by fresh-budget retry persists exactly once");
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
