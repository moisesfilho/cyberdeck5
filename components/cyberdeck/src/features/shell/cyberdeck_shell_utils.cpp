#include "features/shell/cyberdeck_shell_help.h"
#include "features/shell/cyberdeck_shell_utils.h"
#include "lvgl.h"

#include <cstdlib>
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
    while (!command.empty() && (command.back() == ' ' || command.back() == '\t' || command.back() == '\r' || command.back() == '\n')) {
        command.pop_back();
    }

    /* The screen verb accepts the same horizontal separators as the other
     * shell verbs; keep the normalized spelling local to this command. */
    std::string screen_command;
    screen_command.reserve(command.size());
    bool separator = false;
    for (char value : command) {
        if (value == '\t' || value == '\v' || value == '\f') {
            value = ' ';
        }
        if (value == ' ') {
            if (!separator) {
                screen_command.push_back(' ');
            }
            separator = true;
        } else {
            screen_command.push_back(value);
            separator = false;
        }
    }

    if (command == "help") {
        cmd.type = CYBERDECK_CMD_HELP;
    } else if (command == "clear") {
        cmd.type = CYBERDECK_CMD_CLEAR;
    } else if (command == "wifi") {
        cmd.type = CYBERDECK_CMD_WIFI;
    } else if (command == "wifi search") {
        cmd.type = CYBERDECK_CMD_WIFI_SEARCH;
    } else if (command == "wifi saved") {
        cmd.type = CYBERDECK_CMD_WIFI_SAVED;
    } else if (command == "wifi audit") {
        cmd.type = CYBERDECK_CMD_WIFI_AUDIT;
    } else if (command == "wifi audit save") {
        cmd.type = CYBERDECK_CMD_WIFI_AUDIT_SAVE;
    } else if (command == "log") {
        cmd.type = CYBERDECK_CMD_LOG;
    } else if (command == "screen on" || screen_command == "screen on") {
        cmd.type = CYBERDECK_CMD_SCREEN_ON;
    } else if (command == "screen off" || screen_command == "screen off") {
        cmd.type = CYBERDECK_CMD_SCREEN_OFF;
    } else if (command == "screen timeout" ||
               screen_command == "screen timeout" ||
               (screen_command.rfind("screen timeout", 0) == 0 &&
                screen_command.size() > 14 &&
                screen_command[14] == ' ')) {
        cmd.type = CYBERDECK_CMD_SCREEN_TIMEOUT;
        const size_t first = screen_command.find_first_not_of(" \t", 14);
        if (first != std::string::npos) {
            const size_t last = screen_command.find_last_not_of(" \t");
            const std::string args = screen_command.substr(first, last - first + 1);
            if (args.find_first_of(" \t\r\n") == std::string::npos) {
                cmd.args = args;
            } else {
                cmd.type = CYBERDECK_CMD_UNKNOWN;
            }
        }
    } else if (command.rfind("ssh", 0) == 0 && (command.size() == 3 || command[3] == ' ' || command[3] == '\t')) {
        cmd.type = CYBERDECK_CMD_SSH;
        if (command.size() > 3) {
            const size_t first = command.find_first_not_of(" \t", 3);
            if (first != std::string::npos) {
                cmd.args = command.substr(first);
            }
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
// renderable help list in this translation unit.
constexpr bool help_catalog_descriptions_match_contract()
{
    const auto &catalog = cyberdeck_shell_help::kCatalog;
    return catalog[0].description == "show this help" &&
           catalog[1].description == "show status, manage Wi-Fi, or audit" &&
           catalog[2].description == "show recent events" &&
           catalog[3].description == "clear the terminal" &&
           catalog[4].description == "control screen protection" &&
           catalog[5].description == "start an SSH session" &&
           catalog[6].description == "print working directory" &&
           catalog[7].description == "change working directory" &&
           catalog[8].description == "list directory contents" &&
           catalog[9].description == "print a regular file" &&
           catalog[10].description == "create an empty file" &&
           catalog[11].description == "create a directory" &&
           catalog[12].description == "remove a file or directory" &&
           catalog[13].description == "remove an empty directory";
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
