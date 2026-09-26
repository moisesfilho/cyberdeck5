#include "features/bluetooth/cyberdeck_ble_event_dispatch.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace cyberdeck_ble {

struct event_dispatch::State {
    struct QueuedEvent {
        ble_event event;
        std::uint64_t scan_gen = 0;
        std::uint64_t pair_gen = 0;
        std::uint64_t connect_gen = 0;
    };

    std::array<QueuedEvent, k_max_pending_events> queue;
    std::size_t head = 0;
    std::size_t tail = 0;
    std::size_t count = 0;

    std::uint64_t token_counter = 0;
    std::uint64_t active_scan_token = 0;
    std::uint64_t active_pair_token = 0;
    std::uint64_t active_connection_token = 0;
    std::uint64_t scan_generation = 0;
    std::uint64_t pair_generation = 0;
    std::uint64_t connect_generation = 0;
    std::uint64_t previous_scan_generation = 0;
    std::uint64_t previous_pair_generation = 0;
    std::uint64_t previous_connect_generation = 0;
    std::string connection_address;
    bool connection_automatic = false;

    std::uint64_t next_token()
    {
        return ++token_counter;
    }

    bool enqueue(const QueuedEvent &evt)
    {
        if (count >= k_max_pending_events) {
            return false;
        }
        queue[tail] = evt;
        tail = (tail + 1) % k_max_pending_events;
        ++count;
        return true;
    }

    bool dequeue(ble_event &out)
    {
        if (count == 0) {
            return false;
        }
        out = queue[head].event;
        head = (head + 1) % k_max_pending_events;
        --count;
        return true;
    }

    bool is_stale(const QueuedEvent &queued) const
    {
        switch (queued.event.kind) {
        case ble_event_kind::scan_started:
        case ble_event_kind::scan_result:
        case ble_event_kind::scan_finished:
            return queued.scan_gen != 0 && queued.scan_gen != scan_generation &&
                   queued.scan_gen != previous_scan_generation;
        case ble_event_kind::auth_request:
        case ble_event_kind::pair_finished:
            return queued.pair_gen != 0 && queued.pair_gen != pair_generation &&
                   queued.pair_gen != previous_pair_generation;
        case ble_event_kind::connected:
        case ble_event_kind::disconnected:
            return queued.connect_gen != 0 &&
                   queued.connect_gen != connect_generation &&
                   queued.connect_gen != previous_connect_generation;
        }
        return true;
    }

    std::size_t drop_stale_generations()
    {
        std::size_t dropped = 0;
        std::size_t i = head;
        for (std::size_t c = 0; c < count; ++c) {
            if (is_stale(queue[i])) {
                ++dropped;
            }
            i = (i + 1) % k_max_pending_events;
        }
        if (dropped == 0) {
            return 0;
        }
        std::size_t write = head;
        std::size_t read = head;
        for (std::size_t c = 0; c < count; ++c) {
            if (!is_stale(queue[read])) {
                if (write != read) {
                    queue[write] = queue[read];
                }
                write = (write + 1) % k_max_pending_events;
            }
            read = (read + 1) % k_max_pending_events;
        }
        count -= dropped;
        tail = write;
        return dropped;
    }
};

event_dispatch::event_dispatch() : state_(new State()) {}

event_dispatch::~event_dispatch() { delete state_; }

std::uint64_t event_dispatch::begin_scan()
{
    return begin_scan(state_->next_token());
}

std::uint64_t event_dispatch::begin_scan(std::uint64_t token)
{
    if (token == 0) return 0;
    state_->previous_scan_generation = state_->scan_generation;
    state_->scan_generation = token;
    state_->active_scan_token = token;
    state_->token_counter = std::max(state_->token_counter, token);
    return token;
}

std::uint64_t event_dispatch::begin_pairing(std::string_view address)
{
    return begin_pairing(state_->next_token(), address);
}

std::uint64_t event_dispatch::begin_pairing(std::uint64_t token, std::string_view address)
{
    if (token == 0) return 0;
    state_->previous_pair_generation = state_->pair_generation;
    state_->pair_generation = token;
    state_->active_pair_token = token;
    state_->token_counter = std::max(state_->token_counter, token);
    (void)address;
    return token;
}

std::uint64_t event_dispatch::begin_connection(std::string_view address, bool automatic)
{
    return begin_connection(state_->next_token(), address, automatic);
}

std::uint64_t event_dispatch::begin_connection(std::uint64_t token, std::string_view address, bool automatic)
{
    if (token == 0) return 0;
    state_->previous_connect_generation = state_->connect_generation;
    state_->connect_generation = token;
    state_->active_connection_token = token;
    state_->token_counter = std::max(state_->token_counter, token);
    state_->connection_address.assign(address.data(), address.size());
    state_->connection_automatic = automatic;
    return state_->active_connection_token;
}

std::uint64_t event_dispatch::active_scan_token() const
{
    return state_->active_scan_token;
}

std::uint64_t event_dispatch::active_pair_token() const
{
    return state_->active_pair_token;
}

std::uint64_t event_dispatch::active_connection_token() const
{
    return state_->active_connection_token;
}

bool event_dispatch::publish_scan_started(std::uint64_t token)
{
    if (token != state_->active_scan_token || token == 0) {
        return false;
    }
    State::QueuedEvent evt;
    evt.event.kind = ble_event_kind::scan_started;
    evt.event.token = token;
    evt.scan_gen = state_->scan_generation;
    return state_->enqueue(evt);
}

bool event_dispatch::publish_scan_result(std::uint64_t token, const device &item)
{
    if (token != state_->active_scan_token || token == 0) {
        return false;
    }
    State::QueuedEvent evt;
    evt.event.kind = ble_event_kind::scan_result;
    evt.event.token = token;
    evt.event.device_record = item;
    evt.scan_gen = state_->scan_generation;
    return state_->enqueue(evt);
}

