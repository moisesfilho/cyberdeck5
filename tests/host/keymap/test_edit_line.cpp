/*
 * Testes de REGRESSAO host-side para o modelo PURO da linha de edicao +
 * sessao SSH (contrato em cyberdeck_edit_line.h, implementacao futura em
 * components/cyberdeck/src/features/shell/cyberdeck_edit_line.cpp).
 *
 * Bug aprovado A sob teste (duplicacao de caracteres na entrada e no Enter):
 *   - caracteres aparecem EXATAMENTE uma vez na linha (fisico e virtual);
 *   - entrada fisica/virtual compartilham o mesmo nucleo (paridade);
 *   - backspace/setas/editam UTF-8 na posicao REAL do cursor;
 *   - Enter envia comando+newline UMA vez e limpa a linha sem duplicacao;
 *   - limite hard de 12288 bytes sem insercao parcial.
 *
 * Politica de ECO aprovada (regressao E) sob teste: cyberdeck_enter_result::echo
 * fixa os bytes EXATOS a anexar ao terminal local; "" = SEM eco local
 * (somente o remoto ecoa):
 *   - CONNECTED (comando nao vazio) -> payload exatamente "cmd\n", SEM eco;
 *   - CONNECTED (linha vazia)       -> somente "\n", sem eco;
 *   - line_already_sent             -> somente "\n", sem duplicacao, sem eco;
 *   - MENU                          -> eco local "$ cmd\n" PRESERVADO;
 *   - PASSWORD                      -> sem eco (a senha nunca e ecoada);
 *   - HOST_KEY                      -> fluxo TOFU preservado, sem eco.
 *
 * TDD (mesmo padrao de test_terminal_filter/test_wifi_indicator): enquanto
 * cyberdeck_edit_line.cpp nao existir, `make test_edit_line` falha por modulo
 * ausente ("No rule to make target ...cyberdeck_edit_line.cpp"). Nenhuma
 * implementacao fake e usada para fazer os testes passar.
 *
 * Estruturado segundo AAA (Arrange, Act, Assert) e principios F.I.R.S.T.
 *
 * Build: make test_edit_line -> ver Makefile (so g++/make).
 */
#include "features/shell/cyberdeck_edit_line.h"

#include <cstdio>
#include <cstring>
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

