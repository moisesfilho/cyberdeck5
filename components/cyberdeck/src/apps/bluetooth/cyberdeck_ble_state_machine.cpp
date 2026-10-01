#include "apps/bluetooth/cyberdeck_ble_state_machine.h"

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
    address_type pairing_addr_type = address_type::public_address;
    std::string connecting_address;
    address_type connecting_addr_type = address_type::public_address;
    std::string active_reconnect_address;
    bool connection_in_flight = false;
    bool connection_established = false;
    auth_request_kind pending_auth = auth_request_kind::passkey;
    auth_io_action pending_io = auth_io_action::input;
    std::uint32_t displayed_passkey = 0;

    std::uint32_t reconnect_attempts = 0;
    std::uint32_t max_reconnect_attempts = k_max_reconnect_attempts;

    /* Background observer state: armed only for spontaneous link loss of a
     * known bond.  Manual disconnect blocks the cycle until explicit Enter;
     * reaching the per-cycle cap only closes the current announcement window
     * until a fresh advertisement resets it. */
    bool background_armed = false;
    bool background_manually_blocked = false;
    device background_target_device;
    bool has_background_target_device = false;
    std::uint32_t background_cycle_attempts = 0;
    bool background_connection_in_flight = false;
    bool background_attempt_reserved = false;

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
        pairing_addr_type = address_type::public_address;
        connecting_address.clear();
        connecting_addr_type = address_type::public_address;
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
        connection_established = false;
        /* reset_to_idle is an internal helper only; the background armed flag
         * is managed explicitly by arm/block paths so a spontaneous link loss
         * never disarms the observer by accident. */
    }

    void emit_action(action_kind kind, const std::string &addr = "", std::uint32_t pk = 0,
                     std::uint64_t tok = 0,
                     address_type addr_type = address_type::public_address)
    {
        action a;
        a.kind = kind;
        a.address = addr;
        if (!addr.empty()) {
            const device *known = devices_.find(addr);
            if (known == nullptr) {
                for (const device &candidate : paired_devices) {
                    if (candidate.address == addr) { known = &candidate; break; }
                }
            }
            if (known != nullptr) a.addr_type = known->addr_type;
        }
        if (!addr.empty()) a.addr_type = addr_type;
    a.passkey = pk;
    a.numcmp = (kind == action_kind::submit_auth && pending_io == auth_io_action::numeric_compare)
                   ? displayed_passkey : 0;
    a.numcmp_accept = (kind == action_kind::submit_auth && pending_io == auth_io_action::numeric_compare);
    a.auth_action = (kind == action_kind::submit_auth) ? pending_io : auth_io_action::input;
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
    if (const device *known = state_->devices_.find(address)) {
        state_->pairing_addr_type = known->addr_type;
    }
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
                                   std::uint32_t passkey, auth_io_action io_action)
{
    if (state_->current != screen::pairing || token != state_->pair_token) {
        return;
    }
    if (io_action == auth_io_action::display || io_action == auth_io_action::input) {
        if (kind != auth_request_kind::passkey ||
            (io_action == auth_io_action::display && passkey >= k_passkey_modulus) ||
            (io_action == auth_io_action::input && passkey != 0)) {
            return;
        }
    } else if (io_action == auth_io_action::numeric_compare) {
        if (kind != auth_request_kind::numeric_compare || passkey >= k_passkey_modulus) return;
    } else {
        return;
    }
    if (io_action == auth_io_action::display) {
        state_->displayed_passkey = passkey;
    } else if (io_action == auth_io_action::numeric_compare) {
        state_->displayed_passkey = passkey;
    } else {
        state_->displayed_passkey = 0;
    }
    state_->pending_io = io_action;
    /* The old kind-only branch is intentionally gone: the stack action is the
     * authority and is never inferred from the presence of a number. */
    /* if (kind == auth_request_kind::passkey) {
        if (passkey >= k_passkey_modulus) {
            return;
        }
        state_->displayed_passkey = passkey;
    } else {
        state_->displayed_passkey = 0;
    } */
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
        state_->connecting_addr_type = state_->pairing_addr_type;
        state_->connect_token = state_->next_token();
        state_->connection_in_flight = true;
        state_->connection_established = false;
        state_->connect_deadline = k_connect_timeout_ms;
        state_->reconnect_attempts = 0;
        state_->active_reconnect_address = state_->pairing_address;
        state_->emit_action(action_kind::connect, state_->connecting_address, 0, state_->connect_token,
                            state_->connecting_addr_type);
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
    if (token == 0 || token != state_->connect_token ||
        (!state_->connection_in_flight && !state_->connection_established)) {
        return;
    }
    if (connected && !state_->connection_in_flight) {
        return;
    }
    /* Capture this before clearing the in-flight state below.  Background
     * failures are intentionally silent and must not fall through to the
     * interactive results screen. */
    const bool was_background_connection = state_->background_connection_in_flight;
    if (connected) {
        state_->background_connection_in_flight = false;
        state_->connection_in_flight = false;
        state_->connection_established = true;
        /* ble_mgr keeps the authenticated link.  The model only releases the
         * interactive screen so the local shell can resume. */
        state_->current = screen::idle;
        state_->active_notice = notice::none;
        state_->reconnect_attempts = 0;
        state_->timed_out_from_pairing = false;
        state_->timed_out_from_connect = false;
        state_->failed_from_pairing = false;
        state_->failed_from_connect = false;
        state_->cancelled_from_pairing = false;
        state_->cancelled_from_connect = false;
        state_->reconnect_gave_up = false;
        /* A successful link (re)arms the background cycle for the bonded peer:
         * any later spontaneous loss must retry without user interaction. */
        if (!state_->connecting_address.empty()) {
            state_->background_target_device.address = state_->connecting_address;
            state_->background_target_device.addr_type = state_->connecting_addr_type;
            state_->has_background_target_device = true;
            state_->background_cycle_attempts = 0;
            state_->background_manually_blocked = false;
            state_->background_armed = true;
        }
        state_->clear_deadlines();
    } else {
        const bool was_established = state_->connection_established;
        state_->connection_in_flight = false;
        state_->connection_established = false;
        if (was_established) {
            /* ble_mgr already performed the physical teardown.  Do not emit a
             * synthetic disconnect or report a failed connection.  A
             * spontaneous loss keeps the background cycle armed so the
             * observer may retry; the per-cycle budget is consumed by the
             * UI-side scheduler, not here. */
            state_->current = screen::idle;
            state_->active_notice = notice::none;
            state_->clear_deadlines();
            return;
        }
        if (was_background_connection) {
            state_->background_connection_in_flight = false;
            state_->current = screen::idle;
            state_->active_notice = notice::none;
            state_->failed_from_connect = false;
            state_->clear_deadlines();
            return;
        }
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
    state_->connecting_addr_type = item.addr_type;
    state_->active_reconnect_address = item.address;
    state_->connect_token = state_->next_token();
    state_->connection_in_flight = true;
    state_->background_connection_in_flight = state_->background_attempt_reserved;
    state_->background_attempt_reserved = false;
    state_->connection_established = false;
    state_->connect_deadline = k_connect_timeout_ms;
    state_->timed_out_from_pairing = false;
    state_->timed_out_from_connect = false;
    state_->failed_from_pairing = false;
    state_->failed_from_connect = false;
    state_->cancelled_from_pairing = false;
    state_->cancelled_from_connect = false;
    state_->emit_action(action_kind::reconnect, item.address, 0, state_->connect_token, item.addr_type);
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
                state_->emit_action(action_kind::cancel_pair, address, 0, token, state_->pairing_addr_type);
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
                state_->emit_action(action_kind::cancel_pair, address, 0, token, state_->pairing_addr_type);
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
                const bool background = state_->background_connection_in_flight;
                connection_finished(token, false);
                if (!background) {
                    state_->active_notice = notice::timed_out;
                    state_->timed_out_from_connect = true;
                    state_->failed_from_connect = false;
                }
                state_->emit_action(action_kind::cancel_connect, address, 0, token,
                                    state_->connecting_addr_type);
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
        /* Successful connections release ownership to idle while ble_mgr
         * keeps the link.  An explicit Escape there is the manual disconnect:
         * it must reach the manager and block the background cycle. */
        if (pressed == key::escape && state_->connection_established) {
            const std::string address = state_->connecting_address;
            const address_type addr_type = state_->connecting_addr_type;
            const std::uint64_t token = state_->connect_token;
            if (token == 0 || address.empty()) {
                break;
            }
            state_->current = screen::idle;
            state_->active_notice = notice::none;
            state_->connecting_address.clear();
            state_->active_reconnect_address.clear();
            state_->reconnect_attempts = 0;
            /* Manual disconnect blocks the background cycle until an explicit
             * Enter action re-arms it. */
            state_->background_armed = false;
            state_->background_manually_blocked = true;
            state_->background_cycle_attempts = 0;
            state_->timed_out_from_pairing = false;
            state_->timed_out_from_connect = false;
            state_->failed_from_pairing = false;
            state_->failed_from_connect = false;
            state_->cancelled_from_pairing = false;
            state_->cancelled_from_connect = false;
            state_->clear_deadlines();
            state_->emit_action(action_kind::disconnect, address, 0, token, addr_type);
            state_->connection_in_flight = false;
            /* connection_established stays true until the physical
             * DISCONNECTED completion arrives; the cleared address guards
             * against a second synthetic disconnect. */
        }
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
                state_->pairing_addr_type = sel->addr_type;
                state_->pair_token = state_->next_token();
                state_->pair_deadline = k_pair_timeout_ms;
                state_->emit_action(action_kind::pair, sel->address, 0, state_->pair_token, sel->addr_type);
                state_->current = screen::pairing;
            } else if (sel) {
                state_->active_notice = notice::not_connectable;
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
                state_->connecting_addr_type = sel->addr_type;
                state_->connect_token = state_->next_token();
                state_->connection_in_flight = true;
                state_->background_connection_in_flight = false;
                state_->connection_established = false;
                state_->connect_deadline = k_connect_timeout_ms;
                state_->active_reconnect_address = sel->address;
                state_->reconnect_gave_up = false;
                /* Explicit Enter re-arms the background cycle for this bond. */
                state_->background_target_device = *sel;
                state_->has_background_target_device = true;
                state_->background_cycle_attempts = 0;
                state_->background_manually_blocked = false;
                state_->background_armed = true;
                state_->current = screen::connecting;
                state_->emit_action(action_kind::connect, sel->address, 0, state_->connect_token,
                                    sel->addr_type);
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
                state_->emit_action(action_kind::cancel_pair, addr, 0, state_->pair_token,
                                    state_->pairing_addr_type);
            }
        } else if (pressed == key::enter) {
            if (state_->pending_io != auth_io_action::input)
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
                state_->emit_action(action_kind::cancel_connect, addr, 0, state_->connect_token,
                                    state_->connecting_addr_type);
                state_->connection_in_flight = false;
                state_->background_connection_in_flight = false;
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
            /* Manual disconnect blocks the background cycle until an explicit
             * Enter action re-arms it. */
            state_->background_armed = false;
            state_->background_manually_blocked = true;
            state_->background_cycle_attempts = 0;
            state_->timed_out_from_pairing = false;
            state_->timed_out_from_connect = false;
            state_->failed_from_pairing = false;
            state_->failed_from_connect = false;
            state_->cancelled_from_pairing = false;
            state_->cancelled_from_connect = false;
            state_->clear_deadlines();
            state_->emit_action(action_kind::disconnect, address, 0, state_->connect_token,
                                state_->connecting_addr_type);
            state_->connection_in_flight = false;
            state_->background_connection_in_flight = false;
        }
        break;
    }
}

