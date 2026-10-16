#pragma once

#include <stdint.h>

namespace cyberdeck_netdiag {

bool tcp_connect(void *context, const char *ipv4, uint16_t port, uint64_t deadline_ms);

} // namespace cyberdeck_netdiag