void expect_line(cyberdeck_edit_line &ed, const std::string &exp, size_t exp_cursor,
                 size_t exp_utf8_len)
{
    ++s_checks;
    if (ed.line() != exp) {
        ++s_failures;
        std::printf("FAIL %s:%d  line: esperada '%s' atual '%s'\n", __FILE__, __LINE__,
                    escape_string(exp).c_str(), escape_string(ed.line()).c_str());
        return;
    }
    ++s_checks;
    if (ed.cursor() != exp_cursor) {
        ++s_failures;
        std::printf("FAIL %s:%d  cursor: esperado %zu atual %zu\n", __FILE__, __LINE__,
                    exp_cursor, ed.cursor());
    }
    ++s_checks;
    if (ed.utf8_length() != exp_utf8_len) {
        ++s_failures;
        std::printf("FAIL %s:%d  utf8_length: esperado %zu atual %zu\n", __FILE__, __LINE__,
                    exp_utf8_len, ed.utf8_length());
    }
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

/* Limite HARD especificado pelo TUI (paridade com TERMINAL_LIMIT = 12288 em
 * cyberdeck_ui.cpp). O literal e o contrato: se o valor em producao mudar,
 * este static_assert quebra o build e forcara revisao consciente. */
constexpr size_t kSpecLimit = 12288;
static_assert(cyberdeck_edit_line::limit == kSpecLimit,
              "TERMINAL_LIMIT do TUI mudou: reveja o contrato e atualize os testes");

/* ------------------------------------------------------------------ testes */

/* REGRESSAO (A1): caracteres aparecem exatamente uma vez na linha. */
void test_insert_each_char_once()
{
    cyberdeck_edit_line ed; /* MENU */

    CHECK(ed.insert("a", 1) == true);
    expect_line(ed, "a", 1, 1);

    /* Chunk com varios bytes: todos inseridos uma unica vez. */
    CHECK(ed.insert("bc", 2) == true);
    expect_line(ed, "abc", 3, 3);

    /* Chunks futuros concatenam sem duplicar. */
    CHECK(ed.insert("defgh", 5) == true);
    expect_line(ed, "abcdefgh", 8, 8);

    /* Um caractere = uma insercao, incluso com chunk vazio/null. */
    CHECK(ed.insert("", 0) == true);
    CHECK(ed.insert(nullptr, 0) == true); /* no-op logico, nao erro */
    expect_line(ed, "abcdefgh", 8, 8);

    /* text invalido com len > 0: rejeitado SEM alterar o estado. */
    CHECK(ed.insert(nullptr, 3) == false);
    expect_line(ed, "abcdefgh", 8, 8);
}

/* REGRESSAO (A2): entrada fisica e virtual compartilham o mesmo nucleo. */
void test_physical_virtual_parity()
{
    cyberdeck_edit_line phys;
    cyberdeck_edit_line virt;

    const struct {
        bool physical;
        const char *text;
        size_t len;
    } seq[] = {
        {true, "hel", 3}, {false, "lo", 2}, {true, " ", 1},
        {false, "wor", 3}, {true, "ld", 2},
    };
    for (const auto &s : seq) {
        const bool ok = s.physical ? phys.insert_physical(s.text, s.len)
                                   : phys.insert_virtual(s.text, s.len);
        const bool ok2 = s.physical ? virt.insert_physical(s.text, s.len)
                                    : virt.insert_virtual(s.text, s.len);
        CHECK(ok == true);
        CHECK(ok2 == true);
    }

    /* Paridade total: mesma linha, mesmo cursor, mesmo UTF-8. */
    CHECK(phys.line() == virt.line());
    CHECK(phys.cursor() == virt.cursor());
    CHECK(phys.utf8_length() == virt.utf8_length());
    expect_line(phys, "hello world", 11, 11);

    /* Um segundo cenario: edicao mista (backspace/arrows) em ambas. */
    cyberdeck_edit_line a;
    cyberdeck_edit_line b;
    CHECK(a.insert_physical("abcd", 4) == true);
    CHECK(b.insert_virtual("abcd", 4) == true);
    a.cursor_left(); a.cursor_left();
    b.cursor_left(); b.cursor_left();
    CHECK(a.insert_virtual("X", 1) == true);
    CHECK(b.insert_physical("X", 1) == true);
    CHECK(a.line() == b.line());
    CHECK(a.cursor() == b.cursor());
    expect_line(a, "abXcd", 3, 5);
}

/* REGRESSAO (A): insercoes no meio deslocam o texto a direita (S12). */
void test_insert_in_middle_shifts_and_advances_cursor()
{
    cyberdeck_edit_line ed;
    CHECK(ed.insert("ac", 2) == true);   /* cursor no fim (2) */
    ed.cursor_left();
    CHECK(ed.cursor() == 1);
    CHECK(ed.insert("b", 1) == true);    /* insere na posicao real */
    expect_line(ed, "abc", 2, 3);

    /* A direita desloca: remover o 'b' restaura "ac". */
    ed.backspace();
    expect_line(ed, "ac", 1, 2);
}

/* REGRESSAO (A3): backspace remove o codepoint ANTERIOR ao cursor (S5/S13). */
void test_backspace_utf8_and_middle()
{
    cyberdeck_edit_line ed;
    CHECK(ed.insert("caf\xC3\xA9", 5) == true); /* UTF-8: c,a,f, é(2 bytes) */
    expect_line(ed, "caf\xC3\xA9", 5, 4);

    ed.backspace(); /* remove o codepoint inteiro (2 bytes) */
    expect_line(ed, "caf", 3, 3);

    ed.backspace();
    expect_line(ed, "ca", 2, 2);

    /* Backspace no meio: remove o codepoint a esquerda do cursor. */
    cyberdeck_edit_line mid;
    CHECK(mid.insert("abcd", 4) == true);
    mid.cursor_left();
    mid.cursor_left();              /* cursor entre b|c */
    CHECK(mid.cursor() == 2);
    mid.backspace();                /* remove 'b' */
    expect_line(mid, "acd", 1, 3);

    /* Backspace com linha vazia e cursor 0: no-op. */
    cyberdeck_edit_line empty;
    empty.backspace();
    expect_line(empty, "", 0, 0);
    empty.cursor_left();
    empty.backspace();
    expect_line(empty, "", 0, 0);

    /* Backspace na borda esquerda: no-op. */
    cyberdeck_edit_line edge;
    CHECK(edge.insert("ab", 2) == true);
    edge.cursor_home();
    edge.backspace();
    expect_line(edge, "ab", 0, 2);
}

/* REGRESSAO (A3): del remove o codepoint NA posicao do cursor (S6). */
void test_del_at_cursor()
{
    cyberdeck_edit_line ed;
    CHECK(ed.insert("a\xC3\xA7\xC3\xA3o", 6) == true); /* a, ç, ã, o */
    expect_line(ed, "a\xC3\xA7\xC3\xA3o", 6, 4);

    ed.cursor_home();                  /* cursor 0 */
    ed.del();                          /* remove 'a' (1 byte) */
    expect_line(ed, "\xC3\xA7\xC3\xA3o", 0, 3);

    ed.del();                          /* remove ç (2 bytes) */
    expect_line(ed, "\xC3\xA3o", 0, 2);

    /* del no fim: no-op. */
    cyberdeck_edit_line end;
    CHECK(end.insert("xy", 2) == true);
    end.del();
    expect_line(end, "xy", 2, 2);
}

/* REGRESSAO (A3): setas movem por codepoint com clamp (S7/S13). */
void test_arrows_utf8_and_clamp()
{
    cyberdeck_edit_line ja;
    CHECK(ja.insert("\xE6\x97\xA5\xE6\x9C\xAC", 6) == true); /* 日本 (2 codepoints, 6 bytes) */
    expect_line(ja, "\xE6\x97\xA5\xE6\x9C\xAC", 6, 2);

    ja.cursor_left();
    CHECK(ja.cursor() == 3); /* inicio do 2o codepoint */
    ja.cursor_left();
    CHECK(ja.cursor() == 0);
    ja.cursor_left();        /* clamp na borda */
    CHECK(ja.cursor() == 0);
    ja.cursor_right();
    CHECK(ja.cursor() == 3);
    ja.cursor_right();
    CHECK(ja.cursor() == 6);
    ja.cursor_right();       /* clamp no fim */
    CHECK(ja.cursor() == 6);

    /* Home/End. */
    cyberdeck_edit_line he;
    CHECK(he.insert("abc", 3) == true);
    he.cursor_left();
    he.cursor_home();
    CHECK(he.cursor() == 0);
    he.cursor_end();
    CHECK(he.cursor() == 3);
}

/* REGRESSAO (A3): bytes UTF-8 invalidos contam como 1 byte (paridade com o
 * ramo `else i += 1` de utf8_char_count do TUI). */
void test_invalid_utf8_counted_as_single_byte()
{
    cyberdeck_edit_line ed;
    CHECK(ed.insert("x", 1) == true);
    CHECK(ed.insert("\x80", 1) == true); /* continuacao isolada: 1 byte/1 cp */
    CHECK(ed.insert("\xFF", 1) == true); /* byte invalido: 1 byte/1 cp */
    expect_line(ed, "x\x80\xFF", 3, 3);

    /* Backspace apaga UM byte do byte invalido/isolado. */
    ed.backspace();
    expect_line(ed, "x\x80", 2, 2);

    /* Continuacao apos lead valido e tratada como parte do codepoint:
     * backspace remove o par inteiro. */
    cyberdeck_edit_line utf8;
    CHECK(utf8.insert("\xC3\xA7", 2) == true); /* ç valido */
    utf8.backspace();
    expect_line(utf8, "", 0, 0);
}

/* REGRESSAO (A): visible_line() mascara em PASSWORD, um '*' por BYTE
 * (paridade com `std::string(s_line.size(), '*')` do TUI). */
void test_visible_line_password_mask()
{
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::PASSWORD);
    CHECK(ed.insert("caf\xC3\xA9", 5) == true); /* 5 bytes, 4 codepoints */
    CHECK_EQ(ed.visible_line(), "*****");        /* tamanho em BYTES */
    CHECK(ed.insert("pwd", 3) == true);
    CHECK_EQ(ed.visible_line(), "********");     /* 8 bytes */
    CHECK(ed.line() == "caf\xC3\xA9pwd");        /* dados nao mascaram o modelo */

    cyberdeck_edit_line menu;
    menu.set_session(cyberdeck_session_state::MENU);
    CHECK(menu.insert("ls", 2) == true);
    CHECK_EQ(menu.visible_line(), "ls");
}

