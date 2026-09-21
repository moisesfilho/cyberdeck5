#include "cyberdeck_net_coordinator.h"

namespace {
constexpr size_t QUEUE_CAPACITY = 8;
constexpr size_t CRITICAL_RESERVE = 4;
}

void cyberdeck_net_coordinator::set_notify(cyberdeck_net_notify_fn fn, void *ctx)
{
    m_notify_fn = fn;
    m_notify_ctx = ctx;
}

void cyberdeck_net_coordinator::reset()
{
    m_phase = cyberdeck_net_phase::OFFLINE;
    m_callbacks_active = true;
    m_notify_fn = nullptr;
    m_notify_ctx = nullptr;
    m_head = m_tail = m_count = 0;
}

bool cyberdeck_net_coordinator::has_pending_notice() const
{
    for (size_t i = 0, p = m_head; i < m_count; ++i, p = (p + 1) % QUEUE_CAPACITY)
        if (m_queue[p] == ITEM_SSH_OFFLINE) return true;
    return false;
}

bool cyberdeck_net_coordinator::contains(item_kind item) const
{
    for (size_t i = 0, p = m_head; i < m_count; ++i, p = (p + 1) % QUEUE_CAPACITY)
        if (m_queue[p] == item) return true;
    return false;
}

bool cyberdeck_net_coordinator::enqueue(item_kind item, bool critical)
{
    if (m_count >= QUEUE_CAPACITY) return false;
    if (!critical && m_count >= QUEUE_CAPACITY - CRITICAL_RESERVE) return false;
    m_queue[m_tail] = item;
    m_tail = (m_tail + 1) % QUEUE_CAPACITY;
    ++m_count;
    return true;
}

bool cyberdeck_net_coordinator::has_pending_work(cyberdeck_net_work work) const
{
    item_kind item = ITEM_RUN_SCAN;
    switch (work) {
    case cyberdeck_net_work::TEARDOWN_SSH: item = ITEM_TEARDOWN_SSH; break;
    case cyberdeck_net_work::TEARDOWN_WIFI: item = ITEM_TEARDOWN_WIFI; break;
    case cyberdeck_net_work::RUN_SCAN: item = ITEM_RUN_SCAN; break;
    case cyberdeck_net_work::RUN_RETRY: item = ITEM_RUN_RETRY; break;
    case cyberdeck_net_work::CANCEL_CONNECT: item = ITEM_CANCEL_CONNECT; break;
    case cyberdeck_net_work::WIFI_DISCONNECT: item = ITEM_WIFI_DISCONNECT; break;
    default: return false;
    }
    return contains(item);
}

void cyberdeck_net_coordinator::on_session_connecting()
{
    if (!m_callbacks_active) return;
    if (m_phase == cyberdeck_net_phase::OFFLINE || m_phase == cyberdeck_net_phase::OFFLINE_ERROR)
        m_phase = cyberdeck_net_phase::CONNECTING;
}

void cyberdeck_net_coordinator::on_session_online()
{
    if (m_callbacks_active && m_phase == cyberdeck_net_phase::CONNECTING)
        m_phase = cyberdeck_net_phase::ONLINE;
}

void cyberdeck_net_coordinator::on_socket_error()
{
    if (!m_callbacks_active || m_phase == cyberdeck_net_phase::TEARDOWN_IN_PROGRESS) return;
    if (m_phase != cyberdeck_net_phase::CONNECTING && m_phase != cyberdeck_net_phase::ONLINE) return;
    m_phase = cyberdeck_net_phase::OFFLINE_ERROR;
    if (!has_pending_notice()) (void)enqueue(ITEM_SSH_OFFLINE, false);
}

void cyberdeck_net_coordinator::request_disconnect()
{
    if (!m_callbacks_active || m_phase == cyberdeck_net_phase::TEARDOWN_IN_PROGRESS) return;
    if (m_phase == cyberdeck_net_phase::TEARDOWN_DONE) return;
    m_phase = cyberdeck_net_phase::TEARDOWN_IN_PROGRESS;
    (void)enqueue(ITEM_TEARDOWN_SSH, true);
    (void)enqueue(ITEM_TEARDOWN_WIFI, true);
}

void cyberdeck_net_coordinator::request_wifi_disconnect()
{
    if (!m_callbacks_active || m_phase == cyberdeck_net_phase::TEARDOWN_IN_PROGRESS ||
        m_phase == cyberdeck_net_phase::TEARDOWN_DONE) return;
    if (contains(ITEM_WIFI_DISCONNECT)) return;
    (void)enqueue(ITEM_WIFI_DISCONNECT, true);
}

void cyberdeck_net_coordinator::request_scan()
{
    if (!m_callbacks_active || m_phase == cyberdeck_net_phase::TEARDOWN_IN_PROGRESS) return;
    if (contains(ITEM_RUN_SCAN)) return;
    (void)enqueue(ITEM_RUN_SCAN, false);
}

void cyberdeck_net_coordinator::request_retry()
{
    if (!m_callbacks_active || m_phase == cyberdeck_net_phase::TEARDOWN_IN_PROGRESS) return;
    if (contains(ITEM_RUN_RETRY)) return;
    (void)enqueue(ITEM_RUN_RETRY, false);
}

void cyberdeck_net_coordinator::request_cancel_connect()
{
    if (!m_callbacks_active || m_phase == cyberdeck_net_phase::TEARDOWN_IN_PROGRESS) return;
    if (contains(ITEM_CANCEL_CONNECT)) return;
    (void)enqueue(ITEM_CANCEL_CONNECT, true);
}

void cyberdeck_net_coordinator::on_scan_timer_tick(bool wifi_enabled, bool has_saved_network, bool connected)
{ if (wifi_enabled && !has_saved_network && !connected) request_scan(); }

void cyberdeck_net_coordinator::on_retry_timer_tick(bool wifi_enabled, bool has_saved_network, bool connected)
{ if (wifi_enabled && has_saved_network && !connected) request_retry(); }

cyberdeck_net_drain_result cyberdeck_net_coordinator::drain_pending()
{
    cyberdeck_net_drain_result result;
    if (!m_callbacks_active || m_count == 0) return result;
    const item_kind item = m_queue[m_head];
    m_head = (m_head + 1) % QUEUE_CAPACITY;
    --m_count;
    switch (item) {
    case ITEM_TEARDOWN_SSH: result.work = cyberdeck_net_work::TEARDOWN_SSH; break;
    case ITEM_TEARDOWN_WIFI:
        result.work = cyberdeck_net_work::TEARDOWN_WIFI;
        m_phase = cyberdeck_net_phase::TEARDOWN_DONE;
        m_callbacks_active = false;
        break;
    case ITEM_RUN_SCAN: result.work = cyberdeck_net_work::RUN_SCAN; break;
    case ITEM_RUN_RETRY: result.work = cyberdeck_net_work::RUN_RETRY; break;
    case ITEM_CANCEL_CONNECT: result.work = cyberdeck_net_work::CANCEL_CONNECT; break;
    case ITEM_WIFI_DISCONNECT: result.work = cyberdeck_net_work::WIFI_DISCONNECT; break;
    case ITEM_SSH_OFFLINE:
        result.notice = cyberdeck_net_notice::SSH_OFFLINE;
        if (m_notify_fn) m_notify_fn(result.notice, m_notify_ctx);
        break;
    }
    return result;
}
