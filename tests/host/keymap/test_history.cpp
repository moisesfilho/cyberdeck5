/*
 * Testes unitarios host-side para o historico local do terminal TUI.
 * Cobre components/cyberdeck/src/core/cyberdeck_history.cpp (logica pura extraida
 * de cyberdeck_ui.cpp, namespace anonimo, sem dependencia de LVGL).
 *
 * Contrato sob teste (paridade com o TUI):
 *   - Up/Down navegam do mais recente para o mais antigo, com clamp nas bordas
 *     e "linha de edicao vazia" no final (posicao == size());
 *   - limite HARD de 64 entradas; a 65a descarta a mais antiga (erase(begin));
 *   - linhas em branco (so espacos/tabs, ou vazias) NAO sao persistidas;
 *   - add() re-sincroniza a posicao com o final antes do guard de branco
 *     (paridade com execute_line(), que roda s_history_pos = size() primeiro).
 *
 * Estruturado segundo o padrao AAA (Arrange, Act, Assert) e principios F.I.R.S.T.
 *
 * Build: make test (veja Makefile; sem dependencias alem de g++/make).
 */
#include "cyberdeck_history.h"

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

/* Limite HARD especificado pelo TUI (paridade com HISTORY_LIMIT = 64 em
 * cyberdeck_ui.cpp). O literal e o contrato: se o valor em producao mudar,
 * este static_assert quebra o build e forcara revisao consciente. */
constexpr size_t kSpecLimit = 64;
static_assert(cyberdeck_history::limit == kSpecLimit,
              "HISTORY_LIMIT do TUI mudou: reveja o contrato e atualize os testes");

/* Preenche historico com n entradas distintas "cmd1".."cmdn" (todas nao-blank). */
void fill(cyberdeck_history &h, size_t n)
{
    for (size_t i = 1; i <= n; ++i) {
        CHECK(h.add("cmd" + std::to_string(i)) == true);
    }
}

void test_add_persists_non_blank()
{
    // Arrange
    cyberdeck_history h;
    // Act
    CHECK(h.add("help") == true);
    CHECK(h.add("wifi") == true);
    // Assert
    CHECK(h.size() == 2);
    CHECK_EQ(h.at(0), "help");
    CHECK_EQ(h.at(1), "wifi");
    /* Apos add, posicao aponta para o fim (linha de edicao vazia);
     * o primeiro move_up() recupera a entrada mais recente ("wifi"). */
    CHECK_EQ(h.current(), "");
    h.move_up();
    CHECK_EQ(h.current(), "wifi");
    h.move_up();
    CHECK_EQ(h.current(), "help");
}

void test_blank_lines_not_persisted()
{
    // Arrange
    cyberdeck_history h;
    const char *blanks[] = {"", " ", "\t", "  \t ", " \t\t "};
    // Act & Assert: nenhum blank e persistido (retorno false, size inalterado)
    for (const char *b : blanks) {
        CHECK(h.add(b) == false);
    }
    CHECK(h.add(static_cast<const char *>(nullptr)) == false);
    CHECK(h.empty());
    CHECK(h.size() == 0);
    /* Corpo do historico imutavel: mesmo com entradas existentes. */
    fill(h, 3);
    CHECK(h.size() == 3);
    CHECK(h.add("") == false);
    CHECK(h.add("   ") == false);
    CHECK(h.add("\t \t") == false);
    CHECK(h.size() == 3);
    CHECK_EQ(h.at(0), "cmd1");
    CHECK_EQ(h.at(2), "cmd3");
}