/* REGRESSAO (A4): Enter no MENU: LOCAL_COMMAND com payload = linha; linhas
 * em branco (so espacos/tabs) -> NONE. Linha sempre limpa e cursor = 0. */
void test_enter_menu()
{
    // Arrange
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::MENU);
    CHECK(ed.insert("help", 4) == true);

    // Act
    const cyberdeck_enter_result r = ed.enter();

    // Assert
    CHECK(r.action == cyberdeck_enter_action::LOCAL_COMMAND);
    CHECK_EQ(r.payload, "help");
    CHECK_EQ(r.echo, "$ help\n");
    expect_line(ed, "", 0, 0); /* linha limpa, cursor 0 */

    /* Enter com linha em branco (CMD9): nada, linha permanece vazia. */
    // Arrange
    cyberdeck_edit_line blank;
    blank.set_session(cyberdeck_session_state::MENU);
    CHECK(blank.insert("   \t", 4) == true);

    // Act
    const cyberdeck_enter_result rb = blank.enter();

    // Assert
    CHECK(rb.action == cyberdeck_enter_action::NONE);
    CHECK_EQ(rb.payload, "");
    CHECK_EQ(rb.echo, "");
    expect_line(blank, "", 0, 0);

    // Arrange
    cyberdeck_edit_line empty;
    empty.set_session(cyberdeck_session_state::MENU);

    // Act
    const cyberdeck_enter_result re = empty.enter();

    // Assert
    CHECK(re.action == cyberdeck_enter_action::NONE);
    CHECK_EQ(re.payload, "");
    CHECK_EQ(re.echo, "");
    expect_line(empty, "", 0, 0);
}

/* REGRESSAO (A4): Enter no PASSWORD envia a linha UMA vez (payload exato,
 * sem '\n'); linha em branco nao envia (paridade do blank guard). */
