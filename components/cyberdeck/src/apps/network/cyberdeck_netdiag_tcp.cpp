#include "apps/network/cyberdeck_netdiag_tcp.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace cyberdeck_netdiag::tcp {
namespace {

constexpr std::uint32_t k_connect_timeout_ms = 3000;
constexpr std::size_t k_task_stack_bytes = 8192;

enum class phase : std::uint8_t { free, running, complete };

struct slot {
    phase current = phase::free;
    std::uint64_t token = 0;
    bool cancel_requested = false;
    result answer{};
    sockaddr_storage address{};
    socklen_t address_length = 0;
    int family = 0;
};

slot s_slot{};
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void finish(std::uint64_t token, const result &answer) {
    portENTER_CRITICAL(&s_mux);
    if (s_slot.token != token) {
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    if (s_slot.cancel_requested) {
        // The descriptor is closed before finish(); release without requiring
        // the caller to keep polling a logically cancelled operation.
        s_slot = {};
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    s_slot.answer = answer;
    s_slot.current = phase::complete;
    portEXIT_CRITICAL(&s_mux);
}

void connect_worker(void *argument) {
    (void)argument;
    portENTER_CRITICAL(&s_mux);
    const std::uint64_t token = s_slot.token;
    const std::int64_t started = esp_timer_get_time();
    const int family = s_slot.family;
    const sockaddr_storage address = s_slot.address;
    const socklen_t address_length = s_slot.address_length;
    portEXIT_CRITICAL(&s_mux);
    result answer{};
    const int descriptor = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (descriptor < 0) {
        answer.socket_error = errno;
        answer.code = result_code::network_error;
    } else if (descriptor >= FD_SETSIZE) {
        answer.socket_error = EMFILE;
        answer.code = result_code::network_error;
        close(descriptor);
    } else {
        const int flags = fcntl(descriptor, F_GETFL, 0);
        if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0) {
            answer.socket_error = errno;
            answer.code = result_code::network_error;
        } else {
            const int status = connect(descriptor, reinterpret_cast<const sockaddr *>(&address), address_length);
            if (status == 0) {
                answer.code = result_code::connected;
            } else if (errno != EINPROGRESS) {
                answer.socket_error = errno;
                answer.code = errno == ECONNREFUSED ? result_code::refused : result_code::network_error;
            } else {
                fd_set writable;
                FD_ZERO(&writable);
                FD_SET(descriptor, &writable);
                timeval timeout{static_cast<time_t>(k_connect_timeout_ms / 1000),
                                static_cast<suseconds_t>((k_connect_timeout_ms % 1000) * 1000)};
                const int selected = select(descriptor + 1, nullptr, &writable, nullptr, &timeout);
                if (selected == 0) {
                    answer.code = result_code::timeout;
                } else if (selected < 0) {
                    answer.socket_error = errno;
                    answer.code = result_code::network_error;
                } else {
                    socklen_t error_length = sizeof(answer.socket_error);
                    if (getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &answer.socket_error, &error_length) < 0) {
                        answer.socket_error = errno;
                        answer.code = result_code::network_error;
                    } else if (answer.socket_error == 0) {
                        answer.code = result_code::connected;
                    } else {
                        answer.code =
                            answer.socket_error == ECONNREFUSED ? result_code::refused : result_code::network_error;
                    }
                }
            }
        }
        close(descriptor);
    }
    answer.duration_ms = static_cast<std::uint32_t>((esp_timer_get_time() - started) / 1000);
    finish(token, answer);
    vTaskDelete(nullptr);
}

} // namespace

start_result start(const char *numeric_address, std::uint16_t port, std::uint64_t token) {
    if (numeric_address == nullptr || port == 0)
        return start_result::invalid_endpoint;
    sockaddr_storage address{};
    int family = AF_INET;
    auto *ipv4 = reinterpret_cast<sockaddr_in *>(&address);
    if (inet_pton(AF_INET, numeric_address, &ipv4->sin_addr) == 1) {
        ipv4->sin_family = AF_INET;
        ipv4->sin_port = htons(port);
        address.ss_family = AF_INET;
    } else {
        family = AF_INET6;
        auto *ipv6 = reinterpret_cast<sockaddr_in6 *>(&address);
        if (inet_pton(AF_INET6, numeric_address, &ipv6->sin6_addr) != 1)
            return start_result::invalid_endpoint;
        ipv6->sin6_family = AF_INET6;
        ipv6->sin6_port = htons(port);
        address.ss_family = AF_INET6;
    }
    portENTER_CRITICAL(&s_mux);
    if (s_slot.current != phase::free) {
        portEXIT_CRITICAL(&s_mux);
        return start_result::busy;
    }
    s_slot = {};
    s_slot.current = phase::running;
    s_slot.token = token;
    s_slot.address = address;
    s_slot.family = family;
    s_slot.address_length = family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
    portEXIT_CRITICAL(&s_mux);
    if (xTaskCreate(connect_worker, "netdiag_tcp", k_task_stack_bytes, nullptr, 5, nullptr) != pdPASS) {
        portENTER_CRITICAL(&s_mux);
        s_slot = {};
        portEXIT_CRITICAL(&s_mux);
        return start_result::task_failed;
    }
    return start_result::accepted;
}

bool cancel(std::uint64_t token) {
    portENTER_CRITICAL(&s_mux);
    if (s_slot.current != phase::running || s_slot.token != token) {
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    s_slot.cancel_requested = true;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

poll_state poll(std::uint64_t token, result &completed) {
    portENTER_CRITICAL(&s_mux);
    if (s_slot.current == phase::free || s_slot.token != token) {
        portEXIT_CRITICAL(&s_mux);
        return poll_state::unknown_token;
    }
    if (s_slot.current == phase::running) {
        portEXIT_CRITICAL(&s_mux);
        return poll_state::pending;
    }
    completed = s_slot.answer;
    s_slot = {};
    portEXIT_CRITICAL(&s_mux);
    return poll_state::complete;
}

bool busy() {
    portENTER_CRITICAL(&s_mux);
    const bool occupied = s_slot.current != phase::free;
    portEXIT_CRITICAL(&s_mux);
    return occupied;
}

} // namespace cyberdeck_netdiag::tcp
