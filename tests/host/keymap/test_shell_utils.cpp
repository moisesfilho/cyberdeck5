/*
 * Testes unitarios host-side para a logica pura de shell e traducao SSH/VT100.
 * Cobre components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp.
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
 * (16 newlines, 16 linhas nao vazias, newline final), a presenca de cada
 * comando e o determinismo entre chamadas.
 */
#include "features/shell/cyberdeck_shell_utils.h"
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
    // Standard commands
    cyberdeck_cmd_t cmd = cyberdeck_parse_command("help");
    CHECK(cmd.type == CYBERDECK_CMD_HELP);

    cmd = cyberdeck_parse_command("clear");
    CHECK(cmd.type == CYBERDECK_CMD_CLEAR);

    cmd = cyberdeck_parse_command("wifi");
    CHECK(cmd.type == CYBERDECK_CMD_WIFI);

    cmd = cyberdeck_parse_command("wifi search");
    CHECK(cmd.type == CYBERDECK_CMD_WIFI_SEARCH);

    cmd = cyberdeck_parse_command("wifi saved");
    CHECK(cmd.type == CYBERDECK_CMD_WIFI_SAVED);

    cmd = cyberdeck_parse_command("log");
    CHECK(cmd.type == CYBERDECK_CMD_LOG);

    // SSH command variants
    cmd = cyberdeck_parse_command("ssh");
    CHECK(cmd.type == CYBERDECK_CMD_SSH);
    CHECK(cmd.args.empty());

    cmd = cyberdeck_parse_command("ssh user@host:22");
    CHECK(cmd.type == CYBERDECK_CMD_SSH);
    CHECK_EQ(cmd.args, "user@host:22");

    cmd = cyberdeck_parse_command("   ssh   10.0.0.1   ");
    CHECK(cmd.type == CYBERDECK_CMD_SSH);
    CHECK_EQ(cmd.args, "10.0.0.1");

    cmd = cyberdeck_parse_command("ssh\tsomehost");
    CHECK(cmd.type == CYBERDECK_CMD_SSH);
    CHECK_EQ(cmd.args, "somehost");

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

    cmd = cyberdeck_parse_command("sshd");
    CHECK(cmd.type == CYBERDECK_CMD_UNKNOWN);
}

void test_parse_command_whitespace_and_exact_match()
{
    // Espacos/tabs nas bordas (inclusive \r\n de linhas de terminal) sao ignorados.
    CHECK(cyberdeck_parse_command("help ").type == CYBERDECK_CMD_HELP);
    CHECK(cyberdeck_parse_command("\thelp").type == CYBERDECK_CMD_HELP);
    CHECK(cyberdeck_parse_command("  help\t").type == CYBERDECK_CMD_HELP);
    CHECK(cyberdeck_parse_command("clear\r\n").type == CYBERDECK_CMD_CLEAR);
    CHECK(cyberdeck_parse_command("wifi\t ").type == CYBERDECK_CMD_WIFI);
    CHECK(cyberdeck_parse_command("log\r\n").type == CYBERDECK_CMD_LOG);

    // Match exato: case e sufixos tornam o comando desconhecido.
    CHECK(cyberdeck_parse_command("HELP").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("Help").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("help me").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("clear all").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("wifi on").type == CYBERDECK_CMD_UNKNOWN);

    // Leading \r ou \n nao sao espaco/tab de prefixo -> viram UNKNOWN
    CHECK(cyberdeck_parse_command("\nhelp").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("\rhelp").type == CYBERDECK_CMD_UNKNOWN);

    // "ssh" aceita apenas espaco/tab como separador do alvo; "ssh " sem
    // argumentos e SSH com args vazio (nao UNKNOWN).
    cyberdeck_cmd_t cmd = cyberdeck_parse_command("ssh ");
    CHECK(cmd.type == CYBERDECK_CMD_SSH);
    CHECK(cmd.args.empty());

    cmd = cyberdeck_parse_command("  ssh\t\t");
    CHECK(cmd.type == CYBERDECK_CMD_SSH);
    CHECK(cmd.args.empty());

    cmd = cyberdeck_parse_command("ssh\tuser@host:22");
    CHECK(cmd.type == CYBERDECK_CMD_SSH);
    CHECK_EQ(cmd.args, "user@host:22");

    // "ssh" colado a outro caractere nao e o verbo ssh (contrato do separador).
    cmd = cyberdeck_parse_command("ssh:host");
    CHECK(cmd.type == CYBERDECK_CMD_UNKNOWN);
    CHECK_EQ(cmd.args, "ssh:host");

    cmd = cyberdeck_parse_command("ssh-host");
    CHECK(cmd.type == CYBERDECK_CMD_UNKNOWN);

    // Args de comando desconhecido preservam a cauda apos trim de bordas.
    cmd = cyberdeck_parse_command("  foo bar  ");
    CHECK(cmd.type == CYBERDECK_CMD_UNKNOWN);
    CHECK_EQ(cmd.args, "foo bar");
}

