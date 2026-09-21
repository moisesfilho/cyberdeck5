#include "cyberdeck_wifi_state_machine.h"

#include <algorithm>
#include <cstring>

namespace cyberdeck_wifi {
namespace {
constexpr std::uint32_t connect_timeout_ms = 15000;
}

class state_machine_impl {
public:
    ~state_machine_impl() {
        clear_secret();
        if (!connection_password.empty()) {
            std::fill(connection_password.begin(), connection_password.end(), '\0');
        }
        for (auto &item : pending) {
            std::fill(item.password.begin(), item.password.end(), '\0');
        }
    }
    screen view = screen::idle;
    std::size_t selected = 0;
    std::vector<access_point> aps;
    std::vector<std::string> saved;
    std::string password;
    std::string connection_password;
    std::string selected_ssid;
    std::vector<action> pending;
    // These are deliberately different token domains.  A callback must match
    // the token of the operation that is currently active; a larger token is
    // not evidence that it belongs to the current operation.
    std::uint64_t next_scan_token = 0;
    std::uint64_t active_scan_token = 0;
    std::uint64_t next_connection_token = 0;
    std::uint64_t active_connection_token = 0;
    std::uint64_t next_operation_token = 0;
    std::uint64_t active_operation_token = 0;
    std::uint32_t elapsed = 0;
    bool connected = false;
    bool has_ip = false;

    void clear_secret() {
        if (!password.empty()) {
            std::fill(password.begin(), password.end(), '\0');
            password.clear();
        }
    }

    void invalidate_connection() {
        active_connection_token = 0;
        elapsed = 0;
        connected = false;
        has_ip = false;
        clear_secret();
        if (!connection_password.empty()) { std::fill(connection_password.begin(), connection_password.end(), '\0'); connection_password.clear(); }
    }

    void connect(const std::string &ssid, const std::string &pwd) {
        active_scan_token = 0;
        selected_ssid = ssid;
        if (!connection_password.empty()) { std::fill(connection_password.begin(), connection_password.end(), '\0'); connection_password.clear(); }
        connection_password = pwd;
        active_connection_token = ++next_connection_token;
        elapsed = 0;
        connected = false;
        has_ip = false;
        pending.push_back({action_kind::connect, ssid, pwd, active_connection_token});
        clear_secret();
        view = screen::connecting;
    }

