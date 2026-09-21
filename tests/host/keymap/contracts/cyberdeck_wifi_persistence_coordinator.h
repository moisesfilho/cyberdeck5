#pragma once

#include "features/wifi/cyberdeck_wifi_event_dispatch.h"
#include "features/wifi/cyberdeck_wifi_persistence_queue.h"

namespace cyberdeck_wifi_test {

/* Public seam required by the Wi-Fi worker contract.  The production adapter
 * is intentionally absent in this TDD change; these tests must not reach into
 * wifi_mgr.cpp's local worker variables. */
class storage_sink {
public:
    virtual ~storage_sink() = default;
    virtual bool persist(const ip_snapshot &snapshot) = 0;
};

class retry_scheduler {
public:
    virtual ~retry_scheduler() = default;
    virtual void arm() = 0;
    virtual void cancel() = 0;
};

class persistence_coordinator {
public:
    persistence_coordinator(event_dispatch &producer,
                            persistence_queue &persistence,
                            storage_sink &storage,
                            retry_scheduler &scheduler);

    /* One worker step: move one producer action into persistence ownership. */
    bool pump_producer();
    /* One worker step: persist the owned item or arm one coalesced retry. */
    bool pump_persistence();
    /* Called by the scheduler only after a backoff has elapsed. */
    void on_retry_timer();

    /* Rollback/disconnect invalidate both ownership slots and queued retries. */
    void rollback();
    /* Invalidate the disconnected generation before allowing reconnect. */
    void disconnect(std::uint64_t invalidated_token);
    void teardown();

    bool producer_pending() const;
    bool persist_pending() const;
    bool retry_pending() const;
};

} // namespace cyberdeck_wifi_test
