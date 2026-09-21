#include "features/wifi/cyberdeck_wifi_persistence_queue.h"
#include <cstring>

namespace cyberdeck_wifi_test {
namespace {
bool same_key(const ip_snapshot &a, const ip_snapshot &b)
{ return std::strncmp(a.ssid, b.ssid, sizeof(a.ssid)) == 0; }
void wipe_snapshot(ip_snapshot &snapshot)
{
    volatile unsigned char *p = reinterpret_cast<volatile unsigned char *>(&snapshot);
    for (std::size_t n = 0; n < sizeof(snapshot); ++n) p[n] = 0;
}
}
bool persistence_queue::initialize()
{
    if (!m_initialized) {
        m_head = m_tail = m_count = 0;
        wipe_snapshot(m_in_flight);
        m_has_in_flight = false;
        m_retry_ready = false;
        m_initialized = true;
    }
    return true;
}
bool persistence_queue::initialized() const { return m_initialized; }
bool persistence_queue::enqueue(const ip_snapshot &snapshot)
{
    if (!m_initialized) return false;
    if (m_has_in_flight && same_key(m_in_flight, snapshot)) return false;
    for (std::size_t i = 0, p = m_head; i < m_count; ++i, p = (p + 1) % capacity)
        if (same_key(m_queue[p], snapshot)) return false;
    if (m_count == capacity) return false;
    m_queue[m_tail] = snapshot;
    m_tail = (m_tail + 1) % capacity;
    ++m_count;
    return true;
}
bool persistence_queue::retry(const ip_snapshot &snapshot)
{
    if (!m_initialized || !m_has_in_flight || !same_key(m_in_flight, snapshot) ||
        m_in_flight.token != snapshot.token) return false;
    m_retry_ready = true;
    return true;
}
bool persistence_queue::acknowledge(const ip_snapshot &snapshot)
{
    if (!m_has_in_flight || !same_key(m_in_flight, snapshot) ||
        m_in_flight.token != snapshot.token) return false;
    wipe_snapshot(m_in_flight);
    m_has_in_flight = false;
    m_retry_ready = false;
    return true;
}
std::size_t persistence_queue::pending() const { return m_count + (m_has_in_flight ? 1U : 0U); }
action persistence_queue::take()
{
    if (!m_initialized) return {action_kind::none, {}};
    if (m_has_in_flight) {
        if (!m_retry_ready) return {action_kind::none, {}};
        m_retry_ready = false;
        return {action_kind::persist, m_in_flight};
    }
    if (m_count == 0) return {action_kind::none, {}};
    ip_snapshot result = m_queue[m_head];
    wipe_snapshot(m_queue[m_head]);
    m_head = (m_head + 1) % capacity;
    --m_count;
    m_in_flight = result;
    m_has_in_flight = true;
    return {action_kind::persist, result};
}

void persistence_queue::wipe()
{
    for (std::size_t i = 0; i < capacity; ++i) {
        wipe_snapshot(m_queue[i]);
    }
    wipe_snapshot(m_in_flight);
    m_head = m_tail = m_count = 0;
    m_has_in_flight = false;
    m_retry_ready = false;
}

void persistence_queue::teardown()
{
    wipe();
    m_initialized = false;
}
}