    void finish_if_ready() {
        if (!connected || !has_ip) return;
        pending.push_back({action_kind::persist, selected_ssid, connection_password,
                           active_connection_token});
        invalidate_connection();
        view = screen::idle;
    }
};

/* Keep the public type deliberately small and ABI-free. */
struct state_machine::storage { state_machine_impl value; };

state_machine::state_machine() : data_(new storage) {}
state_machine::~state_machine() { delete data_; }
state_machine::state_machine(state_machine&& other) noexcept : data_(other.data_) { other.data_ = nullptr; }
state_machine& state_machine::operator=(state_machine&& other) noexcept {
    if (this != &other) { delete data_; data_ = other.data_; other.data_ = nullptr; }
    return *this;
}

void state_machine::begin_search() {
    if (!data_ || data_->value.view == screen::password ||
        data_->value.view == screen::connecting) return;
    auto &m = data_->value;

    /* A search replaces only the scan currently shown.  A connection attempt
     * is deliberately not replaceable from here: its token and the two
     * lifecycle flags must remain valid until it succeeds, fails, times out,
     * or is explicitly cancelled. */
    if (m.view == screen::search) {
        m.pending.push_back({action_kind::cancel_scan, "", "", m.active_scan_token});
        m.active_scan_token = 0;
    }
    m.view = screen::search;
    m.selected = 0;
    m.aps.clear();
    /* Scan tokens have their own sequence.  A connection attempt must not
     * advance it: scan completions can legitimately arrive after a previous
     * connection has finished, while completions from an older scan must
     * still be rejected by the scan sequence. */
    m.active_scan_token = ++m.next_scan_token;
}

void state_machine::begin_saved() {
    if (!data_ || data_->value.view == screen::password) return;
    auto &m = data_->value;

    /* The saved-networks view has the same replacement semantics as search.
     * Cancelling first invalidates the token, so a late callback from the
     * previous connection cannot affect this view or a later request. */
    if (m.view == screen::connecting) {
        m.pending.push_back({action_kind::cancel_connect, m.selected_ssid, "", m.active_connection_token});
        m.invalidate_connection();
    } else if (m.view == screen::search) {
        m.pending.push_back({action_kind::cancel_scan, "", "", m.active_scan_token});
        m.active_scan_token = 0;
    }
    m.view = screen::saved;
    m.selected = 0;
}

void state_machine::set_saved_networks(const std::vector<std::string>& ssids) {
    if (!data_) return;
    data_->value.saved = ssids;
}

void state_machine::scan_complete(std::uint64_t token, const std::vector<access_point>& input) {
    if (!data_ || data_->value.view != screen::search ||
        token != data_->value.active_scan_token) return;
    auto &m = data_->value;
    m.aps.clear();
    for (const auto &candidate : input) {
        if (candidate.ssid.empty()) continue;
        auto found = std::find_if(m.aps.begin(), m.aps.end(), [&](const access_point &ap) { return ap.ssid == candidate.ssid; });
        if (found == m.aps.end()) {
            access_point ap = candidate;
            ap.saved = ap.saved || std::find(m.saved.begin(), m.saved.end(), ap.ssid) != m.saved.end();
            m.aps.push_back(ap);
        } else if (candidate.rssi > found->rssi) {
            found->rssi = candidate.rssi;
            found->open = candidate.open;
        }
    }
    std::sort(m.aps.begin(), m.aps.end(), [](const access_point &a, const access_point &b) {
        if (a.rssi != b.rssi) return a.rssi > b.rssi;
        return a.ssid < b.ssid;
    });
    m.selected = 0;
}

void state_machine::connection_callback(std::uint64_t token, connection_event event) {
    if (!data_ || data_->value.view != screen::connecting ||
        token != data_->value.active_connection_token) return;
    auto &m = data_->value;
    if (event == connection_event::failed) {
        m.invalidate_connection();
        m.view = screen::idle;
    } else if (event == connection_event::connected) {
        m.connected = true;
        m.finish_if_ready();
    } else if (event == connection_event::has_ip) {
        m.has_ip = true;
        m.finish_if_ready();
    }
}

void state_machine::advance_time(std::uint32_t milliseconds) {
    if (!data_ || data_->value.view != screen::connecting) return;
    auto &m = data_->value;
    if (m.elapsed >= connect_timeout_ms || milliseconds >= connect_timeout_ms - m.elapsed) {
        m.pending.push_back({action_kind::timeout, m.selected_ssid, "", m.active_connection_token});
        m.pending.push_back({action_kind::cancel_connect, m.selected_ssid, "", m.active_connection_token});
        m.invalidate_connection();
        m.view = screen::idle;
    } else m.elapsed += milliseconds;
}

void state_machine::press(key input) {
    if (!data_) return;
    auto &m = data_->value;
    if (m.view == screen::search) {
        if (input == key::up && m.selected) --m.selected;
        else if (input == key::down && m.selected + 1 < m.aps.size()) ++m.selected;
        else if (input == key::escape) {
            m.pending.push_back({action_kind::cancel_scan, "", "", m.active_scan_token});
            m.active_scan_token = 0;
            m.view = screen::idle;
        }
        else if (input == key::enter && !m.aps.empty()) {
            const access_point ap = m.aps[m.selected];
            if (ap.open || ap.saved) {
                std::string pwd;
                m.connect(ap.ssid, pwd);
            } else {
                m.active_scan_token = 0;
                m.selected_ssid = ap.ssid;
                m.clear_secret();
                m.view = screen::password;
            }
        }
    } else if (m.view == screen::password) {
        if (input == key::escape) { m.clear_secret(); m.view = screen::idle; }
        else if (input == key::backspace && !m.password.empty()) { m.password.back() = '\0'; m.password.pop_back(); }
        else if (input == key::enter) m.connect(m.selected_ssid, m.password);
    } else if (m.view == screen::connecting && input == key::escape) {
        m.pending.push_back({action_kind::cancel_connect, m.selected_ssid, "", m.active_connection_token});
        m.invalidate_connection(); m.view = screen::idle;
    } else if (m.view == screen::saved) {
        if (input == key::up && m.selected) --m.selected;
        else if (input == key::down && m.selected + 1 < m.saved.size()) ++m.selected;
        else if (input == key::enter && !m.saved.empty()) m.view = screen::forget_confirmation;
    } else if (m.view == screen::forget_confirmation) {
        if (input == key::escape) m.view = screen::saved;
        else if (input == key::enter && m.selected < m.saved.size()) {
            const std::string ssid = m.saved[m.selected];
            m.active_operation_token = ++m.next_operation_token;
            m.pending.push_back({action_kind::forget, ssid, "", m.active_operation_token});
            m.saved.erase(m.saved.begin() + m.selected);
            if (m.selected >= m.saved.size() && m.selected) --m.selected;
            m.view = screen::saved;
        }
    }
}

void state_machine::type_password(const std::string& text) {
    if (data_ && data_->value.view == screen::password) data_->value.password += text;
}

void state_machine::begin_connection(const std::string &ssid, const std::string &password)
{
    if (!data_ || ssid.empty() || data_->value.view == screen::connecting) return;
    data_->value.connect(ssid, password);
}

void state_machine::cancel_connection()
{
    if (!data_ || data_->value.view != screen::connecting) return;
    auto &m = data_->value;
    m.pending.push_back({action_kind::cancel_connect, m.selected_ssid, "", m.active_connection_token});
    m.invalidate_connection();
    m.view = screen::idle;
}

screen state_machine::current_screen() const { return data_ ? data_->value.view : screen::idle; }
std::size_t state_machine::selected_index() const { return data_ ? data_->value.selected : 0; }
std::vector<access_point> state_machine::search_results() const { return data_ ? data_->value.aps : std::vector<access_point>{}; }
std::vector<std::string> state_machine::saved_networks() const { return data_ ? data_->value.saved : std::vector<std::string>{}; }
std::string state_machine::password_display() const { return data_ ? std::string(data_->value.password.size(), '*') : ""; }
std::vector<action> state_machine::take_actions() {
    if (!data_) return {};
    auto out = data_->value.pending;
    for (auto &item : data_->value.pending) {
        std::fill(item.password.begin(), item.password.end(), '\0');
    }
    data_->value.pending.clear();
    return out;
}

std::uint64_t state_machine::active_scan_token() const
{
    return data_ ? data_->value.active_scan_token : 0;
}

std::uint64_t state_machine::active_connection_token() const
{
    return data_ ? data_->value.active_connection_token : 0;
}
}