void test_enter_password()
{
    // Arrange
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::PASSWORD);
    CHECK(ed.insert("s3cr3t", 6) == true);

    // Act
    const cyberdeck_enter_result r = ed.enter();

    // Assert
    CHECK(r.action == cyberdeck_enter_action::SEND_PASSWORD);
    CHECK_EQ(r.payload, "s3cr3t");
    CHECK_EQ(r.echo, "");
    expect_line(ed, "", 0, 0);

    /* Linha em branco durante PASSWORD: NONE (execute_line sai no blank
     * guard antes de ssh_client_send_password). */
    // Arrange
    cyberdeck_edit_line blank;
    blank.set_session(cyberdeck_session_state::PASSWORD);
    CHECK(blank.insert("  ", 2) == true);

    // Act
    const cyberdeck_enter_result rb = blank.enter();

    // Assert
    CHECK(rb.action == cyberdeck_enter_action::NONE);
    CHECK_EQ(rb.payload, "");
    CHECK_EQ(rb.echo, "");
    expect_line(blank, "", 0, 0);
}

/* REGRESSAO (A4): Enter no HOST_KEY aceita inclusive com linha vazia (TOFU,
 * paridade com C2 do roteiro manual); nenhum dado e enviado. */
void test_enter_host_key()
{
    // Arrange
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::HOST_KEY);
    CHECK(ed.insert("confirma", 8) == true);

    // Act
    const cyberdeck_enter_result r = ed.enter();

    // Assert
    CHECK(r.action == cyberdeck_enter_action::ACCEPT_HOST_KEY);
    CHECK_EQ(r.payload, "");
    CHECK_EQ(r.echo, "");
    expect_line(ed, "", 0, 0);

    // Arrange
    cyberdeck_edit_line empty;
    empty.set_session(cyberdeck_session_state::HOST_KEY);

    // Act
    const cyberdeck_enter_result re = empty.enter();

    // Assert
    CHECK(re.action == cyberdeck_enter_action::ACCEPT_HOST_KEY);
    CHECK_EQ(re.payload, "");
    CHECK_EQ(re.echo, "");
    expect_line(empty, "", 0, 0);
}

/* REGRESSAO (A4): Enter CONNECTED envia comando+newline EXATAMENTE uma vez
 * (payload unico linha + '\n'), limpa a linha, e o Enter seguinte envia so
 * '\n' (o comando nao duplica). */
void test_enter_connected_sends_command_once()
{
    // Arrange
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::CONNECTED);
    CHECK(ed.insert("ls -la", 6) == true);

    // Act
    const cyberdeck_enter_result r = ed.enter();

    // Assert
    CHECK(r.action == cyberdeck_enter_action::SEND_LINE_NEWLINE);
    CHECK_EQ(r.payload, "ls -la\n");
    CHECK_EQ(r.echo, "");

    /* Duplicacao estrutural: os bytes da linha aparecem no payload
     * exatamente uma vez e ha exatamente um '\n' (o final). */
    CHECK(r.payload.find("ls -la") != std::string::npos);
    CHECK(r.payload.find("ls -la") == r.payload.rfind("ls -la"));
    size_t newlines = 0;
    for (char c : r.payload) {
        if (c == '\n') ++newlines;
    }
    CHECK(newlines == 1);

    /* Linha limpa apos o Enter. */
    expect_line(ed, "", 0, 0);

    /* Enter seguinte (linha vazia): apenas '\n' — o comando anterior NAO
     * pode ser reenviado (sem duplicacao). */
    // Act
    const cyberdeck_enter_result r2 = ed.enter();

    // Assert
    CHECK(r2.action == cyberdeck_enter_action::SEND_NEWLINE);
    CHECK_EQ(r2.payload, "\n");
    CHECK_EQ(r2.echo, "");
}

/* REGRESSAO (A4): CONNECTED com linha ja enviada (line_already_sent) envia
 * APENAS o '\n' — a linha nunca e transmitida duas vezes. */
void test_enter_connected_already_sent_newline_only()
{
    // Arrange
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::CONNECTED);
    CHECK(ed.insert("whoami", 6) == true);

    // Act
    const cyberdeck_enter_result r = ed.enter(true /* line_already_sent */);

    // Assert
    CHECK(r.action == cyberdeck_enter_action::SEND_NEWLINE);
    CHECK_EQ(r.payload, "\n");
    CHECK_EQ(r.echo, "");
    CHECK(r.payload.find("whoami") == std::string::npos);

    /* O caminho normal (nao enviada) continua completo. */
    // Arrange
    cyberdeck_edit_line ed2;
    ed2.set_session(cyberdeck_session_state::CONNECTED);
    CHECK(ed2.insert("pwd", 3) == true);

    // Act
    const cyberdeck_enter_result r2 = ed2.enter(false);

    // Assert
    CHECK(r2.action == cyberdeck_enter_action::SEND_LINE_NEWLINE);
    CHECK_EQ(r2.payload, "pwd\n");
    CHECK_EQ(r2.echo, "");
}

/* REGRESSAO (A4): Enter CONNECTED com linha vazia envia '\n' (R2 sem texto). */
void test_enter_connected_empty_sends_newline()
{
    // Arrange
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::CONNECTED);

    // Act
    const cyberdeck_enter_result r = ed.enter();

    // Assert
    CHECK(r.action == cyberdeck_enter_action::SEND_NEWLINE);
    CHECK_EQ(r.payload, "\n");
    CHECK_EQ(r.echo, "");
    expect_line(ed, "", 0, 0);
}

