#include "cyberdeck_wifi_indicator.h"

bool cyberdeck_wifi_indicator_is_lit(bool enabled, bool connected, bool has_ip)
{
    return enabled && connected && has_ip;
}
