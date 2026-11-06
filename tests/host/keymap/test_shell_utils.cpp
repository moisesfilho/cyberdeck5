/*
 * Testes unitarios host-side para a logica pura de shell e traducao SSH/VT100.
 * Cobre components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp.
 *
 * Alem das unidades isoladas, inclui testes de pipeline que espelham o fluxo
 * do terminal TUI unificado (menu inicial + tela SSH): parse_command gera o
 * `args` que alimenta parse_ssh_target (preenchimento da conexao, CMD4-CMD7
 * do roteiro manual) e encode_ssh_key cobre o roteamento de teclas nas duas
 * superficies.
 *
 * Estruturado segundo o padrao AAA (Arrange, Act, Assert) e principios F.I.R.S.T.
 *
 * cyberdeck_help_text(): funcao pura que fornece o catalogo unico de ajuda do
 * shell. O teste fixa o catalogo completo, incluindo os comandos comuns e os
 * comandos locais, byte a byte (separador "-" entre comando e descricao,
 * newline final). Alem da igualdade exata, os testes fixam a estrutura
 * (65 newlines, 65 linhas nao vazias = 18 tags + 47 indentadas, newline
 * final), a presenca de cada comando e o determinismo entre chamadas.
 */
#include "apps/shell/cyberdeck_shell_utils.h"
#include "contracts/cyberdeck_help.h"
#include "lvgl.h"

#include <cstdio>
#include <string>

namespace {

int s_failures = 0;
int s_checks = 0;

inline std::string escape_string(const std::string &s)
{
    std::string res;
    for (unsigned char c : s) {
        if (c == '\\') res += "\\\\";
        else if (c == '\n') res += "\\n";
        else if (c == '\r') res += "\\r";
        else if (c == '\t') res += "\\t";
        else if (c == '\x1B') res += "\\x1B";
        else if (c < 32 || c >= 127) {
            char buf[10];
            std::snprintf(buf, sizeof(buf), "\\x%02X", static_cast<unsigned int>(c));
            res += buf;
        } else {
            res += static_cast<char>(c);
        }
    }
    return res;
}

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++s_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);          \
        }                                                                              \
    } while (0)

#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        ++s_checks;                                                                    \
        if ((actual) != (expected)) {                                                  \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  expected: '%s' actual: '%s'\n",                   \
                        __FILE__, __LINE__, escape_string(expected).c_str(),           \
                        escape_string(actual).c_str());                                \
        }                                                                              \
    } while (0)

void test_parse_ssh_target_valid()
{
    // Act & Assert 1: Full user@host:port
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("alice@192.168.1.50:2222", user, host, port) == true);
        CHECK_EQ(user, "alice");
        CHECK_EQ(host, "192.168.1.50");
        CHECK(port == 2222);
    }

    // Act & Assert 2: user@host (default port 22)
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("bob@my-server.local", user, host, port) == true);
        CHECK_EQ(user, "bob");
        CHECK_EQ(host, "my-server.local");
        CHECK(port == 22);
    }

    // Act & Assert 3: host:port (default user root)
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("10.0.0.1:2200", user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "10.0.0.1");
        CHECK(port == 2200);
    }

    // Act & Assert 4: bare host (default user root and port 22)
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("myserver", user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "myserver");
        CHECK(port == 22);
    }

    // Act & Assert 5: boundary valid ports (1 and 65535)
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("host:1", user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "host");
        CHECK(port == 1);

        user.clear(); host.clear(); port = 0;
        CHECK(cyberdeck_parse_ssh_target("host:65535", user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "host");
        CHECK(port == 65535);
    }

    // Act & Assert 6: bracketed IPv6 support ([ipv6]:port and [ipv6])
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("[2001:db8::1]:2222", user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "2001:db8::1");
        CHECK(port == 2222);

        user.clear(); host.clear(); port = 0;
        CHECK(cyberdeck_parse_ssh_target("admin@[2001:db8::1]", user, host, port) == true);
        CHECK_EQ(user, "admin");
        CHECK_EQ(host, "2001:db8::1");
        CHECK(port == 22);

        user.clear(); host.clear(); port = 0;
        CHECK(cyberdeck_parse_ssh_target("[::1]:22", user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "::1");
        CHECK(port == 22);

        user.clear(); host.clear(); port = 0;
        CHECK(cyberdeck_parse_ssh_target("admin@[2001:db8::1]:2222", user, host, port) == true);
        CHECK_EQ(user, "admin");
        CHECK_EQ(host, "2001:db8::1");
        CHECK(port == 2222);

        // IPv6 link-local com zone index numerico/hex (%)
        user.clear(); host.clear(); port = 0;
        CHECK(cyberdeck_parse_ssh_target("[fe80::1%1]:22", user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "fe80::1%1");
        CHECK(port == 22);

        user.clear(); host.clear(); port = 0;
        CHECK(cyberdeck_parse_ssh_target("alice@[fe80::1%2]:2222", user, host, port) == true);
        CHECK_EQ(user, "alice");
        CHECK_EQ(host, "fe80::1%2");
        CHECK(port == 2222);

        // Interface nao-hex (ex: eth0) e rejeitada pelo filtro estrito de caracteres
        CHECK(cyberdeck_parse_ssh_target("[fe80::1%eth0]:22", user, host, port) == false);
    }
}