/* ================================================================== */
/* REGRESSAO E (politica de eco aprovada): `echo` explicito.           */
/*                                                                     */
/* cyberdeck_enter_result::echo carrega os bytes EXATOS a anexar ao    */
/* terminal local; "" = SEM eco local. As assinaturas abaixo fixam o   */
/* contrato (o coder preenche `echo` na implementacao):                */
/*   CONNECTED comando   -> payload "cmd\n", eco "";                   */
/*   CONNECTED vazio     -> payload "\n", eco "";                      */
/*   line_already_sent   -> payload "\n" (sem duplicacao), eco "";     */
/*   MENU                -> payload "cmd", eco "$ cmd\n";              */
/*   PASSWORD            -> payload "pwd", eco "";                     */
/*   HOST_KEY            -> ACCEPT_HOST_KEY, payload "", eco "".      */
/* ================================================================== */

/* Descricao legivel do caso para mensagens de falha. */
std::string describe_case(cyberdeck_session_state session, const std::string &line,
                          bool line_already_sent)
{
    const char *names[] = {"MENU", "PASSWORD", "HOST_KEY", "CONNECTED"};
    const unsigned idx = static_cast<unsigned>(session);
    return std::string(idx < 4 ? names[idx] : "?") + " line='" + escape_string(line) +
           "' already_sent=" + (line_already_sent ? "1" : "0");
}

/* Oráculo INDEPENDENTE da politica Enter/eco: monta action/payload/echo
 * esperados apenas a partir das literais do contrato aprovado (sem
 * reutilizar a logica condicional de cyberdeck_edit_line). */
struct expected_enter {
    cyberdeck_enter_action action;
    std::string payload;
    std::string echo;
};

expected_enter oracle_enter(cyberdeck_session_state session, const std::string &line,
                            bool line_already_sent)
{
    const bool blank = line.find_first_not_of(" \t") == std::string::npos;
    switch (session) {
    case cyberdeck_session_state::HOST_KEY:
        /* TOFU preservado: aceita mesmo com linha vazia; sem dados, sem eco. */
        return {cyberdeck_enter_action::ACCEPT_HOST_KEY, "", ""};
    case cyberdeck_session_state::CONNECTED:
        if (line.empty() || line_already_sent) {
            return {cyberdeck_enter_action::SEND_NEWLINE, "\n", ""};
        }
        /* SEM eco local: so o remoto ecoa o comando de volta. */
        return {cyberdeck_enter_action::SEND_LINE_NEWLINE, line + "\n", ""};
    case cyberdeck_session_state::PASSWORD:
        if (blank) return {cyberdeck_enter_action::NONE, "", ""};
        return {cyberdeck_enter_action::SEND_PASSWORD, line, ""};
    case cyberdeck_session_state::MENU:
        if (blank) return {cyberdeck_enter_action::NONE, "", ""};
        return {cyberdeck_enter_action::LOCAL_COMMAND, line, "$ " + line + "\n"};
    }
    return {cyberdeck_enter_action::NONE, "", ""};
}

/* Executa o modelo para o caso e compara action/payload/echo contra o
 * oraculo; tambem verifica que Enter SEMPRE consome a linha. */
void verify_against_oracle(cyberdeck_session_state session, const std::string &line,
                           bool line_already_sent)
{
    cyberdeck_edit_line ed;
    ed.set_session(session);
    if (!line.empty()) CHECK(ed.insert(line.data(), line.size()) == true);
    const cyberdeck_enter_result r = ed.enter(line_already_sent);
    const expected_enter exp = oracle_enter(session, line, line_already_sent);
    const std::string label = describe_case(session, line, line_already_sent);

    ++s_checks;
    if (r.action != exp.action) {
        ++s_failures;
        std::printf("FAIL %s:%d  [%s] acao esperada %d atual %d\n", __FILE__, __LINE__,
                    label.c_str(), static_cast<int>(exp.action),
                    static_cast<int>(r.action));
    }
    CHECK_EQ(r.payload, exp.payload);
    CHECK_EQ(r.echo, exp.echo);

    /* Enter consome a linha EXATAMENTE uma vez em qualquer estado. */
    expect_line(ed, "", 0, 0);
}

/* REGRESSAO (E1): CONNECTED com comando nao vazio -> payload exatamente
 * "cmd\n" e SEM eco local (echo == ""): o comando aparece no terminal
 * apenas pelo echo do lado remoto. */
void test_echo_connected_nonempty_no_local_echo()
{
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::CONNECTED);
    CHECK(ed.insert("ls -la", 6) == true);

    const cyberdeck_enter_result r = ed.enter();
    CHECK(r.action == cyberdeck_enter_action::SEND_LINE_NEWLINE);
    CHECK_EQ(r.payload, "ls -la\n");
    /* Politica: SEM eco local — o eco do remoto e a unica fonte de
     * exibicao do comando. O eco local jamais pode conter "ls -la\n". */
    CHECK(r.echo.empty());
    CHECK_EQ(r.echo, "");
    expect_line(ed, "", 0, 0);

    /* Comando com UTF-8: payload exato linha+"\n", eco continua vazio. */
    cyberdeck_edit_line utf8;
    utf8.set_session(cyberdeck_session_state::CONNECTED);
    CHECK(utf8.insert("echo \xC3\xA7", 7) == true); /* "echo ç" (7 bytes) */
    const cyberdeck_enter_result ru = utf8.enter();
    CHECK(ru.action == cyberdeck_enter_action::SEND_LINE_NEWLINE);
    CHECK_EQ(ru.payload, "echo \xC3\xA7\n");
    CHECK(ru.echo.empty());
    expect_line(utf8, "", 0, 0);
}

