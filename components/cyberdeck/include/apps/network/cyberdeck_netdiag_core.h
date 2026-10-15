#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cyberdeck_netdiag {

constexpr std::size_t k_target_capacity = 255;

enum class operation : std::uint8_t { none, status, dns, tcp, https, result, cancel };
enum class state : std::uint8_t { idle, queued, running, complete, failed, cancelled };
enum class error : std::uint8_t {
    none,
    invalid_command,
    invalid_target,
    invalid_port,
    busy,
    rate_limited,
    grant_revoked,
    backend_unavailable,
    dns_failed,
    tcp_failed,
    tls_failed,
    http_failed,
    timeout,
    cancelled,
    response_too_large,
    clock_regressed
};

struct request {
    operation kind = operation::none;
    std::array<char, k_target_capacity + 1> target{};
    std::uint16_t port = 0;
};

struct outcome {
    state status = state::idle;
    error reason = error::none;
    std::uint32_t duration_ms = 0;
    std::uint16_t http_status = 0;
};

struct backend {
    void *context = nullptr;
    bool (*submit)(void *context, const request &work, std::uint64_t token) = nullptr;
    void (*cancel)(void *context, std::uint64_t token) = nullptr;
};

struct parsed_command {
    operation kind = operation::none;
    request work{};
    error failure = error::none;
};

parsed_command parse_command(std::string_view command);

/* Controller calls are serialized by the owner. Backend callbacks may complete
 * later from any context only after the owner marshals them to that serializer. */
class controller final {
  public:
    bool admit(const parsed_command &command, std::uint64_t now_ms, bool network_grant, const backend &service,
               error &failure);
    bool cancel(const backend &service);
    bool mark_running(std::uint64_t token);
    bool complete(std::uint64_t token, outcome result);

    outcome snapshot() const {
        return outcome_;
    }
    std::uint64_t generation() const {
        return generation_;
    }
    bool in_flight() const {
        return in_flight_;
    }

  private:
    outcome outcome_{};
    std::uint64_t generation_ = 0;
    std::uint64_t last_admitted_ms_ = 0;
    bool has_admitted_ = false;
    bool in_flight_ = false;
    bool cancelled_ = false;
};

} // namespace cyberdeck_netdiag
