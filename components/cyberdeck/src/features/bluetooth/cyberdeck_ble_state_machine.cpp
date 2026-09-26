#include "features/bluetooth/cyberdeck_ble_state_machine.h"

#include <algorithm>
#include <string>
#include <vector>

namespace cyberdeck_ble {

struct state_machine::State {
    screen current = screen::idle;
    notice active_notice = notice::none;
    device_list devices_;
    std::vector<device> paired_devices;
    std::vector<action> action_queue;

    std::uint64_t scan_token = 0;
    std::uint64_t pair_token = 0;
    std::uint64_t connect_token = 0;
    std::uint64_t token_counter = 0;

    std::uint32_t scan_deadline = 0;
    std::uint32_t pair_deadline = 0;
    std::uint32_t auth_deadline = 0;
    std::uint32_t connect_deadline = 0;

    std::string pairing_address;
    std::string connecting_address;
    std::string active_reconnect_address;
    bool connection_in_flight = false;
    auth_request_kind pending_auth = auth_request_kind::passkey;
    std::uint32_t displayed_passkey = 0;

    std::uint32_t reconnect_attempts = 0;
    std::uint32_t max_reconnect_attempts = k_max_reconnect_attempts;

    // Track the source of notices to disambiguate scan vs pair/connect
    bool timed_out_from_pairing = false;
    bool timed_out_from_connect = false;
    bool failed_from_pairing = false;
    bool failed_from_connect = false;
    bool cancelled_from_pairing = false;
    bool cancelled_from_connect = false;
    bool rejected_from_pairing = false;
    bool reconnect_gave_up = false;

    std::uint64_t next_token()
    {
        return ++token_counter;
    }

    void clear_deadlines()
    {
        scan_deadline = 0;
        pair_deadline = 0;
        auth_deadline = 0;
        connect_deadline = 0;
    }

    void reset_to_idle()
    {
        current = screen::idle;
        active_notice = notice::none;
        clear_deadlines();
        pairing_address.clear();
        connecting_address.clear();
        displayed_passkey = 0;
        reconnect_attempts = 0;
        active_reconnect_address.clear();
        timed_out_from_pairing = false;
        timed_out_from_connect = false;
        failed_from_pairing = false;
        failed_from_connect = false;
        cancelled_from_pairing = false;
        cancelled_from_connect = false;
        rejected_from_pairing = false;
        reconnect_gave_up = false;
        connection_in_flight = false;
    }

