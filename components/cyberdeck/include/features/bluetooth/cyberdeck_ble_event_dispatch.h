#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "features/bluetooth/cyberdeck_ble_types.h"
#include "features/bluetooth/cyberdeck_ble_state_machine.h"

namespace cyberdeck_ble {

/* Bounded hand-off.  Overflow is fail-closed: the newest publication is
 * rejected, the queue never grows past the bound, and nothing is allocated. */
inline constexpr std::size_t k_max_pending_events = 8;

enum class ble_event_kind {
    scan_started,
    scan_result,
    scan_finished,
    auth_request,
    pair_finished,
    connected,
    disconnected,
};

/*
 * A bounded, self-contained snapshot.  There is no pointer, no span and no
 * reference to stack-owned memory, so a snapshot stays valid after the callback
 * returns.
 */
struct ble_event {
    ble_event_kind kind = ble_event_kind::scan_started;
    std::uint64_t token = 0;

    /* scan_result / paired listing */
    device device_record;

    /* scan_finished */
    notice scan_outcome = notice::empty;

    /* auth_request */
    auth_request_kind auth_kind = auth_request_kind::passkey;
    std::uint32_t passkey = 0;

    /* pair_finished */
    pair_outcome pair_result = pair_outcome::bonded;

    /* connected / disconnected */
    std::string address;
    bool automatic = false; /* true for an automatic reconnection attempt */
};

class event_dispatch {
public:
    event_dispatch();
    ~event_dispatch();
    event_dispatch(const event_dispatch &) = delete;
    event_dispatch &operator=(const event_dispatch &) = delete;

    /* ---- generation allocation ------------------------------------------ */
    /* Every begin* invalidates the previous generation of the same kind and
     * returns a strictly increasing, non-zero token. */
    std::uint64_t begin_scan();
    std::uint64_t begin_scan(std::uint64_t token);
    std::uint64_t begin_pairing(std::string_view address);
    std::uint64_t begin_pairing(std::uint64_t token, std::string_view address);
    std::uint64_t begin_connection(std::string_view address, bool automatic);
    std::uint64_t begin_connection(std::uint64_t token, std::string_view address, bool automatic);

    std::uint64_t active_scan_token() const;
    std::uint64_t active_pair_token() const;
    std::uint64_t active_connection_token() const;

    /* ---- publications ---------------------------------------------------- */
    /* Each returns true only when the token matches the active generation and
     * the bounded queue had room.  A stale, zero or unknown token is dropped
     * without being enqueued and without touching the active generation. */
    bool publish_scan_started(std::uint64_t token);
    bool publish_scan_result(std::uint64_t token, const device &item);
    bool publish_scan_finished(std::uint64_t token, notice outcome);
    bool publish_auth_request(std::uint64_t token, auth_request_kind kind,
                              std::uint32_t passkey);
    bool publish_pair_finished(std::uint64_t token, pair_outcome outcome);
    bool publish_connected(std::uint64_t token);
    bool publish_disconnected(std::uint64_t token);

    /* ---- consumption ----------------------------------------------------- */
    std::size_t pending() const;
    std::size_t capacity() const;

    /* Moves at most `limit` events into `out` in publication order.  limit == 0
     * consumes nothing and drops nothing.  Returns the number written. */
    std::size_t drain(ble_event *out, std::size_t limit);

    /* Discards queued events that belong to a superseded generation and
     * returns how many were discarded. */
    std::size_t drop_stale();

    /* Invalidates every generation and empties the queue.  Tokens already
     * handed out can never become valid again. */
    void reset();

    /*
     * The only projection allowed into a log line, a terminal status line or
     * any other diagnostic.  It NEVER contains a passkey, a PIN, a link key or
     * an IRK: an auth_request is described by its kind and a masked value only.
     */
    std::string event_summary(const ble_event &event);

private:
    struct State;
    State *state_;
};

} // namespace cyberdeck_ble
