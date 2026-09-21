/*
 * Testes host-side para o parser do shell de entrada (keymap do teclado
 * fisico Tab5). Cobre components/cyberdeck/src/core/tab5_keyboard_keys.cpp.
 *
 * A funcao tab5_keymap_lookup mapeia a string UTF-8/ASCII devolvida pelo
 * firmware do teclado (modo Character) para uma acao LVGL. E a porta de
 * entrada do "shell": cada tecla fisica vira texto injetado no campo focado
 * ou uma tecla especial de navegacao/controle.
 *
 * Build: make test  (veja Makefile; sem dependencias alem de g++/make).
 */
#include "tab5_keyboard_keys.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int s_failures = 0;
int s_checks = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++s_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);          \
        }                                                                              \
    } while (0)

/* Entrada valida de acao CHAR: string reconhecida vira caractere imprimivel. */
void check_char(const char *str, char expected_ch)
{
    const tab5_key_entry_t *e = tab5_keymap_lookup(str);
    CHECK(e != nullptr);
    if (e == nullptr) {
        return;
    }
    CHECK(e->type == TAB5_KEY_CHAR);
    CHECK(e->ch == expected_ch);
    CHECK(e->lvgl_key == 0);
    if (e->type != TAB5_KEY_CHAR || e->ch != expected_ch || e->lvgl_key != 0) {
        std::printf("  -> keymap['%s'] type=%d ch='%c'(%d) lvgl=%u\n", str, (int)e->type,
                    e->ch >= 32 && e->ch < 127 ? e->ch : '.', (int)e->ch, (unsigned)e->lvgl_key);
    }
}

/* Entrada valida de acao SPECIAL: string vira tecla de controle/navegacao. */
void check_special(const char *str, uint32_t expected_key)
{
    const tab5_key_entry_t *e = tab5_keymap_lookup(str);
    CHECK(e != nullptr);
    if (e == nullptr) {
        return;
    }
    CHECK(e->type == TAB5_KEY_SPECIAL);
    CHECK(e->lvgl_key == expected_key);
    CHECK(e->ch == 0);
    if (e->type != TAB5_KEY_SPECIAL || e->lvgl_key != expected_key || e->ch != 0) {
        std::printf("  -> keymap[%s] type=%d ch=%d lvgl=%u\n", str, (int)e->type, (int)e->ch,
                    (unsigned)e->lvgl_key);
    }
}

void check_unknown(const char *str)
{
    const tab5_key_entry_t *e = tab5_keymap_lookup(str);
    CHECK(e == nullptr);
    if (e != nullptr) {
        std::printf("  -> keymap['%s'] NAO deveria existir (type=%d ch=%c lvgl=%u)\n", str,
                    (int)e->type, e->ch, (unsigned)e->lvgl_key);
    }
}

void test_control_byte_entries()
{
    /* Bytes de controle ASCII emitidos pelo firmware (modo Character). */
    check_special("\n", LV_KEY_ENTER);
    check_special("\b", LV_KEY_BACKSPACE);
    check_special("\x7F", LV_KEY_DEL);
    check_special("\x1B", LV_KEY_ESC);
    check_special("\x11", LV_KEY_UP);
    check_special("\x12", LV_KEY_DOWN);
    check_special("\x13", LV_KEY_RIGHT);
    check_special("\x14", LV_KEY_LEFT);
    /* Tab e tratado como caractere (nao SPECIAL) no mapa atual. */
    check_char("\t", '\t');
}

void test_name_variant_entries()
{
    /* Variante do firmware que emite nomes em vez de bytes de controle. */
    check_special("esc", LV_KEY_ESC);
    check_special("enter", LV_KEY_ENTER);
    check_special("backspace", LV_KEY_BACKSPACE);
    check_special("del", LV_KEY_DEL);
    check_special("up", LV_KEY_UP);
    check_special("down", LV_KEY_DOWN);
    check_special("left", LV_KEY_LEFT);
    check_special("right", LV_KEY_RIGHT);
    check_char("space", ' ');
    check_char(" ", ' ');
}

void test_printable_chars()
{
    /* Letras minusculas e maiusculas completas. */
    for (char c = 'a'; c <= 'z'; ++c) {
        char s[2] = {c, '\0'};
        check_char(s, c);
    }
    for (char c = 'A'; c <= 'Z'; ++c) {
        char s[2] = {c, '\0'};
        check_char(s, c);
    }
    /* Digitos. */
    for (char c = '0'; c <= '9'; ++c) {
        char s[2] = {c, '\0'};
        check_char(s, c);
    }
    /* Simbolos mapeados (incluindo os que exigem escape em literais). */
    const char *symbols = "`~!@#$%^&*()[]{}|\\;:'\",.<>/?_-+=";
    for (const char *p = symbols; *p != '\0'; ++p) {
        char s[2] = {*p, '\0'};
        check_char(s, *p);
    }
}

