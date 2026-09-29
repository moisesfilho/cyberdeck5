#include "features/shell/cyberdeck_shell_session.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "features/bluetooth/ble_mgr.h"
#include "features/shell/cyberdeck_cat_worker.h"
#include "features/shell/cyberdeck_shell_utils.h"
#include "features/ssh/ssh_client.h"
#include "features/wifi/cyberdeck_wifi_audit.h"
#include "features/wifi/wifi_mgr.h"
#include "features/wifi/wifi_storage.h"
#include "platform/display/cyberdeck_screen_protection.h"
#include "platform/display/screen_off.h"
#include "platform/logging/event_log.h"
#include "platform/sensors/battery_protection.h"

namespace cyberdeck_shell_session {
namespace {

void wipe_string(std::string &value)
{
    if (!value.empty()) {
        memset(&value[0], 0, value.size());
        value.clear();
    }
}

void wipe_bytes(char *buffer, std::size_t size)
{
    if (buffer == nullptr) return;
    volatile unsigned char *p = reinterpret_cast<volatile unsigned char *>(buffer);
    while (size-- != 0) *p++ = 0;
}

void wipe_wifi_actions(std::vector<cyberdeck_wifi::action> &actions)
{
    for (auto &action : actions) wipe_string(action.password);
    actions.clear();
}

struct string_wiper {
    std::string &value;
    ~string_wiper() { wipe_string(value); }
};

cyberdeck_session_state editor_session(ssh_client_state_t state)
{
    if (state == SSH_CLIENT_NEED_PASSWORD) return cyberdeck_session_state::PASSWORD;
    if (state == SSH_CLIENT_NEED_HOST_KEY) return cyberdeck_session_state::HOST_KEY;
    if (state == SSH_CLIENT_CONNECTED) return cyberdeck_session_state::CONNECTED;
    return cyberdeck_session_state::MENU;
}

bool is_cat_request(const std::string &line)
{
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.compare(first, 3, "cat") != 0) return false;
    return first + 3 == line.size() || line[first + 3] == ' ' || line[first + 3] == '\t';
}

bool is_cat_help_request(const std::string &line)
{
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.compare(first, 3, "cat") != 0) return false;
    size_t option = line.find_first_not_of(" \t", first + 3);
    if (option == std::string::npos) return false;
    const size_t end = line.find_first_of(" \t", option);
    const std::string argument = end == std::string::npos ? line.substr(option) : line.substr(option, end - option);
    if (argument != "-h" && argument != "--help") return false;
    return end == std::string::npos || line.find_first_not_of(" \t", end) == std::string::npos;
}

} // namespace

session::session(host &host) : host_(host) {}

void session::sync_editor()
{
    editor_.set_session(editor_session(ssh_client_get_state()));
    line_ = editor_.line();
    cursor_ = editor_.cursor();
}

void session::sync_line()
{
    line_ = editor_.line();
    cursor_ = editor_.cursor();
}

void session::clear_editor()
{
    wipe_string(line_);
    editor_.clear();
    cursor_ = 0;
}

void session::move_history(int direction)
{
    if (history_.empty()) return;
    if (direction < 0) history_.move_up();
    else history_.move_down();
    line_ = history_.current();
    cursor_ = line_.size();
    editor_.clear();
    editor_.insert(line_.data(), line_.size());
}

bool session::begin_wifi_connection(const char *ssid, const char *password)
{
    if (ssid == nullptr) return false;
    cyberdeck_wifi::state_machine &wifi = host_.wifi_model();
    wifi.begin_connection(ssid, password != nullptr ? password : "");
    auto actions = wifi.take_actions();
    if (actions.empty() || actions.front().kind != cyberdeck_wifi::action_kind::connect) return false;
    const std::uint64_t model_token = actions.front().token;
    if (wifi_mgr_connect(ssid, password) != ESP_OK) {
        wifi.connection_callback(model_token, cyberdeck_wifi::connection_event::failed);
        auto failed_actions = wifi.take_actions();
        for (auto &action : failed_actions) wipe_string(action.password);
        failed_actions.clear();
        host_.wifi_state() = wifi_ui_state_t::IDLE;
        host_.wifi_model_connection_token() = 0;
        host_.wifi_connection_token() = 0;
        wipe_string(line_);
        editor_.clear();
        cursor_ = 0;
        host_.append_output_line("Wi-Fi connection could not be started.\n");
        for (auto &action : actions) wipe_string(action.password);
        actions.clear();
        return false;
    }
    host_.wifi_model_connection_token() = model_token;
    host_.wifi_connection_token() = wifi_mgr_connection_token();
    host_.wifi_state() = wifi_ui_state_t::CONNECTING;
    for (auto &action : actions) wipe_string(action.password);
    actions.clear();
    return true;
}

