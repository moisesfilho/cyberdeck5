#pragma once

#include <cstddef>
#include <cstdint>

namespace cyberdeck_wifi_test {

struct got_ip_event_data { std::uint32_t ip; };
struct connection_config {
    std::uint64_t token;
    const char *ssid;
    const char *password;
};
struct ip_snapshot {
    std::uint64_t token;
    std::uint32_t ip;
    char ssid[33];
    char password[65];
};
static_assert(sizeof(ip_snapshot) <= 128, "Wi-Fi event snapshot must stay bounded");

enum class action_kind { none, persist, start_sntp, notify_ui, lost_ip };
struct action { action_kind kind; ip_snapshot snapshot; };

/* Wipes an action, including the password embedded in its snapshot. */
void wipe_action(action &item);

class event_dispatch {
public:
    static constexpr std::size_t queue_capacity = 4;
    bool initialize();
    bool initialized() const;
    void on_connected(std::uint64_t token, const char *ssid);
    bool on_got_ip(const got_ip_event_data &event_data, const connection_config &config);
    void on_lost_ip(std::uint64_t token, const char *ssid);
    bool retry_pending();
    std::size_t pending() const;
    /* Delivery does not release the queue slot. */
    action drain_one();
    bool acknowledge(const action &item);
    bool retry(const action &item);
    /* Drops every queued/in-flight snapshot and invalidates the connection. */
    void wipe();
    /* Drops work and rejects callbacks from this disconnected generation. */
    void invalidate(std::uint64_t token);
    /* wipe() plus makes the dispatcher unusable until initialize(). */
    void teardown();
private:
    bool m_initialized = false;
    std::uint64_t m_active_token = 0;
    std::uint64_t m_generation_floor = 0;
    bool m_has_active_connection = false;
    bool m_bootstrap_consumed = false;
    char m_active_ssid[33] = {};
    action m_queue[queue_capacity] = {};
    std::size_t m_head = 0;
    std::size_t m_tail = 0;
    std::size_t m_count = 0;
    bool m_has_in_flight = false;
};

} // namespace cyberdeck_wifi_test
