#pragma once

#include <cstddef>
#include <cstdint>

/* Host-side contract for the IP_EVENT producer/consumer split.
 *
 * This is deliberately a test contract: the firmware implementation must not
 * expose event_data, s_cfg, SD, SNTP, or UI work from the GOT_IP producer.
 */
namespace cyberdeck_wifi_test {

struct got_ip_event_data {
    std::uint32_t ip;
};

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

static_assert(sizeof(ip_snapshot) <= 128, "GOT_IP queue item must remain a bounded lightweight snapshot");

enum class action_kind { none, persist, start_sntp, notify_ui, lost_ip };

struct action {
    action_kind kind;
    ip_snapshot snapshot;
};

class event_dispatch {
public:
    static constexpr std::size_t queue_capacity = 4;

    bool initialize();
    bool initialized() const;

    /* Producer-side operation. It copies all data it needs and only queues. */
    void on_connected(std::uint64_t token, const char *ssid);
    bool on_got_ip(const got_ip_event_data &event_data, const connection_config &config);
    void on_lost_ip(std::uint64_t token, const char *ssid);

    bool retry_pending();
    std::size_t pending() const;
    action drain_one();

    /* A drained action remains owned by the dispatcher until the consumer
     * explicitly acknowledges this exact action.  This declaration is part
     * of the host contract even while the production seam is being completed.
     */
    bool acknowledge(const action &);
};

} // namespace cyberdeck_wifi_test