/* REGRESSAO (E2): CONNECTED com linha vazia produz somente "\n" (payload),
 * sem eco local. */
void test_echo_connected_empty_newline_only()
{
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::CONNECTED);
    const cyberdeck_enter_result r = ed.enter();
    CHECK(r.action == cyberdeck_enter_action::SEND_NEWLINE);
    CHECK_EQ(r.payload, "\n");
    CHECK(r.echo.empty());
    expect_line(ed, "", 0, 0);
}

/* REGRESSAO (E3): line_already_sent nunca duplica o comando: payload
 * somente "\n" (sem "whoami"), eco vazio. */
void test_echo_line_already_sent_no_duplication()
{
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::CONNECTED);
    CHECK(ed.insert("whoami", 6) == true);

    const cyberdeck_enter_result r = ed.enter(true /* line_already_sent */);
    CHECK(r.action == cyberdeck_enter_action::SEND_NEWLINE);
    CHECK_EQ(r.payload, "\n");
    CHECK(r.payload.find("whoami") == std::string::npos); /* sem duplicacao */
    CHECK(r.echo.empty());
    expect_line(ed, "", 0, 0);
}

/* REGRESSAO (E4): MENU PRESERVA o eco local "$ <cmd>\n" (paridade com o
 * TUI que anexa "$ ", a linha e "\n" ao output). O payload continua sendo
 * apenas a linha (o caller executa e ecoia por conta do campo `echo`). */
void test_echo_menu_local_echo()
{
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::MENU);
    CHECK(ed.insert("help", 4) == true);
    const cyberdeck_enter_result r = ed.enter();
    CHECK(r.action == cyberdeck_enter_action::LOCAL_COMMAND);
    CHECK_EQ(r.payload, "help");
    CHECK_EQ(r.echo, "$ help\n");
    expect_line(ed, "", 0, 0);

    /* MENU com comando UTF-8: eco "$ " + linha + "\n" byte a byte. */
    cyberdeck_edit_line utf8;
    utf8.set_session(cyberdeck_session_state::MENU);
    CHECK(utf8.insert("ol\xC3\xA1", 4) == true); /* "olá" (4 bytes) */
    const cyberdeck_enter_result ru = utf8.enter();
    CHECK(ru.action == cyberdeck_enter_action::LOCAL_COMMAND);
    CHECK_EQ(ru.payload, "ol\xC3\xA1");
    CHECK_EQ(ru.echo, "$ ol\xC3\xA1\n");
    expect_line(utf8, "", 0, 0);

    /* MENU em branco: NONE, payload "" e SEM eco (nada e anexado). */
    cyberdeck_edit_line blank;
    blank.set_session(cyberdeck_session_state::MENU);
    CHECK(blank.insert("   \t", 4) == true);
    const cyberdeck_enter_result rb = blank.enter();
    CHECK(rb.action == cyberdeck_enter_action::NONE);
    CHECK_EQ(rb.payload, "");
    CHECK(rb.echo.empty());
    expect_line(blank, "", 0, 0);
}

/* REGRESSAO (E5): PASSWORD envia a linha sem '\n' e SEM eco local — a
 * senha nunca e ecoada no terminal (a linha ja aparece mascarada durante a
 * digitacao). */
void test_echo_password_no_echo()
{
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::PASSWORD);
    CHECK(ed.insert("s3cr3t", 6) == true);
    const cyberdeck_enter_result r = ed.enter();
    CHECK(r.action == cyberdeck_enter_action::SEND_PASSWORD);
    CHECK_EQ(r.payload, "s3cr3t");
    CHECK(r.echo.empty());
    expect_line(ed, "", 0, 0);
}

/* REGRESSAO (E6): HOST_KEY permanece TOFU inalterado: ACCEPT_HOST_KEY,
 * payload "" e SEM eco local (nenhum dado e anexado ao terminal). */
void test_echo_host_key_preserved()
{
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::HOST_KEY);
    CHECK(ed.insert("confirma", 8) == true);
    const cyberdeck_enter_result r = ed.enter();
    CHECK(r.action == cyberdeck_enter_action::ACCEPT_HOST_KEY);
    CHECK_EQ(r.payload, "");
    CHECK(r.echo.empty());
    expect_line(ed, "", 0, 0);
}

/* REGRESSAO (E7): varredura sistematica da politica — todas as combinacoes
 * sessao x linha x line_already_sent contra o ORACULO independente. A
 * inclusao de "   " (somente espacos) em CONNECTED fixa a regra do contrato:
 * so linha REALMENTE vazia (ou line_already_sent) vira SEND_NEWLINE; espacos
 * sao transmitidos como comando ("   \n"), sem eco local. */
