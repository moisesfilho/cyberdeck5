#pragma once

#include <cstdint>

namespace cyberdeck_netdiag::tcp {

enum class start_result : std::uint8_t { accepted, busy, invalid_endpoint, task_failed };
enum class poll_state : std::uint8_t { pending, complete, cancelled, unknown_token };
enum class result_code : std::uint8_t { connected, refused, timeout, network_error, cancelled };

struct result {
    result_code code = result_code::network_error;
    int socket_error = 0;
    std::uint32_t duration_ms = 0;
};

// Endpoint must be a numeric IPv4/IPv6 address. Calls for a token are serialized.
// Cancellation is cooperative: poll stays pending until the worker closes its
// socket; then it releases the slot without another poll and the token is unknown.
// A cancelled worker does not publish a completion result.
start_result start(const char *numeric_address, std::uint16_t port, std::uint64_t token);
bool cancel(std::uint64_t token);
poll_state poll(std::uint64_t token, result &completed);
bool busy();

} // namespace cyberdeck_netdiag::tcp
