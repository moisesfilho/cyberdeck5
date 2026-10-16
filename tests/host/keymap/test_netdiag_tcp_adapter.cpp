#include "apps/network/cyberdeck_netdiag_tcp_adapter.h"

#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
uint64_t now_ms;
int socket_result, connect_result, connect_error, select_result, select_error;
int getsockopt_result, socket_error, fd_value = 7;
int close_count, select_count, getsockopt_count, fcntl_count, fail_fcntl_call;
sockaddr_in seen_endpoint;
void reset() {
    now_ms = 0;
    socket_result = fd_value;
    connect_result = 0;
    connect_error = 0;
    select_result = 1;
    select_error = 0;
    getsockopt_result = 0;
    socket_error = 0;
    close_count = select_count = getsockopt_count = fcntl_count = fail_fcntl_call = 0;
    std::memset(&seen_endpoint, 0, sizeof(seen_endpoint));
}
} // namespace

int64_t esp_timer_get_time(void) {
    return static_cast<int64_t>(now_ms * 1000);
}
extern "C" int __wrap_socket(int, int, int) {
    return socket_result;
}
extern "C" int __wrap_close(int) {
    ++close_count;
    return 0;
}
extern "C" int __wrap_fcntl(int, int cmd, ...) {
    ++fcntl_count;
    if (fcntl_count == fail_fcntl_call)
        return -1;
    return cmd == F_GETFL ? 0 : 0;
}
extern "C" int __wrap_connect(int, const sockaddr *addr, socklen_t) {
    seen_endpoint = *reinterpret_cast<const sockaddr_in *>(addr);
    errno = connect_error;
    return connect_result;
}
extern "C" int __wrap_select(int, fd_set *, fd_set *, fd_set *, timeval *timeout) {
    ++select_count;
    if (select_result == 0 || (select_result < 0 && select_error == EINTR))
        now_ms += static_cast<uint64_t>(timeout->tv_sec) * 1000 + timeout->tv_usec / 1000;
    errno = select_error;
    return select_result;
}
extern "C" int __wrap_getsockopt(int, int, int, void *value, socklen_t *length) {
    ++getsockopt_count;
    assert(*length == sizeof(int));
    *static_cast<int *>(value) = socket_error;
    return getsockopt_result;
}

static void test_immediate_success_and_refusal() {
    reset();
    assert(cyberdeck_netdiag::tcp_connect(nullptr, "192.0.2.8", 443, 10));
    assert(seen_endpoint.sin_family == AF_INET && ntohs(seen_endpoint.sin_port) == 443);
    assert(close_count == 1 && select_count == 0);
    reset();
    connect_result = -1;
    connect_error = ECONNREFUSED;
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "192.0.2.8", 80, 10));
    assert(close_count == 1 && select_count == 0);
}
static void test_nonblocking_completion_and_socket_error() {
    reset();
    connect_result = -1;
    connect_error = EINPROGRESS;
    assert(cyberdeck_netdiag::tcp_connect(nullptr, "198.51.100.2", 22, 10));
    assert(select_count == 1 && getsockopt_count == 1 && close_count == 1);
    reset();
    connect_result = -1;
    connect_error = EINPROGRESS;
    socket_error = ECONNREFUSED;
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "198.51.100.2", 22, 10));
    assert(getsockopt_count == 1 && close_count == 1);
}
static void test_deadlines_and_select_failures() {
    reset();
    now_ms = 10;
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "192.0.2.1", 80, 10));
    assert(close_count == 0 && socket_result == fd_value);
    reset();
    connect_result = -1;
    connect_error = EINPROGRESS;
    select_result = 0;
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "192.0.2.1", 80, 3));
    assert(select_count == 1 && close_count == 1 && getsockopt_count == 0);
    reset();
    connect_result = -1;
    connect_error = EINPROGRESS;
    select_result = -1;
    select_error = EIO;
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "192.0.2.1", 80, 10));
    assert(close_count == 1 && getsockopt_count == 0);
    reset();
    connect_result = -1;
    connect_error = EINPROGRESS;
    select_result = -1;
    select_error = EINTR;
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "192.0.2.1", 80, 10));
    assert(select_count == 1 && close_count == 1);
}
static void test_invalid_inputs_and_ownership() {
    reset();
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "not-ip", 80, 10));
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "192.0.2.1", 0, 10));
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, nullptr, 80, 10));
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "", 80, 10));
    assert(close_count == 0);
    reset();
    socket_result = -1;
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "192.0.2.1", 80, 10));
    assert(close_count == 0);
    reset();
    socket_result = FD_SETSIZE;
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "192.0.2.1", 80, 10));
    assert(close_count == 1);
    reset();
    fail_fcntl_call = 1;
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "192.0.2.1", 80, 10));
    assert(close_count == 1);
    reset();
    fail_fcntl_call = 2;
    assert(!cyberdeck_netdiag::tcp_connect(nullptr, "192.0.2.1", 80, 10));
    assert(close_count == 1);
}
int main() {
    test_immediate_success_and_refusal();
    test_nonblocking_completion_and_socket_error();
    test_deadlines_and_select_failures();
    test_invalid_inputs_and_ownership();
    std::puts("TEST-ND-03 TCP adapter behavioral tests passed");
}
