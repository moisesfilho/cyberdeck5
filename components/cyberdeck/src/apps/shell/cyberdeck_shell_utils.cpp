#include "apps/shell/cyberdeck_shell_help.h"
#include "apps/shell/cyberdeck_shell_utils.h"
#include "lvgl.h"


#include <cstring>

bool cyberdeck_parse_ssh_target(const char *target, std::string &user, std::string &host, int &port)
{
    if (target == nullptr || target[0] == '\0') {
        return false;
    }
    for (const char *p = target; *p != '\0'; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '\v' || *p == '\f') {
            return false;
        }
    }

    std::string value(target);
    std::string parsed_user = "root";
    const size_t at = value.find('@');
    if (at != std::string::npos) {
        if (at == 0 || value.find('@', at + 1) != std::string::npos) {
            return false;
        }
        parsed_user = value.substr(0, at);
        value.erase(0, at + 1);
    }

    std::string parsed_host;
    int parsed_port = 22;

    if (!value.empty() && value.front() == '[') {
        // Formato IPv6 bracketado: [2001:db8::1] ou [2001:db8::1]:22
        const size_t closing = value.find(']');
        if (closing == std::string::npos || closing == 1) {
            return false;
        }
        parsed_host = value.substr(1, closing - 1);
        if (parsed_host.find(':') == std::string::npos || parsed_host == ":") {
            return false;
        }
        if (parsed_host.front() == ':' && (parsed_host.size() < 2 || parsed_host[1] != ':')) {
            return false;
        }
        if (parsed_host.back() == ':' && (parsed_host.size() < 2 || parsed_host[parsed_host.size() - 2] != ':')) {
            return false;
        }
        for (char c : parsed_host) {
            const bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!is_hex && c != ':' && c != '.' && c != '%') {
                return false;
            }
        }
        if (closing + 1 == value.size()) {
            parsed_port = 22;
        } else if (value[closing + 1] == ':') {
            const std::string port_text = value.substr(closing + 2);
            if (port_text.empty()) {
                return false;
            }
            for (char c : port_text) {
                if (c < '0' || c > '9') {
                    return false;
                }
            }
            const long parsed = strtol(port_text.c_str(), nullptr, 10);
            if (parsed < 1 || parsed > 65535) {
                return false;
            }
            parsed_port = static_cast<int>(parsed);
        } else {
            return false;
        }
    } else {
        const size_t first_colon = value.find(':');
        const size_t last_colon = value.rfind(':');
        if (first_colon != std::string::npos) {
            if (first_colon != last_colon) {
                // Múltiplos dois-pontos sem encapsulamento [ipv6] são inválidos
                return false;
            }
            if (last_colon == 0 || last_colon + 1 == value.size()) {
                return false;
            }
            const std::string port_text = value.substr(last_colon + 1);
            for (char c : port_text) {
                if (c < '0' || c > '9') {
                    return false;
                }
            }
            const long parsed = strtol(port_text.c_str(), nullptr, 10);
            if (parsed < 1 || parsed > 65535) {
                return false;
            }
            parsed_port = static_cast<int>(parsed);
            parsed_host = value.substr(0, last_colon);
        } else {
            parsed_host = value;
        }
    }

    if (parsed_host.empty() || parsed_user.empty()) {
        return false;
    }

    user = parsed_user;
    host = parsed_host;
    port = parsed_port;
    return true;
}