void test_parse_ssh_target_defaults_and_reuse()
{
    // Porta com zeros a esquerda continua sendo composta apenas de digitos
    // (strtol normaliza para 22). Garante que o guard de digitos nao a rejeita.
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("host:00022", user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "host");
        CHECK(port == 22);
    }

    // user com ponto/underscore e host DNS com hifen/ponto sao aceitos
    // (o parser valida delimitadores e porta, nao o formato do nome).
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("deploy.bot@build-server.local:2222", user, host, port) == true);
        CHECK_EQ(user, "deploy.bot");
        CHECK_EQ(host, "build-server.local");
        CHECK(port == 2222);
    }

    // Limite superior com user explicito.
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("admin@host:65535", user, host, port) == true);
        CHECK_EQ(user, "admin");
        CHECK_EQ(host, "host");
        CHECK(port == 65535);
    }

    // Reuso: chamadas consecutivas na mesma variavel nao devem herdar valores da chamada anterior
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("alice@h1:2222", user, host, port) == true);
        CHECK_EQ(user, "alice");
        CHECK_EQ(host, "h1");
        CHECK(port == 2222);

        CHECK(cyberdeck_parse_ssh_target("h2", user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "h2");
        CHECK(port == 22);
    }
}

void test_parse_ssh_target_invalid_edge_cases()
{
    std::string user, host;
    int port = 0;

    // Null and empty
    CHECK(cyberdeck_parse_ssh_target(nullptr, user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("", user, host, port) == false);

    // Whitespace and control character errors
    CHECK(cyberdeck_parse_ssh_target("user@host:22 ", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target(" user@host", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user @host", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user@ host", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user@host: 22", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user@host\t:22", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user@host\r\n:22", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user\nhost:22", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user@host:22\n", user, host, port) == false);

    // Malformed delimiters
    CHECK(cyberdeck_parse_ssh_target("@host", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user@", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("@", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target(":22", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user@host:", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user@@host", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user@host@other", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user@:22", user, host, port) == false);

    // Invalid unbracketed multiple colons (malformed or unbracketed IPv6)
    CHECK(cyberdeck_parse_ssh_target("2001:db8::1", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("::1", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("host:22:33", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("user@host:22:33", user, host, port) == false);

    // Malformed bracketed IPv6
    CHECK(cyberdeck_parse_ssh_target("[2001:db8::1", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("[]", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("[:]", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("[]:22", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("[:22]", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("[2001:db8::1]:", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("[::1]:abc", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("[::1]:65536", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("[::1]extra", user, host, port) == false);

    // Invalid port numbers
    CHECK(cyberdeck_parse_ssh_target("host:0", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("host:65536", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("host:-1", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("host:+22", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("host:+", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("host:abc", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("host:22a", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("host:9999999999", user, host, port) == false);
    CHECK(cyberdeck_parse_ssh_target("host:000000000000000000065536", user, host, port) == false);
}

void test_parse_ssh_target_non_mutation_on_failure()
{
    // Garantia de nao-mutacao em caso de erro: se o parsing falhar,
    // as variaveis de saida (user, host, port) devem permanecer intactas.
    const char *invalid_inputs[] = {
        nullptr,
        "",
        "   ",
        "@host",
        "user@",
        "host:abc",
        "host:65536",
        "host:0",
        "user@host: 22",
        "2001:db8::1",
        "host:22:33",
        "[::1]:bad",
        "user@:22",
    };

    for (const char *inp : invalid_inputs) {
        std::string user = "CANARY_USER";
        std::string host = "CANARY_HOST";
        int port = -999;

        CHECK(cyberdeck_parse_ssh_target(inp, user, host, port) == false);
        CHECK_EQ(user, "CANARY_USER");
        CHECK_EQ(host, "CANARY_HOST");
        CHECK(port == -999);
    }
}

void test_parse_ssh_target_limits_normalization()
{
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("alice@server:00022", user, host, port) == true);
        CHECK_EQ(user, "alice");
        CHECK_EQ(host, "server");
        CHECK(port == 22);
    }

    {
        std::string user, host;
        int port = 0;
        // Overflow de strtol -> fora do range 1..65535 -> invalido, sem UB.
        CHECK(cyberdeck_parse_ssh_target("host:999999999999999999999999", user, host, port) == false);
    }

    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("host:000001", user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "host");
        CHECK(port == 1);
    }

    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("host:00065535", user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "host");
        CHECK(port == 65535);
    }

    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("host:00000", user, host, port) == false);
    }
}

void test_encode_ssh_key_control_keys()
{
    // Arrange & Act & Assert: Single control keys
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_ENTER, 0), "\n");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_BACKSPACE, 0), "\x7F");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_DEL, 0), "\x1B[3~");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_ESC, 0), "\x1B");
    CHECK_EQ(cyberdeck_encode_ssh_key('\t', 0), "\t");

    // Arrow keys
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_UP, 0), "\x1B[A");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_DOWN, 0), "\x1B[B");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_LEFT, 0), "\x1B[D");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_RIGHT, 0), "\x1B[C");

    // Navigation keys: Home and End
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_HOME, 0), "\x1B[H");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_END, 0), "\x1B[F");

    // Tab navigation keys (LV_KEY_NEXT / LV_KEY_PREV)
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_NEXT, 0), "\t");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_PREV, 0), "\x0B");
}