void test_echo_policy_oracle_sweep()
{
    const struct {
        cyberdeck_session_state session;
        const char *line;
    } cases[] = {
        {cyberdeck_session_state::MENU, ""},
        {cyberdeck_session_state::MENU, "help"},
        {cyberdeck_session_state::MENU, "  \t "},
        {cyberdeck_session_state::PASSWORD, ""},
        {cyberdeck_session_state::PASSWORD, "p4ss"},
        {cyberdeck_session_state::PASSWORD, " \t"},
        {cyberdeck_session_state::HOST_KEY, ""},
        {cyberdeck_session_state::HOST_KEY, "qualquer texto"},
        {cyberdeck_session_state::CONNECTED, ""},
        {cyberdeck_session_state::CONNECTED, "ls -la"},
        {cyberdeck_session_state::CONNECTED, "   "}, /* espacos: comando */
        {cyberdeck_session_state::CONNECTED, "echo \xC3\xA7"}, /* UTF-8 */
    };
    for (const auto &c : cases) {
        /* line_already_sent so afeta CONNECTED; nas demais sessoes e
         * ignorado (paridade com o contrato documentado). */
        for (int sent = 0; sent < 2; ++sent) {
            verify_against_oracle(c.session, c.line, sent != 0);
        }
    }
}

/* REGRESSAO (A5): limite hard de 12288 — nenhuma insercao parcial e nenhum
 * estouro. Paridade com `s_line.size() + length > TERMINAL_LIMIT` do TUI. */
void test_hard_limit_no_partial_insert()
{
    cyberdeck_edit_line ed;
    /* Chunk de 12 bytes: 12288 / 12 = 1024 insercoes exatas. */
    const char *chunk = "abcdefghijkl";
    const size_t chunk_len = 12;
    size_t inserted = 0;
    while (inserted < kSpecLimit) {
        CHECK(ed.insert(chunk, chunk_len) == true);
        inserted += chunk_len;
    }
    CHECK(ed.size() == kSpecLimit);
    CHECK(ed.cursor() == kSpecLimit);

    /* Um byte alem do limite: rejeitado integralmente — nem a linha nem o
     * cursor mudam (sem insercao parcial). */
    CHECK(ed.insert("x", 1) == false);
    CHECK(ed.size() == kSpecLimit);
    CHECK(ed.cursor() == kSpecLimit);
    std::string expected;
    for (size_t i = 0; i < kSpecLimit / chunk_len; ++i) {
        expected += chunk;
    }
    CHECK(ed.line() == expected);

    /* Backspace libera espaco e a insercao volta a caber: estado coerente. */
    ed.backspace();
    CHECK(ed.size() == kSpecLimit - 1);
    CHECK(ed.insert("z", 1) == true);
    CHECK(ed.size() == kSpecLimit);
    CHECK(ed.line().back() == 'z');
}

/* clear() reseta linha e cursor; o modelo segue utilizavel. */
void test_clear_resets()
{
    cyberdeck_edit_line ed;
    CHECK(ed.insert("abc", 3) == true);
    ed.cursor_left();
    ed.clear();
    expect_line(ed, "", 0, 0);
    CHECK(ed.insert("ok", 2) == true);
    expect_line(ed, "ok", 2, 2);
}

/* Determinismo: states iniciais identicos produzem sequencias identicas. */
void test_deterministic()
{
    const auto run_once = []() {
        cyberdeck_edit_line ed;
        ed.set_session(cyberdeck_session_state::CONNECTED);
        ed.insert("echo ", 5);
        ed.insert("ol\xC3\xA1", 4);
        ed.cursor_left();
        ed.cursor_left();
        ed.insert("\xC3\xA7", 2);
        ed.cursor_end();
        const cyberdeck_enter_result r = ed.enter();
        return r.payload;
    };
    CHECK_EQ(run_once(), run_once());
}

/* CASO DE BORDA: Enter com o cursor no meio da linha (consome o comando
 * inteiro independentemente da posicao do cursor e zera a linha). */
void test_enter_with_cursor_in_middle()
{
    // Arrange - MENU
    cyberdeck_edit_line menu;
    menu.set_session(cyberdeck_session_state::MENU);
    CHECK(menu.insert("ls -la", 6) == true);
    menu.cursor_left();
    menu.cursor_left();
    CHECK(menu.cursor() == 4);

    // Act
    const cyberdeck_enter_result rm = menu.enter();

    // Assert
    CHECK(rm.action == cyberdeck_enter_action::LOCAL_COMMAND);
    CHECK_EQ(rm.payload, "ls -la");
    CHECK_EQ(rm.echo, "$ ls -la\n");
    expect_line(menu, "", 0, 0);

    // Arrange - CONNECTED
    cyberdeck_edit_line conn;
    conn.set_session(cyberdeck_session_state::CONNECTED);
    CHECK(conn.insert("uname -a", 8) == true);
    conn.cursor_home();
    CHECK(conn.cursor() == 0);

    // Act
    const cyberdeck_enter_result rc = conn.enter();

    // Assert
    CHECK(rc.action == cyberdeck_enter_action::SEND_LINE_NEWLINE);
    CHECK_EQ(rc.payload, "uname -a\n");
    CHECK_EQ(rc.echo, "");
    expect_line(conn, "", 0, 0);
}