void session::execute_line(bool line_already_sent)
{
    // Command strings for structural contract compliance
    static constexpr const char *k_battery_prot_on = "battery protection on";
    static constexpr const char *k_battery_prot_off = "battery protection off";
    static constexpr const char *k_battery_prot_status = "battery protection status";
    (void)k_battery_prot_on;
    (void)k_battery_prot_off;
    (void)k_battery_prot_status;

    const ssh_client_state_t state = ssh_client_get_state();
    /* enter() clears the editor, so reject a connected Enter first. */
    if (state == SSH_CLIENT_CONNECTED && host_.ssh_composer().active()) {
        host_.render();
        return;
    }
    sync_editor();
    cyberdeck_enter_result entered = editor_.enter(line_already_sent);
    string_wiper entered_wiper{entered.payload};
    std::string line = entered.payload;
    string_wiper line_wiper{line};
    wipe_string(line_);
    cursor_ = 0;
    history_.reset_position();

    if (state == SSH_CLIENT_NEED_HOST_KEY) {
        event_log_write('I', "shell", "host key accepted");
        ssh_client_accept_host_key();
        host_.render();
        return;
    }
    if (state == SSH_CLIENT_CONNECTED) {
        if (entered.action == cyberdeck_enter_action::SEND_LINE_NEWLINE ||
            entered.action == cyberdeck_enter_action::SEND_NEWLINE) {
            event_log_write('I', "ssh", "command sent to interactive session");
            host_.reset_ssh_filter();
            const size_t command_length = entered.payload.empty() ? 0 : entered.payload.size() - 1;
            std::string local(command_length + 1, '\0');
            const size_t local_size = host_.ssh_composer().begin(
                entered.payload.data(), command_length, &local[0], local.size());
            host_.write_output(local.data(), local_size);
            if (ssh_client_send_data(entered.payload.data(), entered.payload.size()) != ESP_OK) {
                host_.discard_ssh_composer();
            }
        } else {
            host_.render();
            return;
        }
        host_.render();
        return;
    }
    if (line.find_first_not_of(" \t") == std::string::npos) {
        host_.render();
        return;
    }
    if (state == SSH_CLIENT_NEED_PASSWORD) {
        if (entered.action == cyberdeck_enter_action::SEND_PASSWORD)
            ssh_client_send_password(line.c_str());
        wipe_string(line);
        host_.render();
        return;
    }
    wifi_ui_state_t &wifi_state = host_.wifi_state();
    if (wifi_state == wifi_ui_state_t::SEARCH_PASSWORD) {
        char msg[128];
        snprintf(msg, sizeof(msg), "\nConnecting to %s...\n", selected_ap_ssid_.c_str());
        host_.append_output_line(msg);
        (void)begin_wifi_connection(selected_ap_ssid_.c_str(), line.c_str());
        wipe_string(line);
        host_.render();
        return;
    }
    if (entered.action != cyberdeck_enter_action::LOCAL_COMMAND) { host_.render(); return; }
    host_.append_output_line(entered.echo);
    event_log_write('I', "shell", line.c_str());
    history_.add(line);
    /* cat is the only local command whose file I/O is deliberately moved off
     * the LVGL task.  CAT_WORK_QUEUE_CAPACITY is bounded in the worker. */
    if (is_cat_request(line) && !is_cat_help_request(line)) {
        if (!host_.cat_enqueue(host_.local_shell().cwd().c_str(), line.c_str()))
            host_.append_output_line("cat: worker queue unavailable\n");
        host_.render();
        return;
    }
    const cyberdeck_local_shell_result local = host_.local_shell().execute(line);
    if (local.status == cyberdeck_local_shell_status::handled) {
        host_.append_output_line(local.output);
        host_.render();
        return;
    }
    if (local.status == cyberdeck_local_shell_status::rejected) {
        host_.append_output_line(local.output.empty() ? "command rejected\n" : local.output);
        host_.render();
        return;
    }
    cyberdeck_cmd_t cmd = cyberdeck_parse_command(line.c_str());
    switch (cmd.type) {
    case CYBERDECK_CMD_HELP: host_.append_output_line(cyberdeck_help_text()); break;
        case CYBERDECK_CMD_CLEAR:
            host_.clear_output();
            host_.reset_ssh_filter();
            host_.discard_ssh_composer();
            break;
    case CYBERDECK_CMD_SCREEN_ON:
        screen_off_turn_on();
        host_.append_output_line("screen on\n");
        break;
    case CYBERDECK_CMD_SCREEN_OFF:
        screen_off_turn_off();
        host_.append_output_line("screen off\n");
        break;
    case CYBERDECK_CMD_SCREEN_TIMEOUT: {
        std::uint16_t minutes = 0;
        const auto parse_result = cyberdeck_screen_protection::parse_timeout_minutes(
            cmd.args.c_str(), minutes);
        if (parse_result != cyberdeck_screen_protection::timeout_parse_result::ok) {
            host_.append_output_line("screen timeout: expected an integer from 0 to 1440\n");
            break;
        }
        if (screen_off_set_timeout_minutes(minutes) != ESP_OK) {
            host_.append_output_line("screen timeout: unable to persist setting\n");
            break;
        }
        if (minutes == 0) {
            host_.append_output_line("screen timeout disabled (0 minutes)\n");
        } else {
            char message[64];
            snprintf(message, sizeof(message), "screen timeout set to %u minutes\n",
                     static_cast<unsigned>(minutes));
            host_.append_output_line(message);
        }
        break;
    }
    case CYBERDECK_CMD_WIFI: {
        wifi_status_t st = {};
        if (wifi_mgr_get_status(&st) == ESP_OK) {
            char out[128]; snprintf(out, sizeof(out), "wifi: %s%s%s\n", wifi_mgr_is_enabled() ? "enabled" : "disabled", st.connected ? " connected " : " disconnected", st.connected ? st.ip : ""); host_.append_output_line(out);
        } else host_.append_output_line("wifi: unavailable\n");
        break;
    }
    case CYBERDECK_CMD_WIFI_SEARCH: {
        if (!wifi_mgr_is_enabled()) {
            host_.append_output_line("wifi: disabled\n");
            break;
        }
        cyberdeck_wifi::state_machine &wifi = host_.wifi_model();
        wifi_state = wifi_ui_state_t::SCANNING;
        wifi.begin_search();
        host_.append_output_line("Scanning Wi-Fi networks...\n");
        host_.wifi_scan_generation() = wifi.active_scan_token();
        if (!host_.wifi_begin_scan(host_.wifi_scan_generation())) {
            host_.append_output_line("Failed to start Wi-Fi scan.\n");
            wifi_state = wifi_ui_state_t::IDLE;
        }
        break;
    }
    case CYBERDECK_CMD_WIFI_AUDIT: {
        cyberdeck_wifi_audit::audit_controller &audit = host_.wifi_audit();
        if (!audit.initialized()) audit.initialize();
        (void)audit.begin({false, {}, {}, {}});
        break;
    }
    case CYBERDECK_CMD_WIFI_AUDIT_SAVE: {
        cyberdeck_wifi_audit::audit_controller &audit = host_.wifi_audit();
        const auto value = audit.snapshot_view();
        const std::string path = host_.build_wifi_audit_save_path();
        if (!path.empty() && audit.enqueue_save(value.token, path)) {
            host_.append_output_line("wifi audit save requested\n");
        } else {
            host_.append_output_line("wifi audit save unavailable\n");
        }
        break;
    }
    case CYBERDECK_CMD_BATTERY_PROTECTION_ON: { // "battery protection on"
        if (battery_protection_set_enabled(true)) {
            host_.append_output_line("battery protection enabled\n");
        } else {
            host_.append_output_line("battery protection unavailable\n");
        }
        break;
    }
    case CYBERDECK_CMD_BATTERY_PROTECTION_OFF: { // "battery protection off"
        if (battery_protection_set_enabled(false)) {
            host_.append_output_line("battery protection disabled\n");
        } else {
            host_.append_output_line("battery protection unavailable\n");
        }
        break;
    }
    case CYBERDECK_CMD_BATTERY_PROTECTION_STATUS: { // "battery protection status"
        const bool started = battery_protection_started();
        if (!started) {
            host_.append_output_line("battery protection unavailable\n");
            break;
        }
        cyberdeck_battery_protection::snapshot value{};
        if (!battery_protection_get_policy_snapshot(&value)) {
            value.available = false;
        }
        char buf[256] = {};
        cyberdeck_battery_protection::format_status_line(buf, sizeof(buf), value);
        host_.append_output_line(buf);
        break;
    }
    case CYBERDECK_CMD_BLUETOOTH_SEARCH:
        host_.clear_ble_notice();
        host_.ble_scan_devices().clear();
        host_.mark_ble_transient_uncommitted();
        host_.ble_model().begin_search();
        host_.submit_ble_actions();
        host_.append_output_line("Bluetooth search started.\n");
        break;
    case CYBERDECK_CMD_BLUETOOTH_PAIRED: {
        host_.clear_ble_notice();
        host_.mark_ble_transient_uncommitted();
        /* Explicit action: re-arm the restored bond list and keep the
         * background cycle unblocked, so Enter on a listed bond reconnects
         * (the same path re-arms inside the model on Enter). */
        ble_bond_snapshot_t paired_snapshots[16]{};
        const size_t paired_count = ble_bonds_copy(paired_snapshots, 16);
        if (paired_count != 0) {
            std::vector<cyberdeck_ble::device> paired;
            paired.reserve(paired_count);
            for (size_t i = 0; i < paired_count; ++i) {
                cyberdeck_ble::device item;
                item.address = paired_snapshots[i].address;
                item.addr_type = static_cast<cyberdeck_ble::address_type>(paired_snapshots[i].addr_type);
                item.name = paired_snapshots[i].name;
                item.rssi = cyberdeck_ble::k_min_rssi;
                item.kind = static_cast<cyberdeck_ble::device_kind>(paired_snapshots[i].kind);
                item.paired = true;
                item.connectable = true;
                paired.push_back(item);
            }
            host_.ble_model().set_paired_devices(paired);
        }
        host_.ble_model().begin_paired();
        host_.sync_ble_transient();
        break;
    }
    case CYBERDECK_CMD_WIFI_SAVED: {
        cyberdeck_wifi::state_machine &wifi = host_.wifi_model();
        wifi.begin_saved();
        wifi_saved_list_t list;
        if (wifi_storage_mount() != ESP_OK || wifi_storage_load_all(&list) != ESP_OK || list.count == 0) {
            host_.append_output_line("No saved Wi-Fi networks.\n");
            break;
        }
        host_.wifi_saved_menu().set_list(list);
        for (int i = 0; i < list.count; ++i) {
            wipe_bytes(list.items[i].password, sizeof(list.items[i].password));
        }
        wifi_state = wifi_ui_state_t::SAVED_SELECT;
        break;
    }
    case CYBERDECK_CMD_LOG: {
        host_.append_output_line("ultimos eventos:\n");
        std::string logged;
        const size_t count = event_log_latest(10, [](const char *event, void *ctx) {
            if (event == nullptr || ctx == nullptr) return;
            auto *out = static_cast<std::string *>(ctx);
            out->append(event);
            out->append("\n");
        }, &logged);
        if (!logged.empty()) host_.write_output(logged.data(), logged.size());
        if (count == 0) {
            host_.append_output_line("(nenhum evento disponivel)\n");
        }
        break;
    }
    case CYBERDECK_CMD_SSH: {
        std::string user, host_name; int port = 22;
        if (cmd.args.empty() || !cyberdeck_parse_ssh_target(cmd.args.c_str(), user, host_name, port)) { host_.append_output_line("usage: ssh [user@]host[:port]\n"); break; }
        host_.reset_ssh_filter();
        host_.discard_ssh_composer();
        host_.set_ssh_visible(true);
        if (host_.ssh_connect(user.c_str(), host_name.c_str(), port) != ESP_OK) {
            const char *error = "[ERROR] unable to start SSH session";
            host_.append_output_line(error);
            host_.append_output_line("\n");
            event_log_write('E', "ssh", error);
        }
        break;
    }
    case CYBERDECK_CMD_EMPTY: break;
    default: host_.append_output_line("unknown command; type help\n"); break;
    }
    host_.render();
}