void state_machine::submit_auth(std::uint32_t passkey)
{
    if (state_->current != screen::auth) {
        return;
    }
    if (state_->pending_io == auth_io_action::input && passkey >= k_passkey_modulus) return;
    if (state_->pending_io != auth_io_action::input &&
        state_->displayed_passkey >= k_passkey_modulus) return;
    state_->displayed_passkey = 0;
    state_->current = screen::pairing;
    state_->pair_deadline = k_pair_timeout_ms;
    state_->auth_deadline = 0;
    const std::uint32_t injected_passkey =
        state_->pending_io == auth_io_action::numeric_compare ? 0 : passkey;
    state_->emit_action(action_kind::submit_auth, state_->pairing_address, injected_passkey, state_->pair_token,
                        state_->pairing_addr_type);
}

screen state_machine::current_screen() const
{
    return state_->current;
}

notice state_machine::current_notice() const
{
    return state_->active_notice;
}

bool state_machine::owns_input() const
{
    switch (state_->current) {
    case screen::searching:
    case screen::pairing:
    case screen::auth:
    case screen::connecting:
    case screen::connected:
        return true;
    case screen::results:
    case screen::paired:
        return state_->devices_.size() != 0;
    case screen::idle:
        return false;
    }
    return false;
}

bool state_machine::is_connected() const
{
    return state_->connection_established;
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
    case notice::not_connectable:
        return k_msg_not_connectable;
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
        if (state_->pending_io == auth_io_action::input) {
            out = "Enter the passkey: ";
        } else if (state_->pending_io == auth_io_action::display) {
            out = k_status_enter_passkey;
            out += format_passkey(state_->displayed_passkey);
        } else {
            out = k_status_numeric_compare;
            out += " Number: ";
            out += format_passkey(state_->displayed_passkey);
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

auth_io_action state_machine::pending_auth_action() const
{
    return state_->pending_io;
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

bool state_machine::background_reconnect_armed() const
{
    return state_->background_armed && !state_->background_manually_blocked &&
           state_->has_background_target_device &&
           state_->background_cycle_attempts < state_->max_reconnect_attempts;
}

void state_machine::arm_background_reconnect(const device &item)
{
    if (item.address.empty()) {
        return;
    }
    /* Boot restore and spontaneous-loss re-arm: store the known bond identity
     * without touching the visible screen, ownership, tokens or notices. */
    state_->background_target_device = item;
    state_->has_background_target_device = true;
    state_->background_cycle_attempts = 0;
    state_->background_manually_blocked = false;
    state_->background_armed = true;
}

void state_machine::block_background_reconnect()
{
    /* Manual disconnect: the observer must stay quiet until explicit Enter. */
    state_->background_armed = false;
    state_->background_manually_blocked = true;
    state_->background_cycle_attempts = 0;
}

bool state_machine::has_background_target() const
{
    return state_->has_background_target_device;
}

device state_machine::background_target() const
{
    return state_->background_target_device;
}

bool state_machine::consume_background_attempt()
{
    if (!state_->background_armed || state_->background_manually_blocked ||
        !state_->has_background_target_device) {
        return false;
    }
    if (state_->background_cycle_attempts >= state_->max_reconnect_attempts) {
        /* Cap reached for this announcement cycle: wait for a fresh
         * advertisement to reset the budget.  The cycle stays armed (unless
         * manually blocked) so the reset can reopen it without re-arm. */
        return false;
    }
    ++state_->background_cycle_attempts;
    state_->background_attempt_reserved = true;
    return true;
}

void state_machine::reset_background_cycle()
{
    /* A fresh advertisement opens a new budget window, but only when the
     * cycle was not blocked by a manual disconnect. */
    if (!state_->background_manually_blocked) {
        state_->background_armed = true;
        state_->background_cycle_attempts = 0;
        state_->reconnect_attempts = 0;
    }
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