void test_edge_and_invalid_inputs()
{
    check_unknown(nullptr);
    check_unknown("");
    check_unknown("qix");      /* string desconhecida */
    check_unknown("ctrl");     /* modificadores nao sao mapeados como acao */
    check_unknown("alt");
    check_unknown("sym");
    check_unknown("shift");
    check_unknown("fn");
    check_unknown("opt");
    check_unknown("cmd");
    check_unknown("aa");       /* multibyte desconhecido */
    check_unknown("ENTER");    /* case-sensitive: nome em maiusculas nao existe */
    check_unknown("Enter");

    /* Bytes de controle ASCII nao mapeados diretamente na tabela */
    check_unknown("\x01");     /* SOH */
    check_unknown("\x03");     /* ETX / Ctrl+C */
    check_unknown("\x04");     /* EOT / Ctrl+D */
    check_unknown("\x1F");     /* US */
}

void test_known_surface()
{
    /* Whitelist derivada da tabela de producao (tab5_keyboard_keys.cpp).
     * Garante que nenhuma entrada existente seja removida ou renomeada sem
     * quebrar este teste (rede de regressao do parser do shell). */
    const char *known[] = {
        "\t", "\n", "\b", "\x7F", "\x1B", "\x11", "\x12", "\x13", "\x14",
        "esc", "enter", "backspace", "del", "up", "down", "left", "right", "space", " ",
        "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
        "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "`", "~", "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "[", "]",
        "{", "}", "\\", "|", ";", ":", "'", "\"", ",", ".", "<", ">", "/", "?",
        "-", "_", "+", "=",
    };
    for (const char *s : known) {
        CHECK(tab5_keymap_lookup(s) != nullptr);
    }
    /* Unicidade de primeira ocorrencia e paridade entre variantes nomeadas e bytes de controle. */
    struct AliasPair {
        const char *named;
        const char *byte;
    };
    const AliasPair alias_pairs[] = {
        {"esc", "\x1B"},
        {"enter", "\n"},
        {"backspace", "\b"},
        {"del", "\x7F"},
        {"up", "\x11"},
        {"down", "\x12"},
        {"left", "\x14"},
        {"right", "\x13"},
        {"space", " "},
    };
    for (const auto &pair : alias_pairs) {
        const tab5_key_entry_t *e_named = tab5_keymap_lookup(pair.named);
        const tab5_key_entry_t *e_byte = tab5_keymap_lookup(pair.byte);
        CHECK(e_named != nullptr);
        CHECK(e_byte != nullptr);
        if (e_named != nullptr && e_byte != nullptr) {
            CHECK(e_named != e_byte);
            CHECK(e_named->type == e_byte->type);
            CHECK(e_named->lvgl_key == e_byte->lvgl_key);
            CHECK(e_named->ch == e_byte->ch);
        }
    }
}

void test_exact_match_and_multibyte_negatives()
{
    /* Match exato: espacos nas bordas nao sao normalizados (o firmware nao os
     * emite para nomes de tecla). */
    check_unknown("enter ");
    check_unknown(" enter");
    check_unknown("esc ");
    check_unknown("space ");
    check_unknown("backspace\t");

    /* Nomes em caixa alta ou parcial nao casam. */
    check_unknown("Esc");
    check_unknown("Backspace");
    check_unknown("SPACE");
    check_unknown("ente");
    check_unknown("righ");
    check_unknown("del ");

    /* Strings multibyte UTF-8 reais NAO estao na tabela: o fallback de
     * primeiro byte e tratado em tab5_keyboard.cpp, nao no keymap. */
    check_unknown("\xC3\xA1");     /* U+00E1 a-agudo (2 bytes) */
    check_unknown("\xC3\xA7");     /* U+00E7 c-cedilha (2 bytes) */
    check_unknown("\xE2\x82\xAC"); /* U+20AC euro (3 bytes) */

    /* Sequencias VT100 completas nao sao entradas do keymap (as setas usam
     * bytes de controle unicos: 0x11..0x14). */
    check_unknown("\x1B[A");
    check_unknown("\x1B[3~");

    /* String longa desconhecida: deve retornar nullptr sem over-read/crash. */
    const std::string long_unknown(4096, 'x');
    check_unknown(long_unknown.c_str());
}

} // namespace

int main()
{
    test_control_byte_entries();
    test_name_variant_entries();
    test_printable_chars();
    test_edge_and_invalid_inputs();
    test_known_surface();
    test_exact_match_and_multibyte_negatives();

    if (s_failures == 0) {
        std::printf("PASS: tab5_keymap_lookup (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