void test_encode_ssh_key_printable()
{
    // Printable characters without modifiers
    CHECK_EQ(cyberdeck_encode_ssh_key('a', 0), "a");
    CHECK_EQ(cyberdeck_encode_ssh_key('Z', 0), "Z");
    CHECK_EQ(cyberdeck_encode_ssh_key('0', 0), "0");
    CHECK_EQ(cyberdeck_encode_ssh_key('#', 0), "#");
    CHECK_EQ(cyberdeck_encode_ssh_key(' ', 0), " ");
}

void test_encode_ssh_key_modifiers()
{
    // Modifier Ctrl (0x01): Ctrl+A -> 0x01, Ctrl+C -> 0x03, Ctrl+Z -> 0x1A
    CHECK_EQ(cyberdeck_encode_ssh_key('a', 0x01), "\x01");
    CHECK_EQ(cyberdeck_encode_ssh_key('c', 0x01), "\x03");
    CHECK_EQ(cyberdeck_encode_ssh_key('z', 0x01), "\x1A");

    // Uppercase with Ctrl
    CHECK_EQ(cyberdeck_encode_ssh_key('A', 0x01), "\x01");
    CHECK_EQ(cyberdeck_encode_ssh_key('C', 0x01), "\x03");
    CHECK_EQ(cyberdeck_encode_ssh_key('Z', 0x01), "\x1A");

    // Extended Ctrl shortcuts (Vim / Shell)
    CHECK_EQ(cyberdeck_encode_ssh_key('[', 0x01), "\x1B"); // Ctrl+[ = ESC
    CHECK_EQ(cyberdeck_encode_ssh_key('\\', 0x01), "\x1C"); // Ctrl+\ = SIGQUIT
    CHECK_EQ(cyberdeck_encode_ssh_key(']', 0x01), "\x1D"); // Ctrl+] = GS
    CHECK_EQ(cyberdeck_encode_ssh_key('^', 0x01), "\x1E"); // Ctrl+^ = RS
    CHECK_EQ(cyberdeck_encode_ssh_key('_', 0x01), "\x1F"); // Ctrl+_ = US
    CHECK_EQ(cyberdeck_encode_ssh_key('@', 0x01), std::string("\x00", 1)); // Ctrl+@ = NUL
    CHECK_EQ(cyberdeck_encode_ssh_key(' ', 0x01), std::string("\x00", 1)); // Ctrl+Space = NUL

    // Modifier Alt/Meta (0x04): Prefix with 0x1B
    CHECK_EQ(cyberdeck_encode_ssh_key('x', 0x04), "\x1Bx");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_UP, 0x04), "\x1B\x1B[A");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_HOME, 0x04), "\x1B\x1B[H");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_END, 0x04), "\x1B\x1B[F");

    // Modifier Alt + Ctrl (0x05): Alt prefixa ESC ao byte de controle gerado por Ctrl
    CHECK_EQ(cyberdeck_encode_ssh_key('a', 0x05), "\x1B\x01");
    CHECK_EQ(cyberdeck_encode_ssh_key('Z', 0x05), "\x1B\x1A");
    CHECK_EQ(cyberdeck_encode_ssh_key('[', 0x05), "\x1B\x1B");
}