void test_is_blank_predicate_parity()
{
    // Predicado identico ao do TUI (find_first_not_of(" \t") == npos):
    // apenas espaco e tab contam como branco.
    CHECK(cyberdeck_history::is_blank(""));
    CHECK(cyberdeck_history::is_blank(" "));
    CHECK(cyberdeck_history::is_blank("\t"));
    CHECK(cyberdeck_history::is_blank(" \t\t  "));
    CHECK(cyberdeck_history::is_blank(static_cast<const char *>(nullptr)));
    CHECK(!cyberdeck_history::is_blank("a"));
    CHECK(!cyberdeck_history::is_blank(" a"));
    CHECK(!cyberdeck_history::is_blank("a "));
    CHECK(!cyberdeck_history::is_blank("\ta"));
    /* Paridade fina: \r e \n nao sao "branco" para este predicado. Isso pinta
     * o comportamento atual do TUI e guarda contra drift de extracao. */
    CHECK(!cyberdeck_history::is_blank("\r"));
    CHECK(!cyberdeck_history::is_blank("\n"));
    CHECK(!cyberdeck_history::is_blank("\v"));
    CHECK(!cyberdeck_history::is_blank("\f"));

    // add() segue o mesmo predicado: interior com espaco e persistido.
    cyberdeck_history h;
    CHECK(h.add("a b") == true);
    CHECK(h.add("  a") == true);
    CHECK(h.add("a\tb") == true);
    CHECK(h.size() == 3);
    /* Apenas-ascii: \r nao e tratado como branco pelo TUI, logo e persistido
     * (comportamento atual documentado; nao e requisito novo). */
    CHECK(h.add("\r") == true);
    CHECK(h.size() == 4);
}

void test_duplicates_persisted()
{
    // Arrange
    cyberdeck_history h;
    // Act: linhas repetidas sao persistidas (nao ha dedup no TUI)
    CHECK(h.add("wifi") == true);
    CHECK(h.add("wifi") == true);
    CHECK(h.add("wifi") == true);
    // Assert
    CHECK(h.size() == 3);
    CHECK_EQ(h.at(0), "wifi");
    CHECK_EQ(h.at(1), "wifi");
    CHECK_EQ(h.at(2), "wifi");
}

void test_limit_64_no_discard_below_capacity()
{
    // Arrange
    cyberdeck_history h;
    // Act: 64 entradas (= limite) cabem sem descarte
    fill(h, kSpecLimit);
    // Assert
    CHECK(h.size() == kSpecLimit);
    CHECK_EQ(h.at(0), "cmd1");
    CHECK_EQ(h.at(kSpecLimit - 1), "cmd64");
}

void test_limit_discards_oldest()
{
    // Arrange & Act 1: 65a entrada estoura o limite -> descarta a mais antiga
    cyberdeck_history h;
    fill(h, kSpecLimit + 1);

    // Assert 1
    CHECK(h.size() == kSpecLimit);
    CHECK_EQ(h.at(0), "cmd2"); /* cmd1 (mais antiga) descartada */
    CHECK_EQ(h.at(kSpecLimit - 1), "cmd65");

    // Arrange & Act 2: Instancia limpa recebendo 70 entradas (descarta as 6 primeiras)
    cyberdeck_history h_bulk;
    fill(h_bulk, 70);

    // Assert 2
    CHECK(h_bulk.size() == kSpecLimit);
    CHECK_EQ(h_bulk.at(0), "cmd7");
    CHECK_EQ(h_bulk.at(kSpecLimit - 1), "cmd70");
}

void test_navigation_single_entry_history_clamps()
{
    // Arrange: historico com exatamente 1 entrada posicionado na linha de edicao final
    cyberdeck_history h;
    const bool added = h.add("single_cmd");
    CHECK(added == true);
    CHECK(h.size() == 1);
    h.reset_position();
    CHECK_EQ(h.current(), "");

    // Act & Assert 1: Up seleciona a unica entrada e clamp mantem nela
    h.move_up();
    CHECK_EQ(h.current(), "single_cmd");
    h.move_up();
    CHECK_EQ(h.current(), "single_cmd");

    // Act & Assert 2: Down avanca para a linha vazia (pos == size) e faz clamp no fim
    h.move_down();
    CHECK_EQ(h.current(), "");
    h.move_down();
    CHECK_EQ(h.current(), "");

    // Act & Assert 3: Novo Up volta para o comando existente
    h.move_up();
    CHECK_EQ(h.current(), "single_cmd");
}