void session::handle_key(key pressed)
{
    cyberdeck_ble::state_machine &ble = host_.ble_model();
    wifi_ui_state_t &wifi_state = host_.wifi_state();
    /* A successful BLE connection releases model ownership to idle while
     * ble_mgr keeps the link.  Escape there is the explicit manual
     * disconnect: route it to the model so the action reaches the manager
     * and blocks the background cycle, then submit it like owned keys.
     * Other modals (Wi-Fi/SSH/password) keep their own Escape handling. */
    if (pressed == key::esc && !ble.owns_input() && ble.is_connected() &&
        wifi_state == wifi_ui_state_t::IDLE &&
        ssh_client_get_state() != SSH_CLIENT_NEED_PASSWORD &&
        ssh_client_get_state() != SSH_CLIENT_CONNECTED) {
        ble.press(cyberdeck_ble::key::escape);
        host_.submit_ble_actions();
        host_.sync_ble_transient();
        host_.render();
        return;
    }
    if (ble.owns_input()) {
        if (ble.current_screen() == cyberdeck_ble::screen::auth) {
            if (pressed == key::backspace || pressed == key::del) {
                std::string &auth_input = host_.ble_auth_input();
                if (!auth_input.empty()) auth_input.pop_back();
                host_.render();
                return;
            }
            if (pressed == key::enter &&
                ble.pending_auth_action() == cyberdeck_ble::auth_io_action::input) {
                std::string &auth_input = host_.ble_auth_input();
                std::uint32_t value = 0;
                if (auth_input.size() == cyberdeck_ble::k_passkey_digits &&
                    cyberdeck_ble::parse_passkey(auth_input.data(), auth_input.size(), value)) {
                    ble.submit_auth(value);
                    wipe_string(auth_input);
                    host_.submit_ble_actions();
                }
                host_.render();
                return;
            }
        }
        cyberdeck_ble::key ble_key;
        if (pressed == key::up) ble_key = cyberdeck_ble::key::up;
        else if (pressed == key::down) ble_key = cyberdeck_ble::key::down;
        else if (pressed == key::enter) ble_key = cyberdeck_ble::key::enter;
        else if (pressed == key::esc) ble_key = cyberdeck_ble::key::escape;
        else goto not_ble_key;
        ble.press(ble_key);
        host_.submit_ble_actions();
        host_.sync_ble_transient();
        host_.render();
        return;
    }
not_ble_key:
    if (wifi_state == wifi_ui_state_t::SEARCH_SELECT) {
        if (pressed == key::up) {
            host_.wifi_search_menu().move_up();
            host_.render();
            return;
        } else if (pressed == key::down) {
            host_.wifi_search_menu().move_down();
            host_.render();
            return;
        } else if (pressed == key::esc) {
            host_.append_output_text(host_.wifi_search_menu().render());
            host_.append_output_line("Wi-Fi search cancelled.\n");
            wifi_state = wifi_ui_state_t::IDLE;
            host_.render();
            return;
        } else if (pressed == key::enter) {
            const auto *ap = host_.wifi_search_menu().selected_item();
            if (ap) {
                selected_ap_ssid_ = ap->ssid;
                host_.append_output_text(host_.wifi_search_menu().render());
                if (ap->is_open) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Connecting to %s...\n", ap->ssid);
                    host_.append_output_line(msg);
                    (void)begin_wifi_connection(ap->ssid, "");
                } else {
                    char saved_pwd[65] = "";
                    bool has_saved = wifi_storage_find(ap->ssid, saved_pwd, sizeof(saved_pwd));
                    if (has_saved) {
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Connecting to %s...\n", ap->ssid);
                        host_.append_output_line(msg);
                        (void)begin_wifi_connection(ap->ssid, saved_pwd);
                        wipe_bytes(saved_pwd, sizeof(saved_pwd));
                    } else {
                        char prompt[160];
                        snprintf(prompt, sizeof(prompt), "Password for %s:\n", ap->ssid);
                        host_.append_output_line(prompt);
                        wifi_state = wifi_ui_state_t::SEARCH_PASSWORD;
                        wipe_string(line_); editor_.clear(); cursor_ = 0;
                    }
                }
            } else {
                wifi_state = wifi_ui_state_t::IDLE;
            }
            host_.render();
            return;
        }
    } else if (wifi_state == wifi_ui_state_t::SAVED_SELECT) {
        if (pressed == key::up) {
            host_.wifi_saved_menu().move_up();
            host_.render();
            return;
        } else if (pressed == key::down) {
            host_.wifi_saved_menu().move_down();
            host_.render();
            return;
        } else if (pressed == key::esc) {
            host_.append_output_text(host_.wifi_saved_menu().render());
            host_.append_output_line("Done.\n");
            wifi_state = wifi_ui_state_t::IDLE;
            host_.render();
            return;
        } else if (pressed == key::enter) {
            std::string ssid_to_forget = host_.wifi_saved_menu().selected_ssid();
            if (!ssid_to_forget.empty()) {
                host_.append_output_line("Press ENTER again to forget this network, or ESC to keep it.\n");
                wifi_state = wifi_ui_state_t::SAVED_CONFIRM;
            }
            host_.render();
            return;
        }
    } else if (wifi_state == wifi_ui_state_t::SAVED_CONFIRM) {
        if (pressed == key::esc) {
            wifi_state = wifi_ui_state_t::SAVED_SELECT;
            host_.render();
            return;
        } else if (pressed == key::enter) {
            std::string ssid_to_forget = host_.wifi_saved_menu().selected_ssid();
            if (!ssid_to_forget.empty()) {
                wifi_mgr_forget(ssid_to_forget.c_str());
                char msg[128];
                snprintf(msg, sizeof(msg), "Forgot network '%s'.\n", ssid_to_forget.c_str());
                host_.append_output_line(msg);
                host_.wifi_saved_menu().remove_selected();
                if (host_.wifi_saved_menu().count() == 0) {
                    host_.append_output_line("No more saved networks.\n");
                    wifi_state = wifi_ui_state_t::IDLE;
                }
            }
            wifi_state = host_.wifi_saved_menu().count() == 0 ? wifi_ui_state_t::IDLE : wifi_ui_state_t::SAVED_SELECT;
            host_.render();
            return;
        }
    } else if (wifi_state == wifi_ui_state_t::SCANNING) {
        if (pressed == key::esc) {
            ++host_.wifi_scan_generation();
            host_.wifi_model().press(cyberdeck_wifi::key::escape);
            host_.wifi_cancel_scan();
            wifi_state = wifi_ui_state_t::IDLE;
            host_.wifi_model_connection_token() = 0;
            host_.wifi_connection_token() = 0;
            host_.append_output_line("Wi-Fi search cancelled.\n");
            host_.render();
            return;
        }
    } else if (wifi_state == wifi_ui_state_t::SEARCH_PASSWORD) {
        if (pressed == key::esc) {
            host_.wifi_model().press(cyberdeck_wifi::key::escape);
            auto actions = host_.wifi_model().take_actions();
            wipe_wifi_actions(actions);
            host_.append_output_line("\nWi-Fi connect cancelled.\n");
            wifi_state = wifi_ui_state_t::IDLE;
            host_.wifi_model_connection_token() = 0;
            host_.wifi_connection_token() = 0;
            wipe_string(line_); editor_.clear(); cursor_ = 0;
            host_.render();
            return;
        }
    }

    if (wifi_state == wifi_ui_state_t::CONNECTING && pressed == key::esc) {
        ++host_.wifi_connection_token(); /* invalidate callbacks before the worker runs */
        host_.wifi_model().cancel_connection();
        auto cancelled_actions = host_.wifi_model().take_actions();
        wipe_wifi_actions(cancelled_actions);
        (void)wifi_mgr_cancel_connection();
        wifi_state = wifi_ui_state_t::IDLE;
        wipe_string(line_); editor_.clear(); cursor_ = 0;
        host_.append_output_line("Wi-Fi connect cancelled.\n");
        host_.render();
        return;
    }

    if (wifi_state == wifi_ui_state_t::CONNECTING) {
        host_.render();
        return;
    }
    sync_editor();
    if (pressed == key::enter) execute_line();
    else if (pressed == key::backspace) editor_.backspace();
    else if (pressed == key::del) editor_.del();
    else if (pressed == key::left) editor_.cursor_left();
    else if (pressed == key::right) editor_.cursor_right();
    else if (pressed == key::home) editor_.cursor_home();
    else if (pressed == key::end) editor_.cursor_end();
    else if (pressed == key::next_tab) editor_.insert("\t", 1);
    else if (pressed == key::up) move_history(-1);
    else if (pressed == key::down) move_history(1);
    sync_line();
    host_.render();
}

} // namespace cyberdeck_shell_session
