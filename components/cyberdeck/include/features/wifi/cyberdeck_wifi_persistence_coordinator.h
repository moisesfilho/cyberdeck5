#pragma once

#include "features/wifi/cyberdeck_wifi_event_dispatch.h"
#include "features/wifi/cyberdeck_wifi_persistence_queue.h"
#include <cstdint>

namespace cyberdeck_wifi_test {

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

/* Serialized owner of the two bounded persistence hand-off slots. */
class persistence_coordinator {
public:
    persistence_coordinator(event_dispatch &producer,
                            persistence_queue &persistence,
                            storage_sink &storage,
                            retry_scheduler &scheduler);

    bool initialize();
    void on_connected(std::uint64_t token, const char *ssid);
    bool on_got_ip(const got_ip_event_data &event_data, const connection_config &config);
    void on_lost_ip(std::uint64_t token, const char *ssid);

    bool pump_producer();
    bool pump_persistence();
    void on_retry_timer();
    void rollback();
    void disconnect(std::uint64_t invalidated_token);
    void teardown();

    bool producer_pending() const;
    bool persist_pending() const;
    bool retry_pending() const;

private:
    struct state {
        event_dispatch *producer = nullptr;
        persistence_queue *persistence = nullptr;
        storage_sink *storage = nullptr;
        retry_scheduler *scheduler = nullptr;
        action producer_slot{};
        action persist_slot{};
        bool has_producer = false;
        bool has_persist = false;
        bool retry = false;
        bool torn_down = false;
    };
    state m_state;
};

} // namespace cyberdeck_wifi_test
