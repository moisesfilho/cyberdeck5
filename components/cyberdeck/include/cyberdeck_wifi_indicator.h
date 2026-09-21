#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pure state predicate for the single header Wi-Fi icon. SSID is intentionally
 * not part of the contract or rendering path. */
bool cyberdeck_wifi_indicator_is_lit(bool enabled, bool connected, bool has_ip);

#ifdef __cplusplus
}
#endif
