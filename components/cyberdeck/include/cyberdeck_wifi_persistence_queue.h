#pragma once

#include "cyberdeck_wifi_event_dispatch.h"

namespace cyberdeck_wifi_test {
class persistence_queue {
public:
    static constexpr std::size_t capacity = 4;
    bool initialize();
    bool initialized() const;
    bool enqueue(const ip_snapshot &snapshot);
    bool retry(const ip_snapshot &snapshot);
    /* Confirma que o item devolvido por take() foi persistido. */
    bool acknowledge(const ip_snapshot &snapshot);
    std::size_t pending() const;
    action take();
    /* Drops pending and in-flight snapshots.  Retry is no longer possible. */
    void wipe();
    /* wipe() plus makes the queue unusable until initialize(). */
    void teardown();
private:
    bool m_initialized = false;
    ip_snapshot m_queue[capacity] = {};
    std::size_t m_head = 0;
    std::size_t m_tail = 0;
    std::size_t m_count = 0;
    ip_snapshot m_in_flight = {};
    bool m_has_in_flight = false;
    bool m_retry_ready = false;
};
}
