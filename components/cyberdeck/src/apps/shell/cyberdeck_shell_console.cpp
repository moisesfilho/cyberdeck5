#include "apps/shell/cyberdeck_shell_console.h"

#include "apps/shell/cyberdeck_shell_help.h"

namespace cyberdeck_shell_console {
namespace {

constexpr std::size_t k_max_line_bytes = 256;

bool is_space(char value) { return value == ' ' || value == '\t'; }

} // namespace

std::size_t utf8_char_count(std::string_view text)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else i += 1;
        count++;
    }
    return count;
}

std::size_t utf8_valid_start_offset(std::string_view text, std::size_t drop_bytes)
{
    if (drop_bytes >= text.size()) return text.size();
    while (drop_bytes < text.size() &&
           (static_cast<unsigned char>(text[drop_bytes]) & 0xC0) == 0x80) {
        drop_bytes++;
    }
    return drop_bytes;
}

std::string truncate_left_utf8(std::string_view text, std::size_t max_bytes)
{
    if (text.size() <= max_bytes) return std::string(text);
    if (max_bytes == 0) return {};
    return std::string(text.substr(utf8_valid_start_offset(text, text.size() - max_bytes)));
}

std::string fit_prompt_marker(std::string_view marker)
{
    if (marker.size() <= k_terminal_limit) return std::string(marker);

    /* The local prompt must retain its final directory component and the
     * shell terminator even when a deeply nested path exceeds the terminal
     * budget.  The marker is always formed as <cwd> + "$ ". */
    constexpr std::size_t prompt_suffix_size = 2;
    if (marker.size() >= prompt_suffix_size &&
        marker.compare(marker.size() - prompt_suffix_size, prompt_suffix_size, "$ ") == 0) {
        const std::size_t cwd_budget =
            k_terminal_limit > prompt_suffix_size ? k_terminal_limit - prompt_suffix_size : 0;
        return truncate_left_utf8(marker.substr(0, marker.size() - prompt_suffix_size), cwd_budget) +
               "$ ";
    }
    return truncate_left_utf8(marker, k_terminal_limit);
}

std::string fit_visible_line(std::string_view line, std::size_t marker_bytes, std::size_t limit)
{
    const std::size_t budget = limit > marker_bytes ? limit - marker_bytes : 0;
    return truncate_left_utf8(line, budget);
}

const char *session_mode_name(session_mode mode)
{
    switch (mode) {
    case session_mode::menu: return "menu";
    case session_mode::ssh_host_key: return "ssh_host_key";
    case session_mode::ssh_password: return "ssh_password";
    case session_mode::ssh_interactive: return "ssh_interactive";
    }
    return "menu";
}

session_mode mode_for(cyberdeck_session_state state)
{
    switch (state) {
    case cyberdeck_session_state::MENU: return session_mode::menu;
    case cyberdeck_session_state::PASSWORD: return session_mode::ssh_password;
    case cyberdeck_session_state::HOST_KEY: return session_mode::ssh_host_key;
    case cyberdeck_session_state::CONNECTED: return session_mode::ssh_interactive;
    }
    return session_mode::menu;
}

std::string line_view::text() const { return marker + fitted_line; }

std::size_t line_view::cursor_chars() const
{
    if (cursor_bytes < line_start) return utf8_char_count(marker);
    return utf8_char_count(marker) +
           utf8_char_count(visible_line.substr(line_start, cursor_bytes - line_start));
}

std::size_t line_view::reserved() const { return marker.size() + fitted_line.size(); }

line_view compose(const surface_state &state, const line_input &input, std::size_t limit)
{
    line_view view;
    view.visible_line = state.ssh_connected
                            ? input.visible_line
                            : (state.password_pending ? std::string(input.line.size(), '*')
                                                      : input.line);
    if (state.ssh_connected) {
        /* SSH online: o terminal remoto desenha o proprio prompt. */
        view.marker.clear();
    } else if (state.password_pending) {
        view.marker = "Password: ";
    } else if (!state.input_owned_elsewhere) {
        view.marker = fit_prompt_marker(state.cwd + "$ ");
    }
    view.fitted_line = fit_visible_line(view.visible_line, view.marker.size(), limit);
    view.line_start = view.visible_line.size() - view.fitted_line.size();

    view.cursor_bytes = input.cursor_bytes;
    if (view.cursor_bytes > input.line.size()) view.cursor_bytes = input.line.size();
    if (view.cursor_bytes < view.line_start) view.cursor_bytes = view.line_start;
    return view;
}

std::string_view first_token(std::string_view line)
{
    if (line.size() > k_max_line_bytes) line = line.substr(0, k_max_line_bytes);
    std::size_t begin = 0;
    while (begin < line.size() && is_space(line[begin])) ++begin;
    std::size_t end = begin;
    while (end < line.size() && !is_space(line[end])) ++end;
    return line.substr(begin, end - begin);
}

bool is_legacy_command(std::string_view command)
{
    if (command.empty()) return false;
    for (const cyberdeck_shell_help::entry &item : cyberdeck_shell_help::kCatalog) {
        if (item.command == command) return true;
    }
    return false;
}

bool dispatcher::declares_command(std::string_view command) const
{
    if (runtime_ == nullptr || command.empty()) return false;
    for (std::size_t index = 0; index < runtime_->size(); ++index) {
        const cyberdeck_apps::application *app = runtime_->at(index);
        if (app == nullptr) continue;
        const cyberdeck_apps::manifest &item = app->get_manifest();
        if (item.command == command) return true;
        for (std::size_t declared = 0; declared < item.command_count; ++declared) {
            if (item.commands[declared] == command) return true;
        }
    }
    return false;
}

dispatch_target dispatcher::resolve(std::string_view line) const
{
    const std::string_view token = first_token(line);
    if (token.empty()) return dispatch_target::console;
    /* The supervisor owns `app` exclusively: a malformed or partially typed
     * `app` line must never fall through to the legacy parser. */
    if (token == k_supervisor_command) return dispatch_target::supervisor;
    /* A declared application command never shadows a legacy command. */
    if (is_legacy_command(token)) return dispatch_target::console;
    return declares_command(token) ? dispatch_target::supervisor : dispatch_target::console;
}

std::size_t dispatcher::collisions(std::array<std::string_view, k_max_collisions> &out) const
{
    std::size_t count = 0;
    if (runtime_ == nullptr) return 0;
    for (std::size_t index = 0; index < runtime_->size(); ++index) {
        const cyberdeck_apps::application *app = runtime_->at(index);
        if (app == nullptr) continue;
        const cyberdeck_apps::manifest &item = app->get_manifest();
        for (std::size_t declared = 0; declared < item.command_count; ++declared) {
            const std::string_view command = item.commands[declared];
            if (command.empty() || !is_legacy_command(command)) continue;
            if (count == out.size()) return count;
            out[count++] = command;
        }
    }
    return count;
}

} // namespace cyberdeck_shell_console