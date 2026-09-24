#pragma once

#include "features/shell/cyberdeck_shell_help.h"

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

enum cyberdeck_cmd_type_t {
    CYBERDECK_CMD_EMPTY = 0,
    CYBERDECK_CMD_HELP,
    CYBERDECK_CMD_CLEAR,
    CYBERDECK_CMD_WIFI,
    CYBERDECK_CMD_WIFI_SEARCH,
    CYBERDECK_CMD_WIFI_SAVED,
    CYBERDECK_CMD_WIFI_AUDIT,
    CYBERDECK_CMD_WIFI_AUDIT_SAVE,
    CYBERDECK_CMD_LOG,
    CYBERDECK_CMD_SCREEN_ON,
    CYBERDECK_CMD_SCREEN_OFF,
    CYBERDECK_CMD_SCREEN_TIMEOUT,
    CYBERDECK_CMD_SSH,
    CYBERDECK_CMD_UNKNOWN
};

#ifdef __cplusplus
}
#endif

struct cyberdeck_cmd_t {
    cyberdeck_cmd_type_t type;
    std::string args;
    bool confirmed = false;
};

/**
 * Faz o parsing do alvo SSH no formato [user@]host[:port].
 * Retorna true se a string for valida, preenchendo user, host e port.
 */
bool cyberdeck_parse_ssh_target(const char *target, std::string &user, std::string &host, int &port);

/**
 * Converte tecla e modificadores (Ctrl/Alt) para sequencia de bytes ANSI/VT100 para envio SSH.
 */
std::string cyberdeck_encode_ssh_key(uint32_t key, uint8_t modifier);

/**
 * Identifica o comando digitado no shell e extrai os argumentos.
 */
cyberdeck_cmd_t cyberdeck_parse_command(const char *input);

/**
 * Retorna o texto de ajuda do shell a partir do catalogo compartilhado.
 */
std::string cyberdeck_help_text();

/**
 * Retorna a linha de ajuda de um comando local a partir do mesmo catalogo.
 */
std::string cyberdeck_command_help_text(const char *command);
