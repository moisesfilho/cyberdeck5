#pragma once

#include "features/wifi/cyberdeck_wifi_event_dispatch.h"

namespace cyberdeck_wifi_test {

class persistence_queue {
public:
    static constexpr std::size_t capacity = 4;

    bool initialize();
    bool initialized() const;
    bool enqueue(const ip_snapshot &snapshot);
    bool retry(const ip_snapshot &snapshot);
    bool acknowledge(const ip_snapshot &snapshot);
    std::size_t pending() const;
    action take();
};

} // namespace cyberdeck_wifi_test
