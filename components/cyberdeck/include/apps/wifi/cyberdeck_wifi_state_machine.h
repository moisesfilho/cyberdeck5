#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cyberdeck_wifi {
enum class screen { search, saved, password, connecting, forget_confirmation, idle };
enum class key { up, down, enter, escape, backspace };
enum class connection_event { connected, has_ip, failed };
enum class action_kind { connect, persist, forget, cancel_scan, cancel_connect, timeout };
struct access_point { std::string ssid; int rssi; bool open; bool saved; };
struct action { action_kind kind; std::string ssid; std::string password; std::uint64_t token; };

class state_machine {
public:
    state_machine();
    ~state_machine();
    state_machine(const state_machine&) = delete;
    state_machine& operator=(const state_machine&) = delete;
    state_machine(state_machine&&) noexcept;
    state_machine& operator=(state_machine&&) noexcept;
    void begin_search();
    void begin_saved();
    void set_saved_networks(const std::vector<std::string>&);
    void scan_complete(std::uint64_t, const std::vector<access_point>&);
    void connection_callback(std::uint64_t, connection_event);
    void advance_time(std::uint32_t);
    void press(key);
    void type_password(const std::string&);
    void begin_connection(const std::string&, const std::string&);
    void cancel_connection();
    screen current_screen() const;
    std::size_t selected_index() const;
    std::vector<access_point> search_results() const;
    std::vector<std::string> saved_networks() const;
    std::string password_display() const;
    std::vector<action> take_actions();
    std::uint64_t active_scan_token() const;
    std::uint64_t active_connection_token() const;
private:
    struct storage;
    storage *data_;
};
}