void test_encode_ssh_key_modifier_scope_and_bounds()
{
    // Ctrl (bit0) em teclas especiais que possuem codigo proprio nao altera a codificacao
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_ENTER, 0x01), "\n");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_BACKSPACE, 0x01), "\x7F");
    CHECK_EQ(cyberdeck_encode_ssh_key('\t', 0x01), "\t");
    CHECK_EQ(cyberdeck_encode_ssh_key(';', 0x01), ";");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_UP, 0x01), "\x1B[A");

    // Alt (bit2) prefixa ESC tambem em teclas especiais e digitos.
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_BACKSPACE, 0x04), "\x1B\x7F");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_DEL, 0x04), "\x1B\x1B[3~");
    CHECK_EQ(cyberdeck_encode_ssh_key('1', 0x04), "\x1B" "1");

    // Bit nao reconhecido isolado (ex. marcador de Shift 0x02, bits altos 0x10, 0x80) nao altera
    // a codificacao quando Ctrl/Alt nao estao setados.
    CHECK_EQ(cyberdeck_encode_ssh_key('a', 0x02), "a");
    CHECK_EQ(cyberdeck_encode_ssh_key('a', 0x10), "a");
    CHECK_EQ(cyberdeck_encode_ssh_key('a', 0x80), "a");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_UP, 0x02), "\x1B[A");
    CHECK_EQ(cyberdeck_encode_ssh_key(LV_KEY_UP, 0x80), "\x1B[A");

    // Modificador com todos os bits setados (0xFF): bits 0 (Ctrl) e 2 (Alt) ativos
    CHECK_EQ(cyberdeck_encode_ssh_key('a', 0xFF), "\x1B\x01");

    // Codigos de tecla fora do range de 1 byte e nao mapeados sao descartados (retorna string vazia).
    // Mesmo com Alt setado, nenhum ESC orfao e emitido.
    CHECK(cyberdeck_encode_ssh_key(0x100U, 0).empty());
    CHECK(cyberdeck_encode_ssh_key(0xFFFFU, 0).empty());
    CHECK(cyberdeck_encode_ssh_key(0x100U, 0x04).empty());
    CHECK(cyberdeck_encode_ssh_key(0xFFFFU, 0x05).empty());
}