std::string cyberdeck_encode_ssh_key(uint32_t key, uint8_t modifier)
{
    std::string result;
    const bool ctrl = (modifier & 0x01U) != 0;
    const bool alt = (modifier & 0x04U) != 0;

    if (ctrl) {
        if (key >= 'a' && key <= 'z') {
            const char control = static_cast<char>(key - 'a' + 1);
            if (alt) {
                result.push_back('\x1B');
            }
            result.push_back(control);
            return result;
        }
        if (key >= 'A' && key <= 'Z') {
            const char control = static_cast<char>(key - 'A' + 1);
            if (alt) {
                result.push_back('\x1B');
            }
            result.push_back(control);
            return result;
        }
        if (key == '@' || key == ' ') {
            if (alt) {
                result.push_back('\x1B');
            }
            result.push_back('\x00');
            return result;
        }
        if (key == '[') {
            if (alt) {
                result.push_back('\x1B');
            }
            result.push_back('\x1B');
            return result;
        }
        if (key == '\\') {
            if (alt) {
                result.push_back('\x1B');
            }
            result.push_back('\x1C');
            return result;
        }
        if (key == ']') {
            if (alt) {
                result.push_back('\x1B');
            }
            result.push_back('\x1D');
            return result;
        }
        if (key == '^') {
            if (alt) {
                result.push_back('\x1B');
            }
            result.push_back('\x1E');
            return result;
        }
        if (key == '_') {
            if (alt) {
                result.push_back('\x1B');
            }
            result.push_back('\x1F');
            return result;
        }
    }

    std::string key_seq;
    switch (key) {
    case LV_KEY_ENTER:
        key_seq.push_back('\n');
        break;
    case LV_KEY_BACKSPACE:
        key_seq.push_back('\x7F');
        break;
    case LV_KEY_DEL:
        key_seq.append("\x1B[3~");
        break;
    case LV_KEY_ESC:
        key_seq.push_back('\x1B');
        break;
    case LV_KEY_UP:
        key_seq.append("\x1B[A");
        break;
    case LV_KEY_DOWN:
        key_seq.append("\x1B[B");
        break;
    case LV_KEY_LEFT:
        key_seq.append("\x1B[D");
        break;
    case LV_KEY_RIGHT:
        key_seq.append("\x1B[C");
        break;
    case LV_KEY_HOME:
        key_seq.append("\x1B[H");
        break;
    case LV_KEY_END:
        key_seq.append("\x1B[F");
        break;
    case '\t':
        key_seq.push_back('\t');
        break;
    default:
        if (key <= 0xFFU) {
            key_seq.push_back(static_cast<char>(key));
        }
        break;
    }

    if (key_seq.empty()) {
        return "";
    }

    if (alt) {
        result.push_back('\x1B');
    }
    result.append(key_seq);
    return result;
}

cyberdeck_cmd_t cyberdeck_parse_command(const char *input)
{
    cyberdeck_cmd_t cmd = {CYBERDECK_CMD_EMPTY, ""};
    if (input == nullptr) {
        return cmd;
    }

    // Skip leading spaces/tabs
    while (*input == ' ' || *input == '\t') {
        input++;
    }
    if (*input == '\0') {
        return cmd;
    }

    std::string command(input);
    // Trim trailing spaces/newlines
    while (!command.empty() && (command.back() == ' ' || command.back() == '\t' ||
                                command.back() == '\r' || command.back() == '\n')) {
        command.pop_back();
    }

    /* The migrated commands (wifi, log, screen, battery, bluetooth, ssh) are
     * declared by their own manifests and never reach this parser: the console
     * dispatcher resolves them to the supervisor first.  What remains here is
     * the surface the session still owns directly. */
    if (command == "help") {
        cmd.type = CYBERDECK_CMD_HELP;
    } else if (command == "clear") {
        cmd.type = CYBERDECK_CMD_CLEAR;
    } else if (command == "app" || (command.rfind("app", 0) == 0 &&
                                    (command[3] == ' ' || command[3] == '\t'))) {
        cmd.type = CYBERDECK_CMD_APP;
        if (command.size() > 3) {
            const size_t first = command.find_first_not_of(" \t", 3);
            if (first != std::string::npos) cmd.args = command.substr(first);
        }
    } else {
        cmd.type = CYBERDECK_CMD_UNKNOWN;
        cmd.args = command;
    }

    return cmd;
}

namespace {

// The runtime catalog is owned by the pure header.  These compile-time
// checks keep the public shell adapter's approved descriptions visible and
// fail the build if the shared table drifts, without creating a second
// renderable help list in this translation unit.  Lookup is by command name on
// purpose: the catalog is the migration frontier, so its size and the position
// of a given row must not be pinned here.
constexpr bool has_description(std::string_view command, std::string_view description)
{
    for (const cyberdeck_shell_help::entry &item : cyberdeck_shell_help::kCatalog)
        if (item.command == command) return item.description == description;
    return false;
}

constexpr bool help_catalog_descriptions_match_contract()
{
    return has_description("help", "show this help") &&
           has_description("clear", "clear the terminal") &&
           has_description("app", "manage compiled-in applications") &&
           has_description("pwd", "print working directory") &&
           has_description("cd", "change working directory") &&
           has_description("ls", "list directory contents") &&
           has_description("cat", "print a regular file") &&
           has_description("touch", "create an empty file") &&
           has_description("mkdir", "create a directory") &&
           has_description("rm", "remove a file or directory") &&
           has_description("rmdir", "remove an empty directory") &&
           has_description("poweroff", "power off this device after exact y confirmation");
}

static_assert(help_catalog_descriptions_match_contract(),
              "cyberdeck shell help catalog descriptions drifted");

} // namespace

std::string cyberdeck_help_text()
{
    return cyberdeck_shell_help::text();
}

std::string cyberdeck_command_help_text(const char *command)
{
    return cyberdeck_shell_help::command_text(command);
}