void test_navigation_up_clamps_at_oldest()
{
    // Arrange
    cyberdeck_history h;
    fill(h, 3); /* historico: cmd1 cmd2 cmd3; pos = 3 (fim / linha vazia) */
    // Act: Up percorre ate a mais antiga e faz clamp
    h.move_up();
    CHECK_EQ(h.current(), "cmd3");
    h.move_up();
    CHECK_EQ(h.current(), "cmd2");
    h.move_up();
    CHECK_EQ(h.current(), "cmd1");
    h.move_up(); /* ja na borda: nao sai do range */
    // Assert
    CHECK_EQ(h.current(), "cmd1");
    h.move_up();
    CHECK_EQ(h.current(), "cmd1");
}

void test_navigation_down_clamps_at_end()
{
    // Arrange
    cyberdeck_history h;
    fill(h, 3);
    // Act: a partir do fim, Down fica na linha de edicao vazia
    h.reset_position();
    CHECK_EQ(h.current(), "");
    h.move_down(); /* ja no fim: clamp */
    CHECK_EQ(h.current(), "");
    // Act: ciclo Up -> Down completo
    h.move_up();   /* pos 2 -> cmd3 */
    h.move_up();   /* pos 1 -> cmd2 */
    h.move_down(); /* pos 2 -> cmd3 */
    CHECK_EQ(h.current(), "cmd3");
    h.move_down(); /* pos 3 -> fim (linha vazia) */
    CHECK_EQ(h.current(), "");
    h.move_down(); /* clamp no fim */
    CHECK_EQ(h.current(), "");
}

void test_navigation_empty_history_is_noop()
{
    // Arrange
    cyberdeck_history h;
    // Act & Assert
    h.move_up();
    h.move_down();
    CHECK(h.empty());
    CHECK_EQ(h.current(), "");
    h.reset_position();
    CHECK_EQ(h.current(), "");
}

void test_add_resets_position_even_for_blank()
{
    /* Paridade com execute_line(): s_history.reset_position() roda ANTES do guard
     * de linha em branco. Pressionar Enter numa linha em branco sai do modo
     * de navegacao (posicao volta ao fim) sem persistir nada. */
    // Arrange
    cyberdeck_history h;
    fill(h, 3);
    h.move_up(); /* pos 2 -> cmd3 */
    h.move_up(); /* pos 1 -> cmd2 */
    CHECK_EQ(h.current(), "cmd2");
    // Act: Enter com linha em branco
    CHECK(h.add("   ") == false);
    // Assert: nao persistiu, mas a posicao voltou ao fim
    CHECK(h.size() == 3);
    CHECK_EQ(h.current(), "");
    // Act: nova navegacao para conferir que a linha nao entrou no historico
    h.move_up();
    CHECK_EQ(h.current(), "cmd3");
}

void test_limit_boundary_navigation_after_65th_add()
{
    /* Apos a 65a entrada: size=64, pos = 64 (== size) -> current() vazio;
     * Up deve trazer a entrada mais recente que sobrou (cmd65),
     * percorrer ate a mais antiga remanescente (cmd2) e fazer clamp. */
    // Arrange
    cyberdeck_history h;
    fill(h, kSpecLimit + 1);

    // Assert 1: estado pos-descarte
    CHECK(h.size() == kSpecLimit);
    CHECK_EQ(h.at(0), "cmd2");
    CHECK_EQ(h.current(), ""); /* pos 64 == size (fim / linha vazia) */

    // Act & Assert 2: Up volta para a entrada mais recente e percorre ate o topo
    h.move_up();
    CHECK_EQ(h.current(), "cmd65");
    h.move_up();
    CHECK_EQ(h.current(), "cmd64");

    // Percorre todos os itens ate o mais antigo restante (cmd2)
    for (size_t i = 63; i >= 2; --i) {
        h.move_up();
    }
    CHECK_EQ(h.current(), "cmd2");

    // Clamp na borda superior
    h.move_up();
    CHECK_EQ(h.current(), "cmd2");
}

