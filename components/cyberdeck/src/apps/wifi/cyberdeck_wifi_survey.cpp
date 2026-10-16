#include "apps/wifi/cyberdeck_wifi_survey.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cyberdeck_wifi_survey {
namespace {
constexpr std::size_t export_limit = 8192;

const char *security_name(std::uint8_t value)
{
    switch (value) {
    case 0: return "open";
    case 1: return "wep";
    case 2: return "wpa-psk";
    case 3: return "wpa2-psk";
    case 4: return "wpa-wpa2-psk";
    case 5: return "wpa2-enterprise";
    case 6: return "wpa3-psk";
    case 7: return "wpa2-wpa3-psk";
    case 8: return "wapi-psk";
    case 9: return "owe";
    case 10: return "wpa3-enterprise-192";
    case 11: return "wpa3-extended-psk";
    case 12: return "wpa3-extended-psk-mixed";
    case 13: return "dpp";
    case 14: return "wpa3-enterprise";
    case 15: return "wpa2-wpa3-enterprise";
    case 16: return "wpa-enterprise";
    default: return "unknown";
    }
}

bool same_network(const observation &left, const observation &right)
{
    if (left.bssid[0] != '\0' || right.bssid[0] != '\0')
        return left.bssid[0] != '\0' && right.bssid[0] != '\0' &&
               std::strncmp(left.bssid, right.bssid, sizeof(left.bssid)) == 0;
    return left.ssid_known == right.ssid_known && left.channel == right.channel &&
           std::strncmp(left.ssid, right.ssid, sizeof(left.ssid)) == 0;
}

const char *bssid_value(const char *value)
{
    return value == nullptr || value[0] == '\0' ? "<unknown>" : value;
}

observation sanitize(const observation &source)
{
    observation result = source;
    result.ssid[sizeof(result.ssid) - 1] = '\0';
    result.bssid[sizeof(result.bssid) - 1] = '\0';
    for (char *field : {result.ssid, result.bssid}) {
        for (std::size_t i = 0; field[i] != '\0'; ++i) {
            const unsigned char byte = static_cast<unsigned char>(field[i]);
            if (byte < 0x20 || byte > 0x7e || byte == '|' || byte == '\n' || byte == '\r') field[i] = '?';
        }
    }
    for (char &byte : result.bssid) {
        if (byte >= 'A' && byte <= 'F') byte = static_cast<char>(byte - 'A' + 'a');
    }
    if (result.ssid[0] == '\0') result.ssid_known = false;
    return result;
}

}

std::string mask_identifier(const char *value)
{
    if (value == nullptr || value[0] == '\0') return "<hidden>";
    std::string result(value);
    if (result == "<hidden>" || result == "<unknown>") return result;
    for (char &byte : result) if (byte != ':') byte = '*';
    return result;
}

bool session::start(std::uint32_t duration, std::uint64_t now_ms)
{
    if (active_ || duration < min_duration_seconds || duration > max_duration_seconds) return false;
    const std::uint64_t duration_ms = static_cast<std::uint64_t>(duration) * 1000;
    if (now_ms > UINT64_MAX - duration_ms) return false;
    clear();
    duration_seconds_ = duration;
    session_token_ = ++next_token_;
    if (session_token_ == 0) session_token_ = ++next_token_;
    ends_at_ms_ = now_ms + static_cast<std::uint64_t>(duration) * 1000;
    next_scan_at_ms_ = now_ms;
    active_ = true;
    started_ = true;
    cancelled_ = false;
    return true;
}

std::uint32_t session::effective_interval_seconds() const
{
    const std::uint32_t spread =
        (duration_seconds_ + static_cast<std::uint32_t>(max_scans) - 1) /
        static_cast<std::uint32_t>(max_scans);
    return std::max(scan_interval_seconds, spread);
}

bool session::cancel()
{
    if (!active_) return false;
    active_ = false;
    pending_request_token_ = 0;
    ++next_token_;
    cancelled_ = true;
    return true;
}

