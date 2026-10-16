#include "apps/network/cyberdeck_netdiag_tcp_adapter.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace cyberdeck_netdiag {
namespace {

constexpr uint64_t k_wait_slice_ms = 50;

uint64_t monotonic_ms() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

} // namespace

bool tcp_connect(void *, const char *ipv4, uint16_t port, uint64_t deadline_ms) {
    if (ipv4 == nullptr || ipv4[0] == '\0' || port == 0 || monotonic_ms() >= deadline_ms)
        return false;

    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (inet_pton(AF_INET, ipv4, &endpoint.sin_addr) != 1)
        return false;

    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return false;
    if (fd >= FD_SETSIZE) {
        close(fd);
        return false;
    }

    bool connected = false;
    const int original_flags = fcntl(fd, F_GETFL, 0);
    if (original_flags < 0 || fcntl(fd, F_SETFL, original_flags | O_NONBLOCK) < 0) {
        close(fd);
        return false;
    }

    const int result = connect(fd, reinterpret_cast<const sockaddr *>(&endpoint), sizeof(endpoint));
    if (result == 0) {
        connected = monotonic_ms() < deadline_ms;
    } else if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
        for (;;) {
            const uint64_t now = monotonic_ms();
            if (now >= deadline_ms)
                break;

            const uint64_t remaining = deadline_ms - now;
            const uint64_t wait_ms = remaining < k_wait_slice_ms ? remaining : k_wait_slice_ms;
            timeval timeout{};
            timeout.tv_sec = static_cast<time_t>(wait_ms / 1000);
            timeout.tv_usec = static_cast<suseconds_t>((wait_ms % 1000) * 1000);
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(fd, &writable);

            const int selected = select(fd + 1, nullptr, &writable, nullptr, &timeout);
            if (selected < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (selected == 0)
                continue;
            if (monotonic_ms() >= deadline_ms)
                break;

            int socket_error = 0;
            socklen_t error_length = sizeof(socket_error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) == 0 && socket_error == 0) {
                connected = true;
            }
            break;
        }
    }

    const int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return connected;
}

} // namespace cyberdeck_netdiag
