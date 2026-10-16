#pragma once

#include <cstdint>

namespace cyberdeck_netdiag::https {

enum class start_result : std::uint8_t { accepted, busy, invalid_request, client_unavailable };
enum class poll_state : std::uint8_t { pending, complete, cancelled, unknown_token };
enum class result_code : std::uint8_t { response, tls_error, transport_error, cancelled };

struct result {
    result_code code = result_code::transport_error;
    std::uint16_t http_status = 0;
    std::uint32_t duration_ms = 0;
};

// url is the original explicit https URL; numeric_address is its resolved IP.
// Calls for a token are serialized. Each poll invokes perform at most once.
start_result start(const char *url, const char *numeric_address, std::uint64_t token);
bool cancel(std::uint64_t token);
poll_state poll(std::uint64_t token, result &completed);
bool busy();

} // namespace cyberdeck_netdiag::https