void test_parse_wifi_audit_save_command()
{
    // wifi audit is display-only; persistence is opted into with the separate
    // save verb and no retired export confirmation syntax.
    cyberdeck_cmd_t standard = cyberdeck_parse_command("wifi audit");
    CHECK(standard.type == CYBERDECK_CMD_WIFI_AUDIT);
    CHECK(!standard.confirmed);

    cyberdeck_cmd_t save = cyberdeck_parse_command("wifi audit save");
    CHECK(save.type != CYBERDECK_CMD_EMPTY);
    CHECK(save.type != CYBERDECK_CMD_UNKNOWN);
    CHECK(save.type != CYBERDECK_CMD_WIFI_AUDIT);
    CHECK(!save.confirmed);

    CHECK(cyberdeck_parse_command("wifi audit export").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("wifi audit export confirm").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("wifi audit save confirm").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("wifi audit save /sdcard/wifi-audit.txt").type ==
          CYBERDECK_CMD_UNKNOWN);
}

void test_parse_screen_commands()
{
    cyberdeck_cmd_t on = cyberdeck_parse_command("screen on");
    CHECK(on.type == CYBERDECK_CMD_SCREEN_ON);
    CHECK(on.args.empty());

    cyberdeck_cmd_t off = cyberdeck_parse_command(" screen\toff\r\n");
    CHECK(off.type == CYBERDECK_CMD_SCREEN_OFF);
    CHECK(off.args.empty());

    cyberdeck_cmd_t timeout = cyberdeck_parse_command("screen timeout 0");
    CHECK(timeout.type == CYBERDECK_CMD_SCREEN_TIMEOUT);
    CHECK_EQ(timeout.args, "0");

    timeout = cyberdeck_parse_command("  screen\t timeout \t 1440  ");
    CHECK(timeout.type == CYBERDECK_CMD_SCREEN_TIMEOUT);
    CHECK_EQ(timeout.args, "1440");

    // Missing/invalid operands remain routed to the timeout command so the UI
    // can return one deterministic usage/range error via the pure parser.
    timeout = cyberdeck_parse_command("screen timeout");
    CHECK(timeout.type == CYBERDECK_CMD_SCREEN_TIMEOUT);
    CHECK(timeout.args.empty());
    timeout = cyberdeck_parse_command("screen timeout -1");
    CHECK(timeout.type == CYBERDECK_CMD_SCREEN_TIMEOUT);
    CHECK_EQ(timeout.args, "-1");
    timeout = cyberdeck_parse_command("screen timeout 1441");
    CHECK(timeout.type == CYBERDECK_CMD_SCREEN_TIMEOUT);
    CHECK_EQ(timeout.args, "1441");
    timeout = cyberdeck_parse_command("screen timeout abc");
    CHECK(timeout.type == CYBERDECK_CMD_SCREEN_TIMEOUT);
    CHECK_EQ(timeout.args, "abc");

    CHECK(cyberdeck_parse_command("screen").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("screen on now").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("screen off now").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("screen timeout 1 2").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("Screen on").type == CYBERDECK_CMD_UNKNOWN);
}

void test_parse_battery_protection_commands()
{
    const cyberdeck_cmd_t on = cyberdeck_parse_command("battery protection on");
    CHECK(on.type == CYBERDECK_CMD_BATTERY_PROTECTION_ON);
    CHECK(on.args.empty());

    const cyberdeck_cmd_t off = cyberdeck_parse_command("  battery\t protection \t off\r\n");
    CHECK(off.type == CYBERDECK_CMD_BATTERY_PROTECTION_OFF);
    CHECK(off.args.empty());

    const cyberdeck_cmd_t status = cyberdeck_parse_command("battery protection status");
    CHECK(status.type == CYBERDECK_CMD_BATTERY_PROTECTION_STATUS);
    CHECK(status.args.empty());

    CHECK(cyberdeck_parse_command("battery").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("battery protection").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("battery protection maybe").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("battery protection on now").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("Battery protection on").type == CYBERDECK_CMD_UNKNOWN);
}

