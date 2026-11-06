#include "apps/shell/cyberdeck_shell_session.h"

#include "apps/shell/cyberdeck_shell_console.h"
#include "apps/shell/cyberdeck_shell_help.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "apps/shell/cyberdeck_shell_utils.h"

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

bool is_help_request(const std::string &line)
{
    const std::size_t command_start = line.find_first_not_of(" \t");
    if (command_start == std::string::npos || line.compare(command_start, 4, "help") != 0)
        return false;

    const std::size_t argument_start = line.find_first_not_of(" \t", command_start + 4);
    if (argument_start == std::string::npos) return true;
    const std::size_t argument_end = line.find_first_of(" \t", argument_start);
    const std::string_view argument(line.data() + argument_start,
                                    (argument_end == std::string::npos ? line.size() : argument_end) - argument_start);
    if (argument != "-h" && argument != "--help") return false;
    return argument_end == std::string::npos ||
           line.find_first_not_of(" \t", argument_end) == std::string::npos;
}


std::string help_text(cyberdeck_apps::runtime &runtime)
{
    std::string output = cyberdeck_shell_help::text();
    std::array<std::string_view, cyberdeck_apps::k_max_applications *
                                      (cyberdeck_apps::k_max_commands + 1)> listed{};
    std::size_t listed_count = 0;
    for (std::size_t index = 0; index < runtime.size(); ++index) {
        const cyberdeck_apps::application *app = runtime.at(index);
        if (app == nullptr) continue;
        const cyberdeck_apps::manifest &manifest = app->get_manifest();
        const auto append_command = [&](std::string_view command) {
            if (command.empty()) return;
            for (const auto &legacy : cyberdeck_shell_help::kCatalog)
                if (legacy.command == command) return;
            for (std::size_t i = 0; i < listed_count; ++i)
                if (listed[i] == command) return;
            if (listed_count == listed.size()) return;
            listed[listed_count++] = command;
            output += cyberdeck_shell_help::format_app_entry(command);
        };
        append_command(manifest.command);
        for (std::size_t command = 0; command < manifest.command_count; ++command)
            append_command(manifest.commands[command]);
    }
    return output;
}

bool parse_detail_help_request(const std::string &line, std::string_view &command)
{
    const std::size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return false;
    const std::size_t command_end = line.find_first_of(" \t", start);
    if (command_end == std::string::npos) return false;
    const std::size_t help_start = line.find_first_not_of(" \t", command_end);
    if (help_start == std::string::npos || line.compare(help_start, 4, "help") != 0) return false;
    const std::size_t trailing = line.find_first_not_of(" \t", help_start + 4);
    if (trailing != std::string::npos) return false;
    command = std::string_view(line.data() + start, command_end - start);
    return true;
}

std::string app_command_help(cyberdeck_apps::runtime &runtime, std::string_view command)
{
    for (std::size_t index = 0; index < runtime.size(); ++index) {
        const cyberdeck_apps::application *app = runtime.at(index);
        if (app == nullptr) continue;
        const cyberdeck_apps::manifest &manifest = app->get_manifest();
        const std::string_view primary_command = !manifest.command.empty()
                                                     ? std::string_view(manifest.command)
                                                     : manifest.command_count != 0
                                                           ? std::string_view(manifest.commands[0])
                                                           : std::string_view();
        bool matches = manifest.command == command;
        std::size_t command_index = 0;
        for (std::size_t i = 0; !matches && i < manifest.command_count; ++i) {
            matches = manifest.commands[i] == command;
            if (matches) command_index = i;
        }
        if (!matches) continue;

        std::string result(command);
        result += " [app]\n  ";
        result.append(manifest.name.data(), manifest.name.size());
        result += " (";
        result.append(manifest.id.data(), manifest.id.size());
        result += ")";
        if (!manifest.version.empty()) {
            result += " version ";
            result.append(manifest.version.data(), manifest.version.size());
        }
        result += '\n';
        if (!manifest.description.empty()) {
            result += "  ";
            result.append(manifest.description.data(), manifest.description.size());
            result += '\n';
        }
        const bool requested_primary = command_index == 0 && command == primary_command;
        const std::size_t first_help = requested_primary ? 0 : command_index;
        const std::size_t help_end = requested_primary
                                         ? manifest.command_help_entries.size()
                                         : first_help + 1;
        for (std::size_t help_index = first_help; help_index < help_end; ++help_index) {
            const auto &details = manifest.command_help_entries[help_index];
            if (!details.usage.empty()) {
                result += "  usage: ";
                result.append(details.usage.data(), details.usage.size());
                result += '\n';
            }
            if (!details.parameters.empty()) {
                /* Indent every line of the explanation block by the same two
                 * spaces, so a multi-line command keeps the column of the
                 * legacy row it replaced. */
                std::string_view block = details.parameters;
                while (!block.empty()) {
                    const std::size_t end = block.find('\n');
                    const std::string_view line = end == std::string_view::npos ? block : block.substr(0, end);
                    if (!line.empty()) {
                        result += "  ";
                        result.append(line.data(), line.size());
                    }
                    result += '\n';
                    if (end == std::string_view::npos) break;
                    block = block.substr(end + 1);
                }
            }
        }
        return result;
    }
    return {};
}

} // namespace

