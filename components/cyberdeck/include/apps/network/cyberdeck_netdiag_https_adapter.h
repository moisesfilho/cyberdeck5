#pragma once

#include "apps/network/cyberdeck_netdiag_controller.h"

namespace cyberdeck_netdiag {

bool https_head(void *context, const char *url, const char *hostname, uint64_t deadline_ms, uint16_t *http_status);

} // namespace cyberdeck_netdiag
