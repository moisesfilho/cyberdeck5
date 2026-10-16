#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cyberdeck_wifi_survey {

inline constexpr std::size_t max_networks = 32;
inline constexpr std::size_t max_scans = 6;
inline constexpr std::uint32_t default_duration_seconds = 30;
inline constexpr std::uint32_t min_duration_seconds = 10;
inline constexpr std::uint32_t max_duration_seconds = 120;
inline constexpr std::uint32_t scan_interval_seconds = 5;
inline constexpr std::uint8_t unknown_value = 0xff;

struct observation {
    char ssid[33]{};
    bool ssid_known{false};
    char bssid[18]{};
    std::uint8_t channel{0};
    std::uint8_t security{unknown_value};
    std::int8_t rssi{0};
};

struct network {
    observation latest{};
    std::uint8_t sample_count{0};
    std::int8_t minimum_rssi{0};
    std::int8_t maximum_rssi{0};
    std::int32_t rssi_sum{0};
};

struct scan_request {
    std::uint64_t session_token{0};
    std::uint64_t request_token{0};
};

class session {
public:
    bool start(std::uint32_t duration_seconds, std::uint64_t now_ms);
    bool cancel();
    bool expire(std::uint64_t now_ms);
    bool active() const { return active_; }
    bool finished() const { return started_ && !active_; }
    std::uint32_t duration_seconds() const { return duration_seconds_; }
    std::uint64_t token() const { return session_token_; }
    std::size_t scan_count() const { return scan_count_; }
    std::uint32_t effective_interval_seconds() const;
    std::size_t size() const { return size_; }
    const network *at(std::size_t index) const;
    scan_request due_scan(std::uint64_t now_ms);
    bool complete_scan(scan_request request, const observation *items, std::size_t count,
                       std::uint64_t now_ms);
    void fail_scan(scan_request request);
    void clear();
    std::string render() const;
    std::string export_text(bool masked) const;

private:
    void add(const observation &item);
    void sort();

    std::array<network, max_networks> networks_{};
    std::size_t size_{0};
    std::size_t scan_count_{0};
    std::uint32_t duration_seconds_{default_duration_seconds};
    std::uint64_t session_token_{0};
    std::uint64_t next_token_{0};
    std::uint64_t ends_at_ms_{0};
    std::uint64_t next_scan_at_ms_{0};
    std::uint64_t pending_request_token_{0};
    bool active_{false};
    bool started_{false};
    bool cancelled_{false};
};

std::string mask_identifier(const char *value);

} // namespace cyberdeck_wifi_survey