void test_parse_command_routing()
{
    cyberdeck_cmd_t cmd = cyberdeck_parse_command("help");
    CHECK(cmd.type == CYBERDECK_CMD_HELP);

    cmd = cyberdeck_parse_command("clear");
    CHECK(cmd.type == CYBERDECK_CMD_CLEAR);

    cmd = cyberdeck_parse_command("app");
    CHECK(cmd.type == CYBERDECK_CMD_APP);
    CHECK(cmd.args.empty());

    cmd = cyberdeck_parse_command("app list");
    CHECK(cmd.type == CYBERDECK_CMD_APP);
    CHECK_EQ(cmd.args, "list");

    cmd = cyberdeck_parse_command("appx");
    CHECK(cmd.type == CYBERDECK_CMD_UNKNOWN);

    // Empty and unknown commands
    cmd = cyberdeck_parse_command("");
    CHECK(cmd.type == CYBERDECK_CMD_EMPTY);

    cmd = cyberdeck_parse_command("   \t  ");
    CHECK(cmd.type == CYBERDECK_CMD_EMPTY);

    cmd = cyberdeck_parse_command(nullptr);
    CHECK(cmd.type == CYBERDECK_CMD_EMPTY);

    cmd = cyberdeck_parse_command("unknown_cmd");
    CHECK(cmd.type == CYBERDECK_CMD_UNKNOWN);
    CHECK_EQ(cmd.args, "unknown_cmd");

    // The migrated verbs belong to their manifests and never reach this parser.
    for (const char *migrated : {"wifi", "log", "screen on", "battery protection status",
                                 "bluetooth search", "ssh"}) {
        CHECK(cyberdeck_parse_command(migrated).type == CYBERDECK_CMD_UNKNOWN);
    }
}

void test_parse_command_whitespace_and_exact_match()
{
    // Espacos/tabs nas bordas (inclusive \r\n de linhas de terminal) sao ignorados.
    CHECK(cyberdeck_parse_command("help ").type == CYBERDECK_CMD_HELP);
    CHECK(cyberdeck_parse_command("\thelp").type == CYBERDECK_CMD_HELP);
    CHECK(cyberdeck_parse_command("  help\t").type == CYBERDECK_CMD_HELP);
    CHECK(cyberdeck_parse_command("clear\r\n").type == CYBERDECK_CMD_CLEAR);
    CHECK(cyberdeck_parse_command("app\r\n").type == CYBERDECK_CMD_APP);

    // Match exato: case e sufixos tornam o comando desconhecido.
    CHECK(cyberdeck_parse_command("HELP").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("Help").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("help me").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("clear all").type == CYBERDECK_CMD_UNKNOWN);

    // Leading \r ou \n nao sao espaco/tab de prefixo -> viram UNKNOWN
    CHECK(cyberdeck_parse_command("\nhelp").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("\rhelp").type == CYBERDECK_CMD_UNKNOWN);

    // Args de comando desconhecido preservam a cauda apos trim de bordas.
    cyberdeck_cmd_t cmd = cyberdeck_parse_command("  foo bar  ");
    CHECK(cmd.type == CYBERDECK_CMD_UNKNOWN);
    CHECK_EQ(cmd.args, "foo bar");
}

