#pragma once

#include <stdint.h>

namespace cyberdeck_netdiag {

bool resolve_ipv4(void *context, const char *hostname, uint64_t deadline_ms, char output_ipv4[16]);

} // namespace cyberdeck_netdiag
