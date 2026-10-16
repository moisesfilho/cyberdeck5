#include "apps/network/cyberdeck_netdiag_tcp.h"
#include "freertos/task.h"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>

namespace {
int checks = 0;
std::mutex gate_mutex;
std::condition_variable gate_cv;
bool hold_select = false, select_entered = false, release_select = false, threaded_worker = false;
std::thread worker;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(x)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                                          \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)
struct behavior {
    int connect_result = -1, connect_errno = EINPROGRESS, selected = 1, so_error = 0, select_errno = 0,
        socket_result = 9, fcntl_error = 0, getsockopt_error = 0;
    int closes = 0;
    timeval observed{};
} b;
std::uint64_t cancel_token = 0;
std::int64_t now_us = 100000;
} // namespace
std::int64_t esp_timer_get_time() {
    return now_us += 7000;
}
BaseType_t xTaskCreate(TaskFunction_t fn, const char *, unsigned, void *arg, UBaseType_t, void *) {
    if (threaded_worker)
        worker = std::thread(fn, arg);
    else
        fn(arg);
    return pdPASS;
}
void vTaskDelete(void *) {}
extern "C" int __wrap_socket(int, int, int) {
    if (b.socket_result < 0)
        errno = EMFILE;
    return b.socket_result;
}
extern "C" int __wrap_fcntl(int, int cmd, ...) {
    if (b.fcntl_error) {
        errno = EIO;
        return -1;
    }
    return cmd == F_GETFL ? 0 : 0;
}
extern "C" int __wrap_connect(int, const sockaddr *a, socklen_t n) {
    CHECK((a->sa_family == AF_INET && n == sizeof(sockaddr_in)) ||
          (a->sa_family == AF_INET6 && n == sizeof(sockaddr_in6)));
    if (cancel_token) {
        CHECK(cyberdeck_netdiag::tcp::cancel(cancel_token));
        cancel_token = 0;
    }
    errno = b.connect_errno;
    return b.connect_result;
}
extern "C" int __wrap_select(int, fd_set *, fd_set *, fd_set *, timeval *tv) {
    b.observed = *tv;
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        if (hold_select) {
            select_entered = true;
            gate_cv.notify_all();
            gate_cv.wait(lock, [] { return release_select; });
        }
    }
    errno = b.select_errno;
    return b.selected;
}
extern "C" int __wrap_getsockopt(int, int, int, void *v, socklen_t *) {
    if (b.getsockopt_error) {
        errno = EIO;
        return -1;
    }
    *static_cast<int *>(v) = b.so_error;
    return 0;
}
extern "C" int __wrap_close(int) {
    ++b.closes;
    return 0;
}

void join_worker() {
    if (worker.joinable())
        worker.join();
}

int main() {
    using namespace cyberdeck_netdiag::tcp;
    CHECK(start(nullptr, 80, 1) == start_result::invalid_endpoint);
    CHECK(start("127.0.0.1", 0, 1) == start_result::invalid_endpoint);
    CHECK(start("not-ip", 80, 1) == start_result::invalid_endpoint);
    CHECK(start("127.0.0.1", 80, 1) == start_result::accepted);
    result r{};
    CHECK(poll(2, r) == poll_state::unknown_token);
    CHECK(poll(1, r) == poll_state::complete && r.code == result_code::connected);
    CHECK(b.closes == 1);
    b = {};
    b.socket_result = -1;
    CHECK(start("127.0.0.1", 80, 8) == start_result::accepted);
    CHECK(poll(8, r) == poll_state::complete && r.code == result_code::network_error && r.socket_error == EMFILE &&
          b.closes == 0);
    b = {};
    b.fcntl_error = 1;
    CHECK(start("127.0.0.1", 80, 9) == start_result::accepted);
    CHECK(poll(9, r) == poll_state::complete && r.code == result_code::network_error && b.closes == 1);
    b = {};
    b.connect_result = -1;
    b.connect_errno = EINPROGRESS;
    b.selected = 1;
    CHECK(start("2001:db8::1", 443, 2) == start_result::accepted);
    CHECK(poll(2, r) == poll_state::complete && r.code == result_code::connected);
    CHECK(b.observed.tv_sec == 3 && b.observed.tv_usec == 0 && b.closes == 1);
    b = {};
    b.connect_result = -1;
    b.connect_errno = EINPROGRESS;
    b.selected = 0;
    CHECK(start("192.0.2.1", 443, 3) == start_result::accepted);
    CHECK(poll(3, r) == poll_state::complete && r.code == result_code::timeout && b.observed.tv_sec == 3 &&
          b.closes == 1);
    b = {};
    b.connect_errno = ECONNREFUSED;
    CHECK(start("192.0.2.1", 443, 4) == start_result::accepted);
    CHECK(poll(4, r) == poll_state::complete && r.code == result_code::refused && r.socket_error == ECONNREFUSED &&
          b.closes == 1);
    b = {};
    b.connect_errno = EINPROGRESS;
    b.selected = -1;
    b.select_errno = EINTR;
    CHECK(start("192.0.2.1", 443, 5) == start_result::accepted);
    CHECK(poll(5, r) == poll_state::complete && r.code == result_code::network_error && r.socket_error == EINTR);
    b = {};
    b.connect_errno = EINPROGRESS;
    b.selected = 1;
    b.getsockopt_error = 1;
    CHECK(start("192.0.2.1", 443, 10) == start_result::accepted);
    CHECK(poll(10, r) == poll_state::complete && r.code == result_code::network_error && r.socket_error == EIO &&
          b.closes == 1);
    b = {};
    b.connect_errno = EINPROGRESS;
    b.selected = 1;
    b.so_error = ECONNREFUSED;
    CHECK(start("192.0.2.1", 443, 11) == start_result::accepted);
    CHECK(poll(11, r) == poll_state::complete && r.code == result_code::refused && r.socket_error == ECONNREFUSED);
    b = {};
    b.connect_result = 0;
    CHECK(start("127.0.0.1", 80, 6) == start_result::accepted);
    CHECK(poll(6, r) == poll_state::complete && b.closes == 1);
    b = {};
    cancel_token = 7;
    CHECK(start("127.0.0.1", 80, 7) == start_result::accepted);
    CHECK(poll(7, r) == poll_state::unknown_token && b.closes == 1);
    CHECK(poll(7, r) == poll_state::unknown_token);
    join_worker();
    b = {};
    b.connect_result = -1;
    b.connect_errno = EINPROGRESS;
    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        hold_select = true;
        threaded_worker = true;
        select_entered = false;
        release_select = false;
    }
    CHECK(start("127.0.0.1", 80, 12) == start_result::accepted);
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        CHECK(gate_cv.wait_for(lock, std::chrono::seconds(2), [] { return select_entered; }));
    }
    CHECK(cancel(12));
    CHECK(poll(12, r) == poll_state::pending);
    CHECK(b.closes == 0 && busy());
    CHECK(start("127.0.0.1", 80, 13) == start_result::busy);
    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        release_select = true;
        gate_cv.notify_all();
    }
    join_worker();
    threaded_worker = false;
    CHECK(b.closes == 1 && !busy());
    CHECK(poll(12, r) == poll_state::unknown_token);
    CHECK(start("127.0.0.1", 80, 13) == start_result::accepted);
    join_worker();
    CHECK(poll(13, r) == poll_state::complete);
    std::printf("TCP adapter: %d checks\n", checks);
}