void test_parse_command_to_ssh_target_pipeline()
{
    // `ssh` is declared by its manifest, so the legacy parser reports UNKNOWN and
    // the argument split belongs to the console application.  What must survive
    // here is the target validator shared by both call sites.
    for (const char *line : {"ssh", "ssh user@host:22", "  ssh\tmyserver.local:2200  "}) {
        cyberdeck_cmd_t cmd = cyberdeck_parse_command(line);
        CHECK(cmd.type == CYBERDECK_CMD_UNKNOWN);
    }

    // CMD5: porta invalida -> usage.
    {
        std::string user = "CANARY_U", host = "CANARY_H";
        int port = -999;
        CHECK(cyberdeck_parse_ssh_target("host:abc", user, host, port) == false);
        CHECK_EQ(user, "CANARY_U");
        CHECK_EQ(host, "CANARY_H");
        CHECK(port == -999);
    }

    // CMD6: espaco dentro do alvo -> usage.
    {
        std::string user = "CANARY_U", host = "CANARY_H";
        int port = -999;
        CHECK(cyberdeck_parse_ssh_target("user @host", user, host, port) == false);
        CHECK_EQ(user, "CANARY_U");
        CHECK_EQ(host, "CANARY_H");
        CHECK(port == -999);
    }

    // CMD7: alvo valido preenche user/host/port e habilita CONNECT.
    {
        std::string user, host;
        int port = 0;
        CHECK(cyberdeck_parse_ssh_target("alice@192.168.1.50:2222", user, host, port) == true);
        CHECK_EQ(user, "alice");
        CHECK_EQ(host, "192.168.1.50");
        CHECK(port == 2222);
    }
}

void test_help_text_exact_block()
{
    // Arrange
    // Catalogo unico aprovado no plano, terminado com newline final.
    // cyberdeck_help_text() deve devolver esta string byte a byte.
    const std::string k_expected_help(cyberdeck_help_test::kUnifiedHelpText);

    // Act
    const std::string text = cyberdeck_help_text();

    // Assert
    CHECK_EQ(text, k_expected_help);
}

void test_help_text_structure()
{
    // Arrange & Act
    // Estrutura do catalogo unificado: 12 blocos `<command> [terminal]` (as
    // seis linhas migradas para manifests saem do catalogo), cada um com a linha
    // de tag, a linha `  <usage> - <description>` e as linhas de parametros
    // opcionais indentadas; exatamente 38 linhas nao vazias (12 tags + 26
    // indentadas), e o texto termina obrigatoriamente em newline.
    const std::string text = cyberdeck_help_text();

    // Assert
    CHECK(!text.empty());
    CHECK(text.back() == '\n');

    size_t newlines = 0;
    for (char c : text) {
        if (c == '\n') {
            ++newlines;
        }
    }
    CHECK(newlines == 38);

    // Split por '\n': linhas nao vazias entre quebras. No novo formatter toda
    // linha e ou a tag `<command> [terminal]` do bloco ou uma linha indentada
    // com dois espacos; nenhuma linha vazia pode ficar exposta ao terminal.
    size_t non_empty_lines = 0;
    size_t tag_lines = 0;
    size_t indented_lines = 0;
    size_t line_start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            const std::string line = text.substr(line_start, i - line_start);
            if (!line.empty()) {
                ++non_empty_lines;
                if (line.size() > 11 &&
                    line.compare(line.size() - 11, 11, " [terminal]") == 0) {
                    ++tag_lines;
                } else if (line.compare(0, 2, "  ") == 0) {
                    ++indented_lines;
                }
            }
            line_start = i + 1;
        }
    }
    CHECK(non_empty_lines == 38);
    CHECK(tag_lines == 12);
    CHECK(indented_lines == 26);
}

