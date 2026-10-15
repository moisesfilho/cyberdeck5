#pragma once

#include <array>
#include <cstdint>

namespace cyberdeck_netdiag::dns {

enum class start_result : std::uint8_t { accepted, busy, invalid_target, scheduling_failed };
enum class poll_state : std::uint8_t { pending, complete, cancelled, unknown_token };
enum class address_family : std::uint8_t { none, ipv4, ipv6 };
enum class result_code : std::uint8_t { failed, resolved };

struct result {
    result_code code = result_code::failed;
    address_family family = address_family::none;
    std::array<std::uint8_t, 16> address{};
};

// One process-wide lookup slot. Caller operations are serialized by their owner.
// A cancelled slot remains quarantined until the queued trampoline or DNS callback
// proves that lwIP no longer retains its callback argument.
start_result start(const char *hostname, std::uint64_t token);
bool cancel(std::uint64_t token);
poll_state poll(std::uint64_t token, result &resolved);
bool busy();

} // namespace cyberdeck_netdiag::dns