void test_parse_command_ssh_verb_contract()
{
    // "ssh" como prefixo exige separador; o verbo sozinho e SSH sem args.
    CHECK(cyberdeck_parse_command("ssh").type == CYBERDECK_CMD_SSH);
    CHECK(cyberdeck_parse_command("ssh ").type == CYBERDECK_CMD_SSH);
    CHECK(cyberdeck_parse_command("ssh\t").type == CYBERDECK_CMD_SSH);
    CHECK(cyberdeck_parse_command("ssh\r\n").type == CYBERDECK_CMD_SSH);
    CHECK(cyberdeck_parse_command("ssh  \t ").type == CYBERDECK_CMD_SSH);
    CHECK(cyberdeck_parse_command("ssh\n").type == CYBERDECK_CMD_SSH);

    // "ssh" com apenas whitespace nao tem args (leva ao usage no fluxo CMD4).
    CHECK(cyberdeck_parse_command("ssh ").args.empty());
    CHECK(cyberdeck_parse_command("ssh\t").args.empty());
    CHECK(cyberdeck_parse_command("ssh  \t ").args.empty());
    CHECK(cyberdeck_parse_command("ssh\r\n").args.empty());

    // "ssh" colado a pontuacao/nome de comando continua UNKNOWN.
    CHECK(cyberdeck_parse_command("sshd").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("ssh+extra").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("ssh.").type == CYBERDECK_CMD_UNKNOWN);
    CHECK(cyberdeck_parse_command("ssh/help").type == CYBERDECK_CMD_UNKNOWN);
}

void test_parse_command_to_ssh_target_pipeline()
{
    // Espelha o fluxo do terminal TUI unificado: shell_command_clicked()
    // encaminha cmd.args ao parse_ssh_target (CMD4-CMD7 do roteiro manual).

    // CMD4: ssh sem args -> usage (parse_ssh_target rejeita string vazia).
    {
        std::string user = "CANARY_U", host = "CANARY_H";
        int port = -999;
        cyberdeck_cmd_t cmd = cyberdeck_parse_command("ssh ");
        CHECK(cmd.type == CYBERDECK_CMD_SSH);
        CHECK(cmd.args.empty());
        CHECK(cyberdeck_parse_ssh_target(cmd.args.c_str(), user, host, port) == false);
        CHECK_EQ(user, "CANARY_U");
        CHECK_EQ(host, "CANARY_H");
        CHECK(port == -999);
    }

    // CMD5: porta invalida -> usage.
    {
        std::string user = "CANARY_U", host = "CANARY_H";
        int port = -999;
        cyberdeck_cmd_t cmd = cyberdeck_parse_command("ssh host:abc");
        CHECK(cmd.type == CYBERDECK_CMD_SSH);
        CHECK_EQ(cmd.args, "host:abc");
        CHECK(cyberdeck_parse_ssh_target(cmd.args.c_str(), user, host, port) == false);
        CHECK_EQ(user, "CANARY_U");
        CHECK_EQ(host, "CANARY_H");
        CHECK(port == -999);
    }

    // CMD6: espaco dentro do alvo -> usage.
    {
        std::string user = "CANARY_U", host = "CANARY_H";
        int port = -999;
        cyberdeck_cmd_t cmd = cyberdeck_parse_command("ssh user @host");
        CHECK(cmd.type == CYBERDECK_CMD_SSH);
        CHECK_EQ(cmd.args, "user @host");
        CHECK(cyberdeck_parse_ssh_target(cmd.args.c_str(), user, host, port) == false);
        CHECK_EQ(user, "CANARY_U");
        CHECK_EQ(host, "CANARY_H");
        CHECK(port == -999);
    }

    // CMD7: alvo valido preenche user/host/port e habilita CONNECT.
    {
        std::string user, host;
        int port = 0;
        cyberdeck_cmd_t cmd = cyberdeck_parse_command("ssh alice@192.168.1.50:2222");
        CHECK(cmd.type == CYBERDECK_CMD_SSH);
        CHECK_EQ(cmd.args, "alice@192.168.1.50:2222");
        CHECK(cyberdeck_parse_ssh_target(cmd.args.c_str(), user, host, port) == true);
        CHECK_EQ(user, "alice");
        CHECK_EQ(host, "192.168.1.50");
        CHECK(port == 2222);
    }

    // Pipeline com tab como separador do verbo e whitespace nas bordas.
    {
        std::string user, host;
        int port = 0;
        cyberdeck_cmd_t cmd = cyberdeck_parse_command("  ssh\tmyserver.local:2200  ");
        CHECK(cmd.type == CYBERDECK_CMD_SSH);
        CHECK_EQ(cmd.args, "myserver.local:2200");
        CHECK(cyberdeck_parse_ssh_target(cmd.args.c_str(), user, host, port) == true);
        CHECK_EQ(user, "root");
        CHECK_EQ(host, "myserver.local");
        CHECK(port == 2200);
    }

    // Pipeline com enum/prefixo: "sshx" nao e verbo ssh; args intactos.
    {
        cyberdeck_cmd_t cmd = cyberdeck_parse_command("sshx host");
        CHECK(cmd.type == CYBERDECK_CMD_UNKNOWN);
        CHECK_EQ(cmd.args, "sshx host");
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
    // Estrutura do catalogo unificado: exatamente 16 newlines (um por linha) e
    // 16 linhas nao vazias; o texto termina obrigatoriamente em newline.
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
    CHECK(newlines == 16);

    // Split por '\n': linhas nao vazias entre quebras. Uma linha vazia
    // (start == i, sem caracteres) nao conta; a cauda apos o ultimo '\n'
    // (deve ser vazia por causa do newline final) tambem nao conta.
    size_t non_empty_lines = 0;
    size_t line_start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            if (i > line_start) {
                ++non_empty_lines;
            }
            line_start = i + 1;
        }
    }
    CHECK(non_empty_lines == 16);
}