    void emit_action(action_kind kind, const std::string &addr = "", std::uint32_t pk = 0, std::uint64_t tok = 0)
    {
        action a;
        a.kind = kind;
        a.address = addr;
        a.passkey = pk;
        if (tok != 0) {
            a.token = tok;
        } else {
            switch (kind) {
            case action_kind::start_scan:
            case action_kind::cancel_scan:
                a.token = scan_token;
                break;
            case action_kind::pair:
            case action_kind::cancel_pair:
            case action_kind::submit_auth:
                a.token = pair_token;
                break;
            case action_kind::connect:
            case action_kind::reconnect:
            case action_kind::cancel_connect:
            case action_kind::disconnect:
                a.token = connect_token;
                break;
            default:
                a.token = 0;
                break;
            }
        }
        action_queue.push_back(a);
    }
};

state_machine::state_machine() : state_(new State()) {}

state_machine::~state_machine() { delete state_; }

void state_machine::begin_search()
{
    state_->current = screen::searching;
    state_->active_notice = notice::none;
    state_->timed_out_from_pairing = false;
    state_->timed_out_from_connect = false;
    state_->failed_from_pairing = false;
    state_->failed_from_connect = false;
    state_->cancelled_from_pairing = false;
    state_->cancelled_from_connect = false;
    state_->rejected_from_pairing = false;
    state_->reconnect_gave_up = false;
    state_->devices_.clear();
    state_->scan_token = state_->next_token();
    state_->clear_deadlines();
    state_->scan_deadline = k_scan_timeout_ms;
    state_->emit_action(action_kind::start_scan);
}

void state_machine::begin_paired()
{
    state_->current = screen::paired;
    state_->active_notice = notice::none;
    state_->timed_out_from_pairing = false;
    state_->timed_out_from_connect = false;
    state_->failed_from_pairing = false;
    state_->failed_from_connect = false;
    state_->cancelled_from_pairing = false;
    state_->cancelled_from_connect = false;
    state_->rejected_from_pairing = false;
    state_->reconnect_gave_up = false;
    state_->devices_.clear();
    for (const auto &d : state_->paired_devices) {
        state_->devices_.add(d);
    }
    state_->clear_deadlines();
}

void state_machine::set_paired_devices(const std::vector<device> &paired)
{
    state_->paired_devices = paired;
    state_->devices_.clear();
    for (const auto &d : paired) state_->devices_.add(d);
}

void state_machine::scan_finished(std::uint64_t token, const std::vector<device> &devices)
{
    if (state_->current != screen::searching || token != state_->scan_token) {
        return;
    }
    state_->devices_.clear();
    for (const auto &d : devices) {
        state_->devices_.add(d);
    }
    state_->current = screen::results;
    if (devices.empty()) {
        state_->active_notice = notice::empty;
    } else {
        state_->active_notice = notice::none;
    }
    state_->clear_deadlines();
    // No cancel_scan action: the scan completed naturally.
}

void state_machine::scan_failed(std::uint64_t token)
{
    if (state_->current != screen::searching || token != state_->scan_token) {
        return;
    }
    state_->current = screen::results;
    state_->active_notice = notice::failed;
    state_->failed_from_pairing = false;
    state_->failed_from_connect = false;
    state_->cancelled_from_pairing = false;
    state_->cancelled_from_connect = false;
    state_->clear_deadlines();
    // No cancel_scan action: the scan failed, no need to cancel.
}

void state_machine::scan_timed_out(std::uint64_t token)
{
    if (state_->current != screen::searching || token != state_->scan_token) {
        return;
    }
    state_->current = screen::results;
    state_->active_notice = notice::timed_out;
    state_->timed_out_from_pairing = false;
    state_->timed_out_from_connect = false;
    state_->clear_deadlines();
    state_->emit_action(action_kind::cancel_scan);
}

void state_machine::pairing_started(std::uint64_t token, const std::string &address)
{
    if ((state_->current != screen::results && state_->current != screen::paired) ||
        token != state_->pair_token) {
        return;
    }
    state_->pairing_address = address;
    state_->current = screen::pairing;
    state_->active_notice = notice::none;
    state_->timed_out_from_pairing = false;
    state_->timed_out_from_connect = false;
    state_->failed_from_pairing = false;
    state_->failed_from_connect = false;
    state_->rejected_from_pairing = false;
    state_->reconnect_gave_up = false;
    state_->pair_deadline = k_pair_timeout_ms;
    state_->displayed_passkey = 0;
}

void state_machine::auth_requested(std::uint64_t token, auth_request_kind kind,
                                   std::uint32_t passkey)
{
    if (state_->current != screen::pairing || token != state_->pair_token) {
        return;
    }
    if (kind == auth_request_kind::passkey) {
        if (passkey >= k_passkey_modulus) {
            return;
        }
        state_->displayed_passkey = passkey;
    } else {
        state_->displayed_passkey = 0;
    }
    state_->pending_auth = kind;
    state_->current = screen::auth;
    state_->timed_out_from_pairing = false;
    state_->timed_out_from_connect = false;
    state_->failed_from_pairing = false;
    state_->failed_from_connect = false;
    state_->auth_deadline = k_auth_timeout_ms;
    state_->pair_deadline = 0;
}

void state_machine::pairing_finished(std::uint64_t token, pair_outcome outcome)
{
    if (state_->current != screen::pairing && state_->current != screen::auth) {
        return;
    }
    if (token != state_->pair_token) {
        return;
    }

    switch (outcome) {
    case pair_outcome::bonded:
        state_->current = screen::connecting;
        state_->connecting_address = state_->pairing_address;
        state_->connect_token = state_->next_token();
        state_->connection_in_flight = true;
        state_->connect_deadline = k_connect_timeout_ms;
        state_->reconnect_attempts = 0;
        state_->active_reconnect_address = state_->pairing_address;
        state_->emit_action(action_kind::connect, state_->connecting_address, 0, state_->connect_token);
        break;
    case pair_outcome::rejected:
        state_->current = screen::results;
        state_->active_notice = notice::failed;
        state_->failed_from_pairing = true;
        state_->failed_from_connect = false;
        state_->cancelled_from_pairing = false;
        state_->cancelled_from_connect = false;
        state_->rejected_from_pairing = true;
        state_->timed_out_from_pairing = false;
        state_->timed_out_from_connect = false;
        state_->pairing_address.clear();
        state_->displayed_passkey = 0;
        state_->clear_deadlines();
        break;
    case pair_outcome::cancelled:
        state_->current = screen::results;
        state_->active_notice = notice::cancelled;
        state_->timed_out_from_pairing = false;
        state_->timed_out_from_connect = false;
        state_->cancelled_from_pairing = true;
        state_->cancelled_from_connect = false;
        state_->pairing_address.clear();
        state_->displayed_passkey = 0;
        state_->clear_deadlines();
        break;
    case pair_outcome::timed_out:
        state_->timed_out_from_pairing = true;
        state_->timed_out_from_connect = false;
        state_->cancelled_from_pairing = false;
        state_->cancelled_from_connect = false;
        state_->current = screen::results;
        state_->active_notice = notice::timed_out;
        state_->pairing_address.clear();
        state_->displayed_passkey = 0;
        state_->clear_deadlines();
        break;
    case pair_outcome::failed:
        state_->current = screen::results;
        state_->active_notice = notice::failed;
        state_->failed_from_pairing = true;
        state_->failed_from_connect = false;
        state_->cancelled_from_pairing = false;
        state_->cancelled_from_connect = false;
        state_->timed_out_from_pairing = false;
        state_->timed_out_from_connect = false;
        state_->pairing_address.clear();
        state_->displayed_passkey = 0;
        state_->clear_deadlines();
        break;
    }
}

void state_machine::connection_finished(std::uint64_t token, bool connected)
{
    if (!state_->connection_in_flight || token == 0 || token != state_->connect_token) {
        return;
    }
    state_->connection_in_flight = false;
    if (connected) {
        state_->current = screen::connected;
        state_->active_notice = notice::none;
        state_->reconnect_attempts = 0;
        state_->timed_out_from_pairing = false;
        state_->timed_out_from_connect = false;
        state_->failed_from_pairing = false;
        state_->failed_from_connect = false;
        state_->cancelled_from_pairing = false;
        state_->cancelled_from_connect = false;
        state_->reconnect_gave_up = false;
        state_->clear_deadlines();
    } else {
        state_->current = screen::results;
        state_->active_notice = notice::failed;
        state_->failed_from_connect = true;
        state_->failed_from_pairing = false;
        state_->cancelled_from_pairing = false;
        state_->cancelled_from_connect = false;
        state_->reconnect_gave_up = false;
        state_->timed_out_from_pairing = false;
        state_->timed_out_from_connect = false;
        state_->connecting_address.clear();
        state_->clear_deadlines();
    }
}

void state_machine::schedule_reconnect(const device &item)
{
    if (state_->reconnect_attempts >= state_->max_reconnect_attempts) {
        state_->current = screen::paired;
        state_->active_notice = notice::failed;
        state_->failed_from_connect = true;
        state_->failed_from_pairing = false;
        state_->reconnect_gave_up = true;
        state_->timed_out_from_pairing = false;
        state_->timed_out_from_connect = false;
        return;
    }
    state_->reconnect_attempts++;
    state_->current = screen::connecting;
    state_->connecting_address = item.address;
    state_->active_reconnect_address = item.address;
    state_->connect_token = state_->next_token();
    state_->connection_in_flight = true;
    state_->connect_deadline = k_connect_timeout_ms;
    state_->timed_out_from_pairing = false;
    state_->timed_out_from_connect = false;
    state_->failed_from_pairing = false;
    state_->failed_from_connect = false;
    state_->cancelled_from_pairing = false;
    state_->cancelled_from_connect = false;
    state_->emit_action(action_kind::reconnect, item.address, 0, state_->connect_token);
}

void state_machine::advance_time(std::uint32_t elapsed_ms)
{
    if (state_->scan_deadline > 0) {
        if (elapsed_ms >= state_->scan_deadline) {
            state_->scan_deadline = 0;
            if (state_->current == screen::searching) {
                scan_timed_out(state_->scan_token);
            }
        } else {
            state_->scan_deadline -= elapsed_ms;
        }
    }
    if (state_->pair_deadline > 0) {
        if (elapsed_ms >= state_->pair_deadline) {
            state_->pair_deadline = 0;
            if (state_->current == screen::pairing || state_->current == screen::auth) {
                const std::string address = state_->pairing_address;
                const std::uint64_t token = state_->pair_token;
                pairing_finished(token, pair_outcome::timed_out);
                state_->emit_action(action_kind::cancel_pair, address, 0, token);
            }
        } else {
            state_->pair_deadline -= elapsed_ms;
        }
    }
    if (state_->auth_deadline > 0) {
        if (elapsed_ms >= state_->auth_deadline) {
            state_->auth_deadline = 0;
            if (state_->current == screen::auth) {
                const std::string address = state_->pairing_address;
                const std::uint64_t token = state_->pair_token;
                pairing_finished(token, pair_outcome::timed_out);
                state_->emit_action(action_kind::cancel_pair, address, 0, token);
            }
        } else {
            state_->auth_deadline -= elapsed_ms;
        }
    }
    if (state_->connect_deadline > 0) {
        if (elapsed_ms >= state_->connect_deadline) {
            state_->connect_deadline = 0;
            if (state_->current == screen::connecting) {
                const std::string address = state_->connecting_address;
                const std::uint64_t token = state_->connect_token;
                connection_finished(token, false);
                state_->active_notice = notice::timed_out;
                state_->timed_out_from_connect = true;
                state_->failed_from_connect = false;
                state_->emit_action(action_kind::cancel_connect, address, 0, token);
            }
        } else {
            state_->connect_deadline -= elapsed_ms;
        }
    }
}

void state_machine::press(key pressed)
{
    switch (state_->current) {
    case screen::idle:
        break;
    case screen::searching:
        if (pressed == key::escape) {
            state_->current = screen::idle;
            state_->active_notice = notice::cancelled;
            state_->clear_deadlines();
            state_->emit_action(action_kind::cancel_scan, "", 0, state_->scan_token);
        }
        break;
    case screen::results:
        if (pressed == key::up) {
            state_->devices_.move_up();
        } else if (pressed == key::down) {
            state_->devices_.move_down();
        } else if (pressed == key::enter) {
            const device *sel = state_->devices_.selected();
            if (sel && sel->connectable) {
                state_->pairing_address = sel->address;
                state_->pair_token = state_->next_token();
                state_->pair_deadline = k_pair_timeout_ms;
                state_->emit_action(action_kind::pair, sel->address, 0, state_->pair_token);
                state_->current = screen::pairing;
            }
        } else if (pressed == key::escape) {
            state_->current = screen::idle;
            state_->active_notice = notice::none;
        }
        break;
    case screen::paired:
        if (pressed == key::up) {
            state_->devices_.move_up();
        } else if (pressed == key::down) {
            state_->devices_.move_down();
        } else if (pressed == key::enter) {
            const device *sel = state_->devices_.selected();
            if (sel) {
                state_->reconnect_attempts = 0;
                state_->connecting_address = sel->address;
                state_->connect_token = state_->next_token();
                state_->connection_in_flight = true;
                state_->connect_deadline = k_connect_timeout_ms;
                state_->active_reconnect_address = sel->address;
                state_->reconnect_gave_up = false;
                state_->current = screen::connecting;
                state_->emit_action(action_kind::connect, sel->address, 0, state_->connect_token);
            }
        } else if (pressed == key::escape) {
            state_->current = screen::idle;
            state_->active_notice = notice::none;
        }
        break;
    case screen::pairing:
    case screen::auth:
        if (pressed == key::escape) {
            state_->current = screen::results;
            state_->active_notice = notice::cancelled;
            state_->timed_out_from_pairing = false;
            state_->timed_out_from_connect = false;
            state_->cancelled_from_pairing = true;
            state_->cancelled_from_connect = false;
            {
                std::string addr = state_->pairing_address;
                state_->pairing_address.clear();
                state_->displayed_passkey = 0;
                state_->clear_deadlines();
                state_->emit_action(action_kind::cancel_pair, addr, 0, state_->pair_token);
            }
        } else if (pressed == key::enter) {
            submit_auth(state_->displayed_passkey);
        }
        break;
    case screen::connecting:
        if (pressed == key::escape) {
            state_->current = screen::results;
            state_->active_notice = notice::cancelled;
            state_->timed_out_from_pairing = false;
            state_->timed_out_from_connect = false;
            state_->cancelled_from_pairing = false;
            state_->cancelled_from_connect = true;
            {
                std::string addr = state_->connecting_address;
                state_->connecting_address.clear();
                state_->clear_deadlines();
                state_->emit_action(action_kind::cancel_connect, addr, 0, state_->connect_token);
                state_->connection_in_flight = false;
            }
        }
        break;
    case screen::connected:
        if (pressed == key::escape) {
            state_->current = screen::idle;
            state_->active_notice = notice::none;
            const std::string address = state_->connecting_address;
            state_->connecting_address.clear();
            state_->active_reconnect_address.clear();
            state_->reconnect_attempts = 0;
            state_->timed_out_from_pairing = false;
            state_->timed_out_from_connect = false;
            state_->failed_from_pairing = false;
            state_->failed_from_connect = false;
            state_->cancelled_from_pairing = false;
            state_->cancelled_from_connect = false;
            state_->clear_deadlines();
            state_->emit_action(action_kind::disconnect, address, 0, state_->connect_token);
            state_->connection_in_flight = false;
        }
        break;
    }
}

void state_machine::submit_auth(std::uint32_t passkey)
{
    if (state_->current != screen::auth) {
        return;
    }
    if (state_->pending_auth == auth_request_kind::passkey) {
        if (passkey != state_->displayed_passkey) {
            return;
        }
    }
    state_->displayed_passkey = 0;
    state_->current = screen::pairing;
    state_->pair_deadline = k_pair_timeout_ms;
    state_->auth_deadline = 0;
    state_->emit_action(action_kind::submit_auth, state_->pairing_address, passkey, state_->pair_token);
}

screen state_machine::current_screen() const
{
    return state_->current;
}

notice state_machine::current_notice() const
{
    return state_->active_notice;
}

std::string state_machine::notice_text() const
{
    switch (state_->active_notice) {
    case notice::empty:
        return k_msg_scan_empty;
    case notice::failed:
        if (state_->failed_from_pairing) {
            if (state_->rejected_from_pairing) return k_msg_pair_rejected;
            return k_msg_pair_failed;
        }
        if (state_->failed_from_connect) {
            if (state_->reconnect_gave_up) return k_msg_reconnect_gave_up;
            return k_msg_connect_failed;
        }
        return k_msg_scan_failed;
    case notice::timed_out:
        if (state_->timed_out_from_pairing) {
            return k_msg_pair_timeout;
        }
        if (state_->timed_out_from_connect) {
            return k_msg_connect_timeout;
        }
        return k_msg_scan_timeout;
    case notice::cancelled:
        if (state_->cancelled_from_pairing) {
            return k_msg_pair_cancelled;
        }
        if (state_->cancelled_from_connect) {
            return k_msg_connect_cancelled;
        }
        if (state_->current == screen::idle) {
            return k_msg_search_cancelled;
        }
        if (state_->current == screen::results) {
            return k_msg_pair_cancelled;
        }
        return k_msg_connect_cancelled;
    default:
        return "";
    }
}

std::string state_machine::status_line() const
{
    const auto shown_name = [this](const std::string &address) {
        const device *item = state_->devices_.find(address);
        return item == nullptr ? address : display_name(*item);
    };
    switch (state_->current) {
    case screen::searching:
        return k_status_scanning;
    case screen::pairing: {
        std::string out = k_status_pairing_prefix;
        out += shown_name(state_->pairing_address);
        return out;
    }
    case screen::auth: {
        std::string out;
        if (state_->pending_auth == auth_request_kind::passkey) {
            out = k_status_enter_passkey;
            out += format_passkey(state_->displayed_passkey);
        } else if (state_->pending_auth == auth_request_kind::confirm) {
            out = k_status_confirm;
        } else {
            out = k_status_numeric_compare;
        }
        return out;
    }
    case screen::connecting: {
        std::string out = k_status_connecting_prefix;
        out += shown_name(state_->connecting_address);
        return out;
    }
    case screen::connected: {
        std::string out = k_status_connected_prefix;
        out += shown_name(state_->connecting_address);
        out += k_status_connected_suffix;
        return out;
    }
    default:
        return "";
    }
}

std::size_t state_machine::selected_index() const
{
    return state_->devices_.selected_index();
}

const device *state_machine::selected() const
{
    return state_->devices_.selected();
}

const device_list &state_machine::devices() const
{
    return state_->devices_;
}

std::uint32_t state_machine::displayed_passkey() const
{
    return state_->displayed_passkey;
}

auth_request_kind state_machine::pending_auth_kind() const
{
    return state_->pending_auth;
}

std::string state_machine::active_address() const
{
    if (state_->current == screen::pairing || state_->current == screen::auth) {
        return state_->pairing_address;
    }
    if (state_->current == screen::connecting || state_->current == screen::connected) {
        return state_->connecting_address;
    }
    return "";
}

std::uint64_t state_machine::active_scan_token() const
{
    return state_->scan_token;
}

std::uint64_t state_machine::active_pair_token() const
{
    return state_->pair_token;
}

std::uint64_t state_machine::active_connection_token() const
{
    return state_->connect_token;
}

std::vector<action> state_machine::take_actions()
{
    std::vector<action> out = std::move(state_->action_queue);
    state_->action_queue.clear();
    return out;
}

} // namespace cyberdeck_ble