bool session::expire(std::uint64_t now_ms)
{
    if (!active_ || now_ms < ends_at_ms_) return false;
    active_ = false;
    pending_request_token_ = 0;
    ++next_token_;
    return true;
}

void session::clear()
{
    networks_ = {};
    size_ = 0;
    scan_count_ = 0;
    pending_request_token_ = 0;
    session_token_ = 0;
    ends_at_ms_ = 0;
    next_scan_at_ms_ = 0;
    active_ = false;
    started_ = false;
    cancelled_ = false;
}

const network *session::at(std::size_t index) const
{
    return index < size_ ? &networks_[index] : nullptr;
}

scan_request session::due_scan(std::uint64_t now_ms)
{
    if (!active_) return {};
    if (now_ms >= ends_at_ms_) {
        (void)expire(now_ms);
        return {};
    }
    if (pending_request_token_ != 0) return {};
    if (scan_count_ >= max_scans || now_ms < next_scan_at_ms_) return {};
    ++scan_count_;
    const std::uint64_t interval_ms = static_cast<std::uint64_t>(effective_interval_seconds()) * 1000;
    next_scan_at_ms_ = now_ms > UINT64_MAX - interval_ms ? ends_at_ms_ : now_ms + interval_ms;
    pending_request_token_ = ++next_token_;
    if (pending_request_token_ == 0) pending_request_token_ = ++next_token_;
    return {session_token_, pending_request_token_};
}

bool session::complete_scan(scan_request request, const observation *items, std::size_t count,
                            std::uint64_t now_ms)
{
    if (!active_ || request.session_token == 0 || request.session_token != session_token_ ||
        request.request_token == 0 || request.request_token != pending_request_token_) return false;
    if (now_ms >= ends_at_ms_) {
        (void)expire(now_ms);
        return false;
    }
    pending_request_token_ = 0;
    if (items != nullptr) {
        const std::size_t bounded = std::min(count, std::size_t{16});
        std::array<observation, 16> unique{};
        std::size_t unique_count = 0;
        for (std::size_t i = 0; i < bounded; ++i) {
            const observation item = sanitize(items[i]);
            std::size_t duplicate = unique_count;
            for (std::size_t j = 0; j < unique_count; ++j) {
                if (same_network(unique[j], item)) {
                    duplicate = j;
                    break;
                }
            }
            if (duplicate == unique_count) unique[unique_count++] = item;
            else if (item.rssi > unique[duplicate].rssi) unique[duplicate] = item;
        }
        for (std::size_t i = 0; i < unique_count; ++i) add(unique[i]);
    }
    sort();
    return true;
}

void session::fail_scan(scan_request request)
{
    if (active_ && request.session_token == session_token_ && request.request_token == pending_request_token_)
        pending_request_token_ = 0;
}

void session::add(const observation &item)
{
    std::size_t index = size_;
    for (std::size_t i = 0; i < size_; ++i) {
        if (same_network(networks_[i].latest, item)) {
            index = i;
            break;
        }
    }
    if (index == size_) {
        if (size_ == networks_.size()) {
            std::size_t weakest = 0;
            for (std::size_t i = 1; i < size_; ++i) {
                if (networks_[i].latest.rssi < networks_[weakest].latest.rssi ||
                    (networks_[i].latest.rssi == networks_[weakest].latest.rssi &&
                     std::strcmp(networks_[i].latest.bssid, networks_[weakest].latest.bssid) > 0)) weakest = i;
            }
            if (item.rssi < networks_[weakest].latest.rssi ||
                (item.rssi == networks_[weakest].latest.rssi &&
                 std::strcmp(item.bssid, networks_[weakest].latest.bssid) >= 0)) return;
            index = weakest;
        } else {
            ++size_;
        }
        networks_[index] = {};
        networks_[index].latest = item;
        networks_[index].sample_count = 1;
        networks_[index].minimum_rssi = item.rssi;
        networks_[index].maximum_rssi = item.rssi;
        networks_[index].rssi_sum = item.rssi;
        return;
    }
    network &entry = networks_[index];
    if (entry.sample_count != UINT8_MAX) ++entry.sample_count;
    entry.minimum_rssi = std::min(entry.minimum_rssi, item.rssi);
    entry.maximum_rssi = std::max(entry.maximum_rssi, item.rssi);
    entry.rssi_sum += item.rssi;
    if (item.rssi > entry.latest.rssi) entry.latest = item;
}