void test_help_text_commands_present()
{
    // Arrange & Act
    // Cada comando comum e local deve aparecer no catalogo unico, incluindo
    // os formatos completos de uso.
    const std::string text = cyberdeck_help_text();

    // Assert
    const char *entries[] = {
        "help - show this help",
        "pwd - print working directory",
        "cd [path] - change working directory",
        "ls [-a] [path] - list directory contents",
        "cat <file> - print a regular file",
        "touch <file> - create an empty file",
        "mkdir <directory> - create a directory",
        "rm [-r] <path> - remove a file or directory",
        "rmdir <directory> - remove an empty directory",
        "wifi [search|saved|audit] - show status, manage Wi-Fi, or audit",
        "log - show recent events",
        "clear - clear the terminal",
        "screen [on|off|timeout <0-1440>] - control screen protection",
        "battery [protection on|off|status] - show or control battery protection",
        "bluetooth [search|paired] - search for or list paired Bluetooth devices",
        "ssh [user@]host[:port] - start an SSH session",
    };
    for (const char *entry : entries) {
        CHECK(text.find(entry) != std::string::npos);
    }

    // Completude: a lista acima deve cobrir todas as linhas do catalogo. Sem
    // esta contagem, uma linha nova no catalogo passaria despercebida porque
    // o teste so procuraria as entradas conhecidas.
    const size_t expected_rows = sizeof(entries) / sizeof(entries[0]);
    CHECK(expected_rows == 16);
    size_t rows_seen = 0;
    for (const char *entry : entries) {
        size_t position = 0;
        while ((position = text.find(entry, position)) != std::string::npos) {
            // A linha precisa comecar no inicio de uma linha do catalogo, para
            // que uma entrada nao possa satisfazer outra por substring.
            const bool at_line_start = position == 0 || text[position - 1] == '\n';
            const bool at_line_end = (position + std::string(entry).size()) == text.size() ||
                                     text[position + std::string(entry).size()] == '\n';
            CHECK(at_line_start);
            CHECK(at_line_end);
            ++rows_seen;
            position += std::string(entry).size();
        }
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
    test_parse_wifi_audit_save_command();
    test_parse_screen_commands();
    test_parse_battery_protection_commands();
    test_parse_command_ssh_verb_contract();
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