void test_help_text_commands_present()
{
    // Arrange & Act
    // Cada comando comum e local deve aparecer no catalogo unico, incluindo
    // os formatos completos de uso.
    const std::string text = cyberdeck_help_text();

    // Assert
    struct help_row {
        const char *command;
        const char *usage_line;
    };
    const help_row entries[] = {
        {"help", "  help - show this help"},
        {"clear", "  clear - clear the terminal"},
        {"app", "  app [list|info|start|stop] [id] - manage compiled-in applications"},
        {"pwd", "  pwd - print working directory"},
        {"cd", "  cd [path] - change working directory"},
        {"ls", "  ls [-a] [path] - list directory contents"},
        {"cat", "  cat <file> - print a regular file"},
        {"touch", "  touch <file> - create an empty file"},
        {"mkdir", "  mkdir <directory> - create a directory"},
        {"rm", "  rm [-r] <path> - remove a file or directory"},
        {"rmdir", "  rmdir <directory> - remove an empty directory"},
        {"poweroff", "  poweroff - power off this device after exact y confirmation"},
    };

    // Completude: a lista acima deve cobrir todas as linhas do catalogo. Sem
    // esta contagem, uma linha nova no catalogo passaria despercebida porque
    // o teste so procuraria as entradas conhecidas.
    const size_t expected_rows = sizeof(entries) / sizeof(entries[0]);
    CHECK(expected_rows == 12);

    // Ordem do catalogo aprovada: as tags `<command> [terminal]` aparecem
    // exatamente nessa sequencia, cada uma abrindo sua propria linha.
    std::size_t tag_scan = 0;
    for (const help_row &entry : entries) {
        const std::string tag = std::string(entry.command) + " [terminal]\n";
        const std::size_t tag_position = text.find(tag, tag_scan);
        CHECK(tag_position != std::string::npos);
        if (tag_position != std::string::npos) {
            CHECK(tag_position == 0 || text[tag_position - 1] == '\n');
            tag_scan = tag_position + 1;
        }
    }

    // Usage/description vem logo apos a tag do proprio comando, indentada,
    // aparecendo exatamente uma vez (nenhuma entrada satisfaz outra por
    // substring) e fechando a propria linha.
    size_t rows_seen = 0;
    for (const help_row &entry : entries) {
        const std::string tag = std::string(entry.command) + " [terminal]\n";
        const std::string usage(entry.usage_line);
        const std::size_t tag_position = text.find(tag);
        if (tag_position == std::string::npos) {
            CHECK(false);
            continue;
        }
        const std::size_t position = tag_position + tag.size();
        CHECK(text.compare(position, usage.size(), usage) == 0);
        CHECK(text[position + usage.size()] == '\n');
        CHECK(text.find(usage, position + 1) == std::string::npos);
        ++rows_seen;
    }
    CHECK(rows_seen == expected_rows);
}

void test_help_text_stable()
{
    // Arrange & Act
    // Determinismo: duas chamadas consecutivas da funcao pura devolvem
    // exatamente o mesmo texto (nenhuma dependencia de estado global).
    const std::string first = cyberdeck_help_text();
    const std::string second = cyberdeck_help_text();

    // Assert
    CHECK_EQ(first, second);
}

} // namespace

int main()
{
    test_parse_ssh_target_valid();
    test_parse_ssh_target_defaults_and_reuse();
    test_parse_ssh_target_invalid_edge_cases();
    test_parse_ssh_target_non_mutation_on_failure();
    test_parse_ssh_target_limits_normalization();
    test_encode_ssh_key_control_keys();
    test_encode_ssh_key_printable();
    test_encode_ssh_key_modifiers();
    test_encode_ssh_key_modifier_scope_and_bounds();
    test_parse_command_routing();
    test_parse_command_whitespace_and_exact_match();
    test_parse_command_to_ssh_target_pipeline();
    test_help_text_exact_block();
    test_help_text_structure();
    test_help_text_commands_present();
    test_help_text_stable();

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_shell_utils (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