bool event_dispatch::publish_scan_finished(std::uint64_t token, notice outcome)
{
    if (token != state_->active_scan_token || token == 0) {
        return false;
    }
    State::QueuedEvent evt;
    evt.event.kind = ble_event_kind::scan_finished;
    evt.event.token = token;
    evt.event.scan_outcome = outcome;
    evt.scan_gen = state_->scan_generation;
    return state_->enqueue(evt);
}

bool event_dispatch::publish_auth_request(std::uint64_t token, auth_request_kind kind,
                                          std::uint32_t passkey)
{
    if (token != state_->active_pair_token || token == 0) {
        return false;
    }
    State::QueuedEvent evt;
    evt.event.kind = ble_event_kind::auth_request;
    evt.event.token = token;
    evt.event.auth_kind = kind;
    evt.event.passkey = passkey;
    evt.pair_gen = state_->pair_generation;
    return state_->enqueue(evt);
}

bool event_dispatch::publish_pair_finished(std::uint64_t token, pair_outcome outcome)
{
    if (token != state_->active_pair_token || token == 0) {
        return false;
    }
    State::QueuedEvent evt;
    evt.event.kind = ble_event_kind::pair_finished;
    evt.event.token = token;
    evt.event.pair_result = outcome;
    evt.pair_gen = state_->pair_generation;
    return state_->enqueue(evt);
}

bool event_dispatch::publish_connected(std::uint64_t token)
{
    if (token != state_->active_connection_token || token == 0) {
        return false;
    }
    State::QueuedEvent evt;
    evt.event.kind = ble_event_kind::connected;
    evt.event.token = token;
    evt.event.address = state_->connection_address;
    evt.event.automatic = state_->connection_automatic;
    evt.connect_gen = state_->connect_generation;
    return state_->enqueue(evt);
}

bool event_dispatch::publish_disconnected(std::uint64_t token)
{
    if (token != state_->active_connection_token || token == 0) {
        return false;
    }
    State::QueuedEvent evt;
    evt.event.kind = ble_event_kind::disconnected;
    evt.event.token = token;
    evt.event.address = state_->connection_address;
    evt.event.automatic = state_->connection_automatic;
    evt.connect_gen = state_->connect_generation;
    return state_->enqueue(evt);
}

std::size_t event_dispatch::pending() const
{
    return state_->count;
}

std::size_t event_dispatch::capacity() const
{
    return k_max_pending_events;
}

std::size_t event_dispatch::drain(ble_event *out, std::size_t limit)
{
    if (limit == 0 || out == nullptr) {
        return 0;
    }
    std::size_t written = 0;
    while (written < limit && state_->count > 0) {
        ble_event evt;
        state_->dequeue(evt);
        out[written] = evt;
        ++written;
    }
    return written;
}

std::size_t event_dispatch::drop_stale()
{
    return state_->drop_stale_generations();
}

void event_dispatch::reset()
{
    state_->head = 0;
    state_->tail = 0;
    state_->count = 0;
    state_->active_scan_token = 0;
    state_->active_pair_token = 0;
    state_->active_connection_token = 0;
    state_->scan_generation = 0;
    state_->pair_generation = 0;
    state_->connect_generation = 0;
    state_->previous_scan_generation = 0;
    state_->previous_pair_generation = 0;
    state_->previous_connect_generation = 0;
    state_->connection_address.clear();
    state_->connection_automatic = false;
}

std::string event_dispatch::event_summary(const ble_event &event)
{
    std::string out;
    out.reserve(80);
    switch (event.kind) {
    case ble_event_kind::scan_started:
        out = "BLE scan started";
        break;
    case ble_event_kind::scan_result: {
        out = "BLE scan result: ";
        out += sanitize_name(event.device_record.name.data(),
                             event.device_record.name.size());
        out += " ";
        out += event.device_record.address;
        out += " ";
        out += kind_label(event.device_record.kind);
        out += " RSSI=";
        out += std::to_string(event.device_record.rssi);
        break;
    }
    case ble_event_kind::scan_finished:
        out = "BLE scan finished: ";
        switch (event.scan_outcome) {
        case notice::empty: out += "empty"; break;
        case notice::failed: out += "failed"; break;
        case notice::timed_out: out += "timeout"; break;
        case notice::cancelled: out += "cancelled"; break;
        default: out += "none"; break;
        }
        break;
    case ble_event_kind::auth_request:
        out = "BLE auth request: ";
        if (event.auth_kind == auth_request_kind::passkey) {
            out += "passkey=";
            out += mask_passkey(event.passkey);
        } else if (event.auth_kind == auth_request_kind::numeric_compare) {
            out += "numeric_compare";
        } else {
            out += "confirm";
        }
        break;
    case ble_event_kind::pair_finished:
        out = "BLE pair finished: ";
        switch (event.pair_result) {
        case pair_outcome::bonded: out += "bonded"; break;
        case pair_outcome::rejected: out += "rejected"; break;
        case pair_outcome::cancelled: out += "cancelled"; break;
        case pair_outcome::timed_out: out += "timeout"; break;
        case pair_outcome::failed: out += "failed"; break;
        }
        break;
    case ble_event_kind::connected:
        out = "BLE connected: ";
        out += event.address;
        if (event.automatic) out += " (auto)";
        break;
    case ble_event_kind::disconnected:
        out = "BLE disconnected: ";
        out += event.address;
        if (event.automatic) out += " (auto)";
        break;
    }
    return out;
}

} // namespace cyberdeck_ble
