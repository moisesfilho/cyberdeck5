#include "apps/shell/cyberdeck_cat_worker.h"
#include "event_log_shim/event_log_shim.h"
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace {
std::mutex mutex;
std::condition_variable cv;
bool entered = false, released = false;
unsigned queue_deletions = 0;
void result(const char *, size_t, bool, void *) { std::abort(); }
}

void vQueueDelete(QueueHandle_t queue) {
    assert(queue != nullptr);
    ++queue_deletions;
    // Handles are retained by the shared host registry until process exit.
    // Logical destruction is witnessed only after the real worker's completion.
}
extern "C" ssize_t __real_read(int, void *, size_t);
extern "C" ssize_t __wrap_read(int fd, void *data, size_t count) {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    cv.notify_all();
    assert(cv.wait_for(lock, std::chrono::seconds(5), [] { return released; }));
    return __real_read(fd, data, count);
}
extern "C" void __gcov_dump();

int main() {
    assert(mkdir("/tmp/opencode", 0700) == 0 || errno == EEXIST);
    char root[] = "/tmp/opencode/cat-poweroff-runtime-XXXXXX";
    assert(mkdtemp(root) != nullptr);
    const std::string path = std::string(root) + "/payload";
    FILE *file = fopen(path.c_str(), "wb");
    assert(file != nullptr);
    assert(fputs("accepted-read", file) >= 0);
    assert(fclose(file) == 0);
    cyberdeck_sd_io::coordinator storage;
    cyberdeck_cat_worker_bind_sd_io(storage);
    assert(cyberdeck_cat_worker_start(root, result, nullptr));
    event_log_shim::wait_for_task_starts(1);
    const auto task = event_log_shim::task_start_history().front();
    assert(task.name == "cat_worker");
    assert(cyberdeck_cat_worker_enqueue("/", "cat payload"));
    {
        std::unique_lock<std::mutex> lock(mutex);
        assert(cv.wait_for(lock, std::chrono::seconds(5), [] { return entered; }));
    }
    assert(storage.active_leases() == 1);
    const auto owner = storage.begin_close();
    assert(!cyberdeck_cat_worker_quiesce(0));
    assert(queue_deletions == 0);
    assert(!cyberdeck_cat_worker_enqueue("/", "cat payload"));
    assert(!cyberdeck_cat_worker_start(root, result, nullptr));
    assert(storage.drain(owner, std::chrono::steady_clock::now()) == cyberdeck_sd_io::drain_result::timed_out);
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
    }
    cv.notify_all();
    event_log_shim::wait_for_timed_wait_registration(task.owner);
    event_log_shim::advance_ticks(pdMS_TO_TICKS(100));
    assert(event_log_shim::wait_for_test_task(task.handle, std::chrono::seconds(5)));
    assert(event_log_shim::join_test_task(task.handle));
    assert(test_task_self_delete_requested(task.handle));
    assert(cyberdeck_cat_worker_quiesce(0));
    assert(queue_deletions == 1);
    assert(cyberdeck_cat_worker_quiesce(0));
    assert(queue_deletions == 1);
    assert(storage.active_leases() == 0);
    assert(storage.drain(owner, std::chrono::steady_clock::now()) == cyberdeck_sd_io::drain_result::drained);
    assert(unlink(path.c_str()) == 0);
    assert(rmdir(root) == 0);
    event_log_shim::cleanup_test_tasks();
    puts("PASS: real ESP cat worker timeout retains resources; drain/stop completion permits one cleanup; no reopen");
    fflush(stdout);
    __gcov_dump();
    _exit(0);
}