/* CASO DE BORDA: Enters consecutivos em MENU, PASSWORD e HOST_KEY.
 * O primeiro Enter consome o texto; o segundo Enter age sobre a linha vazia. */
void test_consecutive_enter_all_states()
{
    // MENU
    cyberdeck_edit_line menu;
    menu.set_session(cyberdeck_session_state::MENU);
    CHECK(menu.insert("ping", 4) == true);
    const cyberdeck_enter_result m1 = menu.enter();
    CHECK(m1.action == cyberdeck_enter_action::LOCAL_COMMAND);
    CHECK_EQ(m1.payload, "ping");
    CHECK_EQ(m1.echo, "$ ping\n");
    expect_line(menu, "", 0, 0);

    const cyberdeck_enter_result m2 = menu.enter();
    CHECK(m2.action == cyberdeck_enter_action::NONE);
    CHECK_EQ(m2.payload, "");
    CHECK_EQ(m2.echo, "");
    expect_line(menu, "", 0, 0);

    // PASSWORD
    cyberdeck_edit_line pwd;
    pwd.set_session(cyberdeck_session_state::PASSWORD);
    CHECK(pwd.insert("pass123", 7) == true);
    const cyberdeck_enter_result p1 = pwd.enter();
    CHECK(p1.action == cyberdeck_enter_action::SEND_PASSWORD);
    CHECK_EQ(p1.payload, "pass123");
    CHECK_EQ(p1.echo, "");
    expect_line(pwd, "", 0, 0);

    const cyberdeck_enter_result p2 = pwd.enter();
    CHECK(p2.action == cyberdeck_enter_action::NONE);
    CHECK_EQ(p2.payload, "");
    CHECK_EQ(p2.echo, "");
    expect_line(pwd, "", 0, 0);

    // HOST_KEY
    cyberdeck_edit_line hk;
    hk.set_session(cyberdeck_session_state::HOST_KEY);
    CHECK(hk.insert("yes", 3) == true);
    const cyberdeck_enter_result h1 = hk.enter();
    CHECK(h1.action == cyberdeck_enter_action::ACCEPT_HOST_KEY);
    CHECK_EQ(h1.payload, "");
    CHECK_EQ(h1.echo, "");
    expect_line(hk, "", 0, 0);

    const cyberdeck_enter_result h2 = hk.enter();
    CHECK(h2.action == cyberdeck_enter_action::ACCEPT_HOST_KEY);
    CHECK_EQ(h2.payload, "");
    CHECK_EQ(h2.echo, "");
    expect_line(hk, "", 0, 0);
}

/* CASO DE BORDA: MENU com linha no limite maximo (12288 bytes) gera eco integro
 * ("$ " + 12288 bytes + "\n" = 12291 bytes). */
void test_hard_limit_menu_echo()
{
    // Arrange
    cyberdeck_edit_line ed;
    ed.set_session(cyberdeck_session_state::MENU);
    const std::string chunk = "1234567890ab"; // 12 bytes
    std::string full_line;
    full_line.reserve(kSpecLimit);
    for (size_t i = 0; i < kSpecLimit / chunk.size(); ++i) {
        full_line += chunk;
    }
    CHECK(ed.insert(full_line.data(), full_line.size()) == true);
    CHECK(ed.size() == kSpecLimit);

    // Act
    const cyberdeck_enter_result r = ed.enter();

    // Assert
    CHECK(r.action == cyberdeck_enter_action::LOCAL_COMMAND);
    CHECK_EQ(r.payload, full_line);
    const std::string expected_echo = "$ " + full_line + "\n";
    CHECK_EQ(r.echo, expected_echo);
    expect_line(ed, "", 0, 0);
}

} // namespace

int main()
{
    test_insert_each_char_once();
    test_physical_virtual_parity();
    test_insert_in_middle_shifts_and_advances_cursor();
    test_backspace_utf8_and_middle();
    test_del_at_cursor();
    test_arrows_utf8_and_clamp();
    test_invalid_utf8_counted_as_single_byte();
    test_visible_line_password_mask();
    test_enter_menu();
    test_enter_password();
    test_enter_host_key();
    test_enter_connected_sends_command_once();
    test_enter_connected_already_sent_newline_only();
    test_enter_connected_empty_sends_newline();
    test_echo_connected_nonempty_no_local_echo();
    test_echo_connected_empty_newline_only();
    test_echo_line_already_sent_no_duplication();
    test_echo_menu_local_echo();
    test_echo_password_no_echo();
    test_echo_host_key_preserved();
    test_echo_policy_oracle_sweep();
    test_enter_with_cursor_in_middle();
    test_consecutive_enter_all_states();
    test_hard_limit_menu_echo();
    test_hard_limit_no_partial_insert();
    test_clear_resets();
    test_deterministic();

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_edit_line (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
