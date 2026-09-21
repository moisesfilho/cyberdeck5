#include "features/wifi/cyberdeck_wifi_event_dispatch.h"
#include <cstring>

namespace cyberdeck_wifi_test {
namespace {
template <size_t N> void copy_bounded(char (&dst)[N], const char *src)
{
    if (src == nullptr) { dst[0] = '\0'; return; }
    size_t length = std::strlen(src);
    if (length >= N) length = N - 1;
    std::memcpy(dst, src, length);
    dst[length] = '\0';
}

bool same_ssid(const ip_snapshot &a, const ip_snapshot &b)
{
    return std::strncmp(a.ssid, b.ssid, sizeof(a.ssid)) == 0;
}
}

void wipe_action(action &item)
{
    volatile unsigned char *p = reinterpret_cast<volatile unsigned char *>(&item);
    for (size_t i = 0; i < sizeof(item); ++i) p[i] = 0;
}

bool event_dispatch::initialize()
{
    if (!m_initialized) {
        m_head = m_tail = m_count = 0;
        m_active_token = 0;
        m_generation_floor = 0;
        m_active_ssid[0] = '\0';
        m_has_active_connection = false;
        m_bootstrap_consumed = false;
        m_has_in_flight = false;
        m_initialized = true;
    }
    return true;
}
bool event_dispatch::initialized() const { return m_initialized; }

void event_dispatch::on_connected(std::uint64_t token, const char *ssid)
{
    if (!m_initialized) return;
    /* A driver callback is allowed to open a new generation only through the
     * explicit connected transition.  Older callbacks are never allowed to
     * move the generation backwards (or to revive one invalidated by LOST_IP).
     */
    const bool initial_bootstrap = !m_bootstrap_consumed && !m_has_active_connection &&
                                   token == 0 && m_generation_floor == 0;
    if ((token > m_active_token && token > m_generation_floor) || initial_bootstrap) {
        m_active_token = token;
        m_has_active_connection = true;
        m_bootstrap_consumed = true;
        copy_bounded(m_active_ssid, ssid);
    }
}

bool event_dispatch::on_got_ip(const got_ip_event_data &event_data, const connection_config &config)
{
    if (!m_initialized) return false;
    /* The first GOT_IP establishes the generation.  Thereafter an event is
     * accepted only for that exact generation: accepting a larger token here
     * would let a reordered/future callback replace the current connection. */
    const bool initial_bootstrap = !m_bootstrap_consumed && !m_has_active_connection &&
                                   config.token == 0 && m_generation_floor == 0;
    if ((!m_has_active_connection && config.token > m_generation_floor) || initial_bootstrap) {
        m_active_token = config.token;
        m_has_active_connection = true;
        m_bootstrap_consumed = true;
        copy_bounded(m_active_ssid, config.ssid);
    } else if (!m_has_active_connection || config.token != m_active_token) return false;
    ip_snapshot item{};
    item.token = config.token;
    item.ip = event_data.ip;
    copy_bounded(item.ssid, config.ssid);
    copy_bounded(item.password, config.password);
    for (std::size_t i = 0, p = m_head; i < m_count; ++i, p = (p + 1) % queue_capacity)
        if (same_ssid(m_queue[p].snapshot, item)) return false;
    if (m_count == queue_capacity) return false;
    m_queue[m_tail] = {action_kind::persist, item};
    m_tail = (m_tail + 1) % queue_capacity;
    ++m_count;
    return true;
}

void event_dispatch::on_lost_ip(std::uint64_t token, const char *ssid)
{
    if (!m_initialized || !m_has_active_connection || token != m_active_token) return;
    (void)ssid;
    char active_ssid[sizeof(m_active_ssid)] = {};
    copy_bounded(active_ssid, m_active_ssid);
    /* Zero means that no generation is currently eligible.  The monotonic
     * token is retained as the lower bound, so a late callback carrying the
     * old token cannot become the next connection. */
    m_generation_floor = token;
    m_active_token = 0;
    m_has_active_connection = false;
    m_active_ssid[0] = '\0';
    if (m_count == queue_capacity) return;
    ip_snapshot item{};
    item.token = token;
    copy_bounded(item.ssid, active_ssid);
    m_queue[m_tail] = {action_kind::lost_ip, item};
    m_tail = (m_tail + 1) % queue_capacity;
    ++m_count;
}

bool event_dispatch::retry_pending()
{
    /* Queue entries are already the bounded retry backlog.  Report whether
     * there is work still owned by this dispatcher; do not manufacture a
     * duplicate entry or grow the queue. */
    return m_initialized && m_count != 0 && m_count < queue_capacity && !m_has_in_flight;
}
std::size_t event_dispatch::pending() const { return m_count; }
action event_dispatch::drain_one()
{
    if (!m_initialized || m_count == 0 || m_has_in_flight) return {action_kind::none, {}};
    m_has_in_flight = true;
    return m_queue[m_head];
}

bool event_dispatch::acknowledge(const action &item)
{
    if (!m_initialized || !m_has_in_flight || item.kind != m_queue[m_head].kind ||
        item.snapshot.token != m_queue[m_head].snapshot.token) return false;
    wipe_action(m_queue[m_head]);
    m_head = (m_head + 1) % queue_capacity;
    --m_count;
    m_has_in_flight = false;
    return true;
}

bool event_dispatch::retry(const action &item)
{
    return m_initialized && m_has_in_flight && item.kind == m_queue[m_head].kind &&
           item.snapshot.token == m_queue[m_head].snapshot.token;
}

void event_dispatch::wipe()
{
    for (size_t i = 0; i < queue_capacity; ++i) wipe_action(m_queue[i]);
    m_head = m_tail = m_count = 0;
    m_has_in_flight = false;
    m_active_token = 0;
    m_generation_floor = 0;
    m_active_ssid[0] = '\0';
    m_has_active_connection = false;
    m_bootstrap_consumed = false;
}

void event_dispatch::invalidate(std::uint64_t token)
{
    wipe();
    m_generation_floor = token;
    m_bootstrap_consumed = true;
}

void event_dispatch::teardown()
{
    wipe();
    m_initialized = false;
}
}
