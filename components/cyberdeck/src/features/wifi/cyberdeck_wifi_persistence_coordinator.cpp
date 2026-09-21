#include "features/wifi/cyberdeck_wifi_persistence_coordinator.h"

namespace cyberdeck_wifi_test {

persistence_coordinator::persistence_coordinator(event_dispatch &producer,
                                                   persistence_queue &persistence,
                                                   storage_sink &storage,
                                                   retry_scheduler &scheduler)
{
    m_state.producer = &producer;
    m_state.persistence = &persistence;
    m_state.storage = &storage;
    m_state.scheduler = &scheduler;
}

bool persistence_coordinator::initialize()
{
    m_state.torn_down = false;
    m_state.has_producer = false;
    m_state.has_persist = false;
    m_state.retry = false;
    wipe_action(m_state.producer_slot);
    wipe_action(m_state.persist_slot);
    return m_state.producer->initialize() && m_state.persistence->initialize();
}

void persistence_coordinator::on_connected(std::uint64_t token, const char *ssid)
{ if (!m_state.torn_down) m_state.producer->on_connected(token, ssid); }

bool persistence_coordinator::on_got_ip(const got_ip_event_data &event_data,
                                        const connection_config &config)
{ return !m_state.torn_down && m_state.producer->on_got_ip(event_data, config); }

void persistence_coordinator::on_lost_ip(std::uint64_t token, const char *ssid)
{ if (!m_state.torn_down) m_state.producer->on_lost_ip(token, ssid); }

bool persistence_coordinator::pump_producer()
{
    if (m_state.torn_down || m_state.has_producer || m_state.has_persist) return false;
    action item = m_state.producer->drain_one();
    if (item.kind != action_kind::persist) {
        if (item.kind == action_kind::lost_ip) (void)m_state.producer->acknowledge(item);
        wipe_action(item);
        return false;
    }
    m_state.producer_slot = item;
    m_state.has_producer = true;
    if (!m_state.persistence->enqueue(m_state.producer_slot.snapshot)) {
        return false; // retain ownership until the bounded queue has space
    }
    (void)m_state.producer->acknowledge(m_state.producer_slot);
    wipe_action(m_state.producer_slot);
    m_state.has_producer = false;
    return true;
}

bool persistence_coordinator::pump_persistence()
{
    if (m_state.torn_down || m_state.has_persist || m_state.retry) return false;
    const action item = m_state.persistence->take();
    if (item.kind != action_kind::persist) return false;
    m_state.persist_slot = item;
    m_state.has_persist = true;

    if (m_state.storage->persist(m_state.persist_slot.snapshot)) {
        (void)m_state.persistence->acknowledge(m_state.persist_slot.snapshot);
        wipe_action(m_state.persist_slot);
        m_state.has_persist = false;
        return true;
    }

    if (m_state.persistence->retry(m_state.persist_slot.snapshot)) {
        wipe_action(m_state.persist_slot);
        m_state.has_persist = false;
        m_state.retry = true;
        m_state.scheduler->arm();
    }
    return false;
}

void persistence_coordinator::on_retry_timer()
{
    if (m_state.torn_down || !m_state.retry) return;
    m_state.retry = false;
    m_state.scheduler->cancel();
    /* take() is deliberately deferred to pump_persistence(), keeping timer
     * callbacks bounded and free of storage work. */
}

void persistence_coordinator::rollback()
{
    m_state.scheduler->cancel();
    m_state.retry = false;
    wipe_action(m_state.producer_slot);
    wipe_action(m_state.persist_slot);
    m_state.has_producer = false;
    m_state.has_persist = false;
    m_state.producer->wipe();
    m_state.persistence->wipe();
}

void persistence_coordinator::disconnect(std::uint64_t invalidated_token)
{
    rollback();
    m_state.producer->invalidate(invalidated_token);
}

void persistence_coordinator::teardown()
{
    if (m_state.torn_down) return;
    rollback();
    m_state.producer->teardown();
    m_state.persistence->teardown();
    m_state.torn_down = true;
}

bool persistence_coordinator::producer_pending() const
{ return !m_state.torn_down && m_state.producer->pending() != 0; }
bool persistence_coordinator::persist_pending() const
{ return !m_state.torn_down && m_state.persistence->pending() != 0; }
bool persistence_coordinator::retry_pending() const { return m_state.retry; }

} // namespace cyberdeck_wifi_test