void session::sort()
{
    std::stable_sort(networks_.begin(), networks_.begin() + size_, [](const network &left, const network &right) {
        if (left.latest.rssi != right.latest.rssi) return left.latest.rssi > right.latest.rssi;
        const int bssid_order = std::strcmp(left.latest.bssid, right.latest.bssid);
        if (bssid_order != 0) return bssid_order < 0;
        const int ssid_order = std::strcmp(left.latest.ssid, right.latest.ssid);
        if (ssid_order != 0) return ssid_order < 0;
        return left.latest.channel < right.latest.channel;
    });
}

std::string session::render() const
{
    std::string out = "wifi survey: ";
    out += active_ ? "sampling" : (cancelled_ ? "cancelled" : (started_ ? "complete" : "idle"));
    char line[180];
    char channel[12];
    for (std::size_t i = 0; i < size_; ++i) {
        const network &item = networks_[i];
        const char *ssid = item.latest.ssid_known ? item.latest.ssid : "<hidden>";
        const int average = item.sample_count == 0 ? 0 : item.rssi_sum / item.sample_count;
        if (item.latest.channel == 0) std::snprintf(channel, sizeof(channel), "unknown");
        else std::snprintf(channel, sizeof(channel), "%u", static_cast<unsigned>(item.latest.channel));
        const int written = std::snprintf(line, sizeof(line),
            "\n%s | %s | ch=%s | security=%s | rssi=%d dBm | samples=%u min=%d avg=%d max=%d",
            ssid, bssid_value(item.latest.bssid), channel, security_name(item.latest.security),
            static_cast<int>(item.latest.rssi), static_cast<unsigned>(item.sample_count),
            static_cast<int>(item.minimum_rssi), average, static_cast<int>(item.maximum_rssi));
        if (written > 0) out.append(line, std::min<std::size_t>(written, sizeof(line) - 1));
    }
    out += "\n";
    return out;
}

std::string session::export_text(bool masked) const
{
    std::string out = "version=1\nformat=cyberdeck-wifi-survey\n";
    out += "session=" + std::to_string(session_token_) + "\nduration_seconds=" +
           std::to_string(duration_seconds_) + "\nscans=" + std::to_string(scan_count_) +
           "\nnetworks=" + std::to_string(size_) + "\n";
    char line[192];
    for (std::size_t i = 0; i < size_; ++i) {
        const network &item = networks_[i];
        const char *ssid = item.latest.ssid_known ? item.latest.ssid : "<hidden>";
        std::string ssid_value = masked ? mask_identifier(ssid) : ssid;
        if (!masked) {
            for (char &byte : ssid_value) if (byte == '|' || byte == '\n' || byte == '\r') byte = '?';
        }
        const std::string bssid = bssid_value(item.latest.bssid);
        const std::string masked_bssid = masked ? mask_identifier(bssid.c_str()) : bssid;
        const int average = item.sample_count == 0 ? 0 : item.rssi_sum / item.sample_count;
        const int written = std::snprintf(line, sizeof(line),
            "ap=%s|%s|%s|%s|%d|%u|%d|%d|%d\n", ssid_value.c_str(), masked_bssid.c_str(),
            item.latest.channel == 0 ? "unknown" : std::to_string(item.latest.channel).c_str(),
            security_name(item.latest.security), static_cast<int>(item.latest.rssi),
            static_cast<unsigned>(item.sample_count), static_cast<int>(item.minimum_rssi), average,
            static_cast<int>(item.maximum_rssi));
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof(line) ||
            out.size() + static_cast<std::size_t>(written) > export_limit) return {};
        out.append(line, static_cast<std::size_t>(written));
    }
    return out.size() <= export_limit ? out : std::string{};
}

} // namespace cyberdeck_wifi_survey