session::session(host &host) : host_(host)
{
    /* A new console restores every session-scoped command setting (the log
     * window and the survey duration) so nothing leaks from a previous session. */
    host_.app_runtime().reset_session_states();
}

void session::sync_editor()
{
    editor_.set_session(host_.ssh_phase());
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

void session::clear_ble_auth_input()
{
    wipe_string(ble_auth_input_);
}

void session::append_ble_auth_digit(char digit)
{
    if (ble_auth_input_.size() >= cyberdeck_ble::k_passkey_digits) return;
    if (digit < '0' || digit > '9') return;
    ble_auth_input_.push_back(digit);
}

void session::invalidate_wifi_connection()
{
    wifi_model_connection_token_ = 0;
    wifi_connection_token_ = 0;
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
    if (actions.empty() || actions.front().kind != cyberdeck_wifi::action_kind::connect) {
        /* An unexpected action may still carry a password; never leave it
         * resident on the early return. */
        wipe_wifi_actions(actions);
        return false;
    }
    const std::uint64_t model_token = actions.front().token;
    if (host_.wifi_connect(ssid, password) != ESP_OK) {
        wifi.connection_callback(model_token, cyberdeck_wifi::connection_event::failed);
        auto failed_actions = wifi.take_actions();
        wipe_wifi_actions(failed_actions);
        host_.wifi_state() = wifi_ui_state_t::IDLE;
        invalidate_wifi_connection();
        clear_editor();
        host_.append_output_line("Wi-Fi connection could not be started.\n");
        wipe_wifi_actions(actions);
        return false;
    }
    wifi_model_connection_token_ = model_token;
    wifi_connection_token_ = host_.wifi_current_token();
    host_.wifi_state() = wifi_ui_state_t::CONNECTING;
    wipe_wifi_actions(actions);
    return true;
}

void session::execute_line(bool line_already_sent)
{
    if (poweroff_requested_) { clear_editor(); host_.render(); return; }

    const cyberdeck_session_state state = host_.ssh_phase();
    if (poweroff_confirmation_ && state != cyberdeck_session_state::MENU) {
        poweroff_confirmation_ = false;
        clear_editor();
        host_.append_output_line("poweroff: cancelled; shell context changed\n");
        host_.render();
        return;
    }
    /* enter() clears the editor, so reject a connected Enter first. */
    if (state == cyberdeck_session_state::CONNECTED && host_.ssh_composer().active()) {
        host_.render();
        return;
    }
    /* Capture the prompt context before executing the command: `cd` changes
     * the shell cwd, but its echo must describe the directory it came from. */
    const std::string command_cwd = state == cyberdeck_session_state::MENU
                                      ? host_.local_shell().cwd()
                                      : std::string();
    sync_editor();
    cyberdeck_enter_result entered = editor_.enter(line_already_sent, command_cwd);
    string_wiper entered_wiper{entered.payload};
    std::string line = entered.payload;
    string_wiper line_wiper{line};
    clear_editor();
    history_.reset_position();

    if (state == cyberdeck_session_state::HOST_KEY) {
        host_.log_event('I', "shell", "host key accepted");
        host_.ssh_accept_host_key();
        host_.render();
        return;
    }
    if (state == cyberdeck_session_state::CONNECTED) {
        if (entered.action == cyberdeck_enter_action::SEND_LINE_NEWLINE ||
            entered.action == cyberdeck_enter_action::SEND_NEWLINE) {
            host_.log_event('I', "ssh", "command sent to interactive session");
            host_.reset_ssh_filter();
            const size_t command_length = entered.payload.empty() ? 0 : entered.payload.size() - 1;
            std::string local(command_length + 1, '\0');
            const size_t local_size = host_.ssh_composer().begin(
                entered.payload.data(), command_length, &local[0], local.size());
            host_.write_output(local.data(), local_size);
            if (host_.ssh_send_data(entered.payload.data(), entered.payload.size()) != ESP_OK) {
                host_.discard_ssh_composer();
            }
        } else {
            host_.render();
            return;
        }
        host_.render();
        return;
    }
    if (poweroff_confirmation_) {
        poweroff_confirmation_ = false;
        if (state == cyberdeck_session_state::MENU && line == "y") {
            poweroff_requested_ = true;
            const auto result = host_.admit_poweroff();
            switch (result) {
            case cyberdeck_poweroff::admission_result::admitted:
                host_.append_output_line("poweroff: admitted; quiescing storage. Do not remove power.\n");
                break;
            case cyberdeck_poweroff::admission_result::unavailable:
                host_.append_output_line("poweroff: coordinator unavailable; reboot required.\n");
                break;
            case cyberdeck_poweroff::admission_result::already_requested:
                host_.append_output_line("poweroff: request already consumed; reboot required.\n");
                break;
            case cyberdeck_poweroff::admission_result::fence_failed:
                host_.append_output_line("poweroff: storage fence unavailable; reboot required.\n");
                break;
            case cyberdeck_poweroff::admission_result::watchdog_unavailable:
                host_.append_output_line("poweroff: watchdog unavailable; reboot required.\n");
                break;
            case cyberdeck_poweroff::admission_result::task_unavailable:
                host_.append_output_line("poweroff: shutdown task unavailable; reboot required.\n");
                break;
            }
        } else {
            host_.append_output_line("poweroff: cancelled\n");
        }
        host_.render();
        return;
    }
    if (line.find_first_not_of(" \t") == std::string::npos) {
        host_.render();
        return;
    }
    if (state == cyberdeck_session_state::PASSWORD) {
        if (entered.action == cyberdeck_enter_action::SEND_PASSWORD)
            host_.ssh_send_password(line.c_str());
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
    const bool help_request = is_help_request(line);
    host_.append_output_line(entered.echo);
    host_.log_event('I', "shell", line.c_str());
    history_.add(line);
    if (line == "poweroff") {
        poweroff_confirmation_ = true;
        host_.append_output_line("Power off this device? Type exactly y then Enter; any other input cancels.\n");
        host_.render();
        return;
    }
    if (line == "poweroff -h" || line == "poweroff --help") {
        host_.append_output_line(cyberdeck_command_help_text("poweroff"));
        host_.render();
        return;
    }
    /* Resolve help in the composed session before the local shell handles its
     * legacy command, so registered application manifests are included. */
    if (help_request) {
        host_.append_output_line(help_text(host_.app_runtime()));
        host_.render();
        return;
    }
    std::string_view detail_command;
    if (parse_detail_help_request(line, detail_command)) {
        const std::string command(detail_command);
        std::string details = cyberdeck_shell_help::command_text(command.c_str());
        if (details.empty()) details = app_command_help(host_.app_runtime(), detail_command);
        if (details.empty()) {
            details = "help: no details available for command '";
            details.append(detail_command.data(), detail_command.size());
            details += "'\n";
        }
        host_.append_output_line(details);
        host_.render();
        return;
    }
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
    /* The supervisor owns `app` and every command declared by a registered
     * application: `wifi`, `ssh`, `bluetooth`, `battery`, `screen` and `log` are
     * console applications, so the dispatcher resolves them here and their
     * reply is rendered without touching the legacy parser.  What still reaches
     * the parser below is only what the session owns directly (local commands,
     * `help`, `clear` and the fail-closed unknown path). */
    cyberdeck_apps::runtime &runtime = host_.app_runtime();
    const cyberdeck_shell_console::dispatcher dispatcher(&runtime);
    if (dispatcher.resolve(line) == cyberdeck_shell_console::dispatch_target::supervisor) {
        const cyberdeck_apps::result app = runtime.execute_line(line);
        if (app.status == cyberdeck_apps::result_status::handled ||
            app.status == cyberdeck_apps::result_status::rejected) {
            if (!app.output.empty()) host_.append_output_line(app.output);
            host_.render();
            return;
        }
    }
    cyberdeck_cmd_t cmd = cyberdeck_parse_command(line.c_str());
    switch (cmd.type) {
        case CYBERDECK_CMD_CLEAR:
            host_.clear_output();
            host_.reset_ssh_filter();
            host_.discard_ssh_composer();
            break;
    case CYBERDECK_CMD_EMPTY: break;
    default: host_.append_output_line("unknown command; type help\n"); break;
    }
    host_.render();
}

void session::handle_key(key pressed)
{
    if (poweroff_requested_) return;
    if (poweroff_confirmation_ && pressed == key::esc) {
        poweroff_confirmation_ = false;
        clear_editor();
        host_.append_output_line("poweroff: cancelled\n");
        host_.render();
        return;
    }
    cyberdeck_ble::state_machine &ble = host_.ble_model();
    wifi_ui_state_t &wifi_state = host_.wifi_state();
    /* A successful BLE connection releases model ownership to idle while
     * ble_mgr keeps the link.  Escape there is the explicit manual
     * disconnect: route it to the model so the action reaches the manager
     * and blocks the background cycle, then submit it like owned keys.
     * Other modals (Wi-Fi/SSH/password) keep their own Escape handling. */
    if (pressed == key::esc && !ble.owns_input() && ble.is_connected() &&
        wifi_state == wifi_ui_state_t::IDLE &&
        host_.ssh_phase() != cyberdeck_session_state::PASSWORD &&
        host_.ssh_phase() != cyberdeck_session_state::CONNECTED) {
        ble.press(cyberdeck_ble::key::escape);
        host_.submit_ble_actions();
        host_.sync_ble_transient();
        host_.render();
        return;
    }
    if (ble.owns_input()) {
        if (ble.current_screen() == cyberdeck_ble::screen::auth) {
            if (pressed == key::backspace || pressed == key::del) {
                /* pop_back() would only shrink the string; the removed digit
                 * would stay resident in the capacity, so wipe it first. */
                if (!ble_auth_input_.empty()) {
                    ble_auth_input_.pop_back();
                    wipe_string(ble_auth_input_);
                }
                host_.render();
                return;
            }
            if (pressed == key::enter &&
                ble.pending_auth_action() == cyberdeck_ble::auth_io_action::input) {
                std::uint32_t value = 0;
                if (ble_auth_input_.size() == cyberdeck_ble::k_passkey_digits &&
                    cyberdeck_ble::parse_passkey(ble_auth_input_.data(), ble_auth_input_.size(), value)) {
                    ble.submit_auth(value);
                    clear_ble_auth_input();
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
                    bool has_saved = host_.wifi_storage_find(ap->ssid, saved_pwd, sizeof(saved_pwd));
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
                        clear_editor();
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
                host_.wifi_forget(ssid_to_forget.c_str());
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
            invalidate_wifi_connection();
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
            invalidate_wifi_connection();
            clear_editor();
            host_.render();
            return;
        }
    }

    if (wifi_state == wifi_ui_state_t::CONNECTING && pressed == key::esc) {
        /* Invalidate both tokens before the worker runs, so a late callback
         * cannot be matched against this cancelled attempt. */
        invalidate_wifi_connection();
        host_.wifi_model().cancel_connection();
        auto cancelled_actions = host_.wifi_model().take_actions();
        wipe_wifi_actions(cancelled_actions);
        (void)host_.wifi_cancel_connection();
        wifi_state = wifi_ui_state_t::IDLE;
        clear_editor();
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

bool session::insert_physical_text(const char *text, std::size_t length)
{
    if (poweroff_requested_) return false;
    if (text == nullptr || text[0] == '\0' || length == 0) return false;
    /* While the BLE auth screen owns input, only decimal digits reach the
     * bounded passkey buffer; everything else is rejected. */
    cyberdeck_ble::state_machine &ble = host_.ble_model();
    if (ble.current_screen() == cyberdeck_ble::screen::auth &&
        ble.pending_auth_action() == cyberdeck_ble::auth_io_action::input) {
        for (std::size_t i = 0; i < length; ++i) append_ble_auth_digit(text[i]);
        host_.render();
        return false;
    }
    sync_editor();
    if (!editor_.insert_physical(text, length)) return false;
    sync_line();
    host_.render();
    return true;
}

bool session::insert_modified_key(char character, std::uint8_t modifier)
{
    if (poweroff_requested_) return false;
    /* A modified key is an SSH escape sequence, not editor text.  It only
     * applies to an interactive session; the menu owns plain text. */
    if (host_.ssh_phase() != cyberdeck_session_state::CONNECTED) return false;
    const std::string sequence = cyberdeck_encode_ssh_key(
        static_cast<std::uint8_t>(character), modifier);
    if (sequence.empty()) return false;
    return host_.ssh_send_data(sequence.data(), sequence.size()) == ESP_OK;
}

bool session::insert_virtual_text(const char *text, bool *virtual_enter_handled)
{
    if (virtual_enter_handled != nullptr) *virtual_enter_handled = false;
    if (poweroff_requested_) return false;
    if (text == nullptr || *text == '\0') return false;
    /* The virtual keyboard reports editing keys through a single control
     * byte.  They are actions, not text, so route them as keys instead of
     * inserting them. */
    if (text[1] == '\0') {
        const unsigned char control = static_cast<unsigned char>(text[0]);
        key editing = key::unknown;
        if (control == 0x7F) editing = key::backspace;
        else if (control == 0x08) editing = key::del;
        if (editing != key::unknown) {
            handle_key(editing);
            return true;
        }
    }
    cyberdeck_ble::state_machine &ble = host_.ble_model();
    if (ble.current_screen() == cyberdeck_ble::screen::auth &&
        ble.pending_auth_action() == cyberdeck_ble::auth_io_action::input) {
        for (const char *p = text; *p != '\0'; ++p) {
            if (*p == '\n' || *p == '\r') handle_key(key::enter);
            else append_ble_auth_digit(*p);
        }
        host_.render();
        return false;
    }
    /* A paste may carry several newlines.  Keep every part of the chunk and
     * execute each line exactly once instead of dropping the insertion. */
    sync_editor();
    const char *part = text;
    bool inserted_any = false;
    while (*part != '\0') {
        const char *newline = strchr(part, '\n');
        const std::size_t length = newline != nullptr
                                 ? static_cast<std::size_t>(newline - part)
                                 : strlen(part);
        if (length != 0 && editor_.insert_virtual(part, length)) {
            sync_line();
            inserted_any = true;
        }
        if (newline == nullptr) break;
        execute_line();
        if (poweroff_requested_) {
            if (virtual_enter_handled != nullptr) *virtual_enter_handled = true;
            return inserted_any;
        }
        part = newline + 1;
        sync_editor();
        /* CONNECTED can have only one line in flight.  Stop after the first
         * Enter while its echo is pending, retaining the next pasted line for
         * editing instead of turning it into another send. */
        if (host_.ssh_phase() == cyberdeck_session_state::CONNECTED &&
            host_.ssh_composer().active()) {
            const char *next_newline = strchr(part, '\n');
            const std::size_t remainder = next_newline != nullptr
                                       ? static_cast<std::size_t>(next_newline - part)
                                       : strlen(part);
            if (remainder != 0 && editor_.insert_virtual(part, remainder)) {
                sync_line();
                inserted_any = true;
            }
            break;
        }
    }
    if (virtual_enter_handled != nullptr) {
        const char *last_newline = strrchr(text, '\n');
        *virtual_enter_handled = last_newline != nullptr && last_newline[1] == '\0';
    }
    host_.render();
    return inserted_any;
}

} // namespace cyberdeck_shell_session
