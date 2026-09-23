#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cyberdeck_wifi_audit {

inline constexpr std::size_t snapshot_capacity = 8;
inline constexpr std::size_t field_capacity = 32;
inline constexpr std::size_t export_capacity = 2048;
inline constexpr std::uint16_t snapshot_version = 1;

enum class state { unavailable, collecting, ready, error };
enum class command { invalid, audit, export_audit };
enum class result { ignored, accepted, failed };
struct command_line { command kind{command::invalid}; bool confirmed{false}; };
struct association { bool associated{false}; std::string_view ssid{}; std::string_view bssid{}; std::string_view ip{}; };
struct snapshot {
    std::uint16_t version{0}; std::uint64_t token{0}; state status{state::unavailable};
    std::size_t item_count{0}; char ssid[field_capacity]{}; char bssid[field_capacity]{}; char ip[field_capacity]{};
};
struct export_result { bool ok{false}; std::size_t bytes{0}; char path[field_capacity]{}; char data[export_capacity]{}; };

command_line parse_command(std::string_view line);
// Legacy host-contract probe.  It is kept in the contract header only; the
// production audit implementation does not scan or classify sink contents.
inline bool contains_secret(std::string_view text)
{
    return text.find("password") != std::string_view::npos ||
           text.find("passphrase") != std::string_view::npos ||
           text.find("psk") != std::string_view::npos;
}
std::string render_ui(const snapshot &value);
std::string render_log(const snapshot &value);

class audit_controller {
public:
    static constexpr std::size_t queue_capacity = snapshot_capacity;
    audit_controller();
    ~audit_controller();
    audit_controller(const audit_controller &) = delete;
    audit_controller &operator=(const audit_controller &) = delete;
    bool initialize();
    void teardown();
    bool initialized() const;
    state status() const;
    std::uint64_t begin(const association &hint);
    result complete(std::uint64_t token, const association &current, bool worker_error = false);
    bool cancel(std::uint64_t token);
    bool enqueue_export(std::uint64_t token, std::string_view path, bool confirmed);
    export_result drain_export();
    snapshot snapshot_view() const;
    std::size_t pending() const;
private:
    struct implementation;
    implementation *impl_;
};
}

// The host contract used this namespace before the production header existed.
// Keep it as an alias so host tests exercise the production ABI, not a copy.
namespace cyberdeck_wifi_audit_test = cyberdeck_wifi_audit;