void test_add_during_navigation_resets_pos_and_persists()
{
    // Arrange: historico com 5 entradas e cursor posicionado no meio (cmd3)
    cyberdeck_history h;
    fill(h, 5);
    h.move_up(); /* cmd5 */
    h.move_up(); /* cmd4 */
    h.move_up(); /* cmd3 */
    CHECK_EQ(h.current(), "cmd3");

    // Act: usuario digita e submete novo comando valido
    CHECK(h.add("cmd_new") == true);

    // Assert: tamanho incrementado, cursor resetado para o fim (linha de edicao vazia)
    CHECK(h.size() == 6);
    CHECK_EQ(h.current(), "");

    // Act & Assert: Up recupera o novo comando como o mais recente
    h.move_up();
    CHECK_EQ(h.current(), "cmd_new");
    h.move_up();
    CHECK_EQ(h.current(), "cmd5");
}

void test_reset_position()
{
    // Arrange
    cyberdeck_history h;
    fill(h, 3);
    h.move_up(); /* pos 2 -> cmd3 */
    h.move_up(); /* pos 1 -> cmd2 */
    h.move_up(); /* pos 0 -> cmd1 */
    CHECK_EQ(h.current(), "cmd1");
    // Act
    h.reset_position();
    // Assert: fim = linha de edicao vazia
    CHECK_EQ(h.current(), "");
    h.move_down(); /* clamp no fim */
    CHECK_EQ(h.current(), "");
    h.move_up(); /* up volta para a mais recente */
    CHECK_EQ(h.current(), "cmd3");
}

void test_clear()
{
    // Arrange
    cyberdeck_history h;
    fill(h, 3);
    h.move_up();
    CHECK(h.size() == 3);
    // Act
    h.clear();
    // Assert
    CHECK(h.empty());
    CHECK_EQ(h.current(), "");
    h.move_up();
    h.move_down();
    CHECK_EQ(h.current(), "");
    CHECK(h.add("help") == true);
    CHECK(h.size() == 1);
    CHECK_EQ(h.current(), "");
    h.move_up();
    CHECK_EQ(h.current(), "help");
}

void test_at_out_of_range_is_safe_and_deterministic()
{
    // Arrange
    cyberdeck_history h;
    // Act & Assert: sem UB, retorna ""
    CHECK_EQ(h.at(0), "");
    CHECK_EQ(h.at(99), "");
    fill(h, 2);
    CHECK_EQ(h.at(2), ""); /* == size */
    CHECK_EQ(h.at(3), "");
    CHECK_EQ(h.at(65535), "");
    /* Conteudo real preservado. */
    CHECK_EQ(h.at(0), "cmd1");
    CHECK_EQ(h.at(1), "cmd2");
}

void test_add_null_safe()
{
    // Arrange
    cyberdeck_history h;
    // Act: add(nullptr) e tratado como blank (nao persistido)
    CHECK(h.add(static_cast<const char *>(nullptr)) == false);
    CHECK(h.empty());
    fill(h, 1);
    CHECK(h.add(static_cast<const char *>(nullptr)) == false);
    CHECK(h.size() == 1);
    CHECK_EQ(h.at(0), "cmd1");
}

} // namespace

int main()
{
    test_add_persists_non_blank();
    test_blank_lines_not_persisted();
    test_is_blank_predicate_parity();
    test_duplicates_persisted();
    test_limit_64_no_discard_below_capacity();
    test_limit_discards_oldest();
    test_navigation_single_entry_history_clamps();
    test_navigation_up_clamps_at_oldest();
    test_navigation_down_clamps_at_end();
    test_navigation_empty_history_is_noop();
    test_add_resets_position_even_for_blank();
    test_limit_boundary_navigation_after_65th_add();
    test_add_during_navigation_resets_pos_and_persists();
    test_reset_position();
    test_clear();
    test_at_out_of_range_is_safe_and_deterministic();
    test_add_null_safe();

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_history (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
