/*
 * Testes de REGRESSAO host-side para a guarda incremental de eco remoto do SSH
 * (contrato em components/cyberdeck/include/features/shell/cyberdeck_ssh_echo_guard.h;
 * implementacao futura em
 * components/cyberdeck/src/features/shell/cyberdeck_ssh_echo_guard.cpp).
 *
 * Feature aprovada sob teste (historico/eco remoto SSH):
 *   - armar o guard com o payload local exato (ex.: "cmd\n" ou "\n" para linha
 *     vazia);
 *   - alimentar chunks do fluxo remoto JA FILTRADO (ANSI/CSI/OSC removidos,
 *     CRLF normalizado, C0/DEL descartados) e produzir os bytes a exibir;
 *   - NAO duplicar o eco quando o inicio do fluxo remoto corresponde ao payload
 *     armado (byte a byte, inclusive com fragmentacao e UTF-8);
 *   - se NAO houver eco (divergencia) ou o eco ficar incompleto, PRESERVAR a
 *     saida integralmente (prefixo retido nunca e engolido);
 *   - saida imediata na divergencia do primeiro byte;
 *   - multiplos comandos sequenciais e reuso apos flush();
 *   - bytes opacos (UTF-8 / saida pos-filtro) preservados byte a byte.
 *
 * TDD (mesmo padrao de test_terminal_filter/test_wifi_indicator): enquanto
 * cyberdeck_ssh_echo_guard.cpp nao existir, `make test_ssh_echo_guard` falha
 * por MODULO AUSENTE ("No rule to make target ...cyberdeck_ssh_echo_guard.cpp")
 * e, por consequencia, `make test` fica vermelho. Nenhuma implementacao fake e
 * usada para fazer os testes passar.
 *
 * Estruturado segundo AAA (Arrange, Act, Assert) e principios F.I.R.S.T.
 * Regressao central: INVARIANCIA DE PARTICIONAMENTO — o resultado nao depende
 * de como o fluxo remoto chega fatiado em chunks.
 *
 * Build: make test_ssh_echo_guard -> ver Makefile (so g++/make).
 */
#include "features/shell/cyberdeck_ssh_echo_guard.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

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

/* Avalia `actual`/`expected` UMA unica vez: as chamadas de teste (feed_ok,
 * flush_ok, ...) mutam o estado do guard, entao nao podem ser reavaliadas na
 * mensagem de falha. */
#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        ++s_checks;                                                                    \
        const auto _cg_actual = (actual);                                              \
        const auto _cg_expected = (expected);                                          \
        if (_cg_actual != _cg_expected) {                                              \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  expected: '%s' actual: '%s'\n",                   \
                        __FILE__, __LINE__, escape_string(_cg_expected).c_str(),       \
                        escape_string(_cg_actual).c_str());                            \
        }                                                                              \
    } while (0)

/* ------------------------------------------------------------------ drivers */

/* Feed com buffer folgado: pelo contrato, feed() escreve no maximo
 * len + payload.size() (prefixo retido liberado na divergencia), logo
 * len + 4096 elimina qualquer truncamento nos casos de teste. */
std::string feed_ok(cyberdeck_ssh_echo_guard &g, const std::string &in)
{
    std::string out(in.size() + 4096, '\0');
    const size_t n = g.feed(in.data(), in.size(), &out[0], out.size());
    out.resize(n);
    return out;
}

/* Feed com controle explicito de capacidade (exercita truncamento e out
 * nullptr/out_cap 0). Devolve os bytes efetivamente escritos. */
std::string feed_cap(cyberdeck_ssh_echo_guard &g, const std::string &in, size_t cap,
                     size_t *written = nullptr)
{
    std::vector<char> buf(cap ? cap : 1);
    const size_t n = g.feed(in.data(), in.size(), cap ? buf.data() : nullptr, cap);
    if (written) {
        *written = n;
    }
    return std::string(buf.data(), n);
}

std::string flush_ok(cyberdeck_ssh_echo_guard &g)
{
    std::string out(4096, '\0');
    const size_t n = g.flush(&out[0], out.size());
    out.resize(n);
    return out;
}

/* Arma + feed de cada chunk + flush; concatena a saida exibida. */
std::string run(const std::string &payload, std::initializer_list<std::string> chunks,
                bool do_flush = true)
{
    cyberdeck_ssh_echo_guard g;
    const bool armed = g.arm(payload.data(), payload.size());
    CHECK(armed == true);
    std::string out;
    for (const std::string &c : chunks) {
        out += feed_ok(g, c);
    }
    if (do_flush) {
        out += flush_ok(g);
    }
    return out;
}

/* Particiona `stream` em chunks de `step` bytes, com o mesmo payload armado.
 * Quando expect_unarmed_before_flush for true, valida que o eco concluiu/divergiu
 * e o guard ja estava em IDLE antes do flush. */
std::string partitioned(const std::string &payload, const std::string &stream, size_t step,
                        bool expect_unarmed_before_flush = false)
{
    cyberdeck_ssh_echo_guard g;
    const bool armed = g.arm(payload.data(), payload.size());
    CHECK(armed == true);
    std::string out;
    for (size_t i = 0; i < stream.size(); i += step) {
        out += feed_ok(g, stream.substr(i, step));
    }
    if (expect_unarmed_before_flush) {
        CHECK(g.armed() == false);
    }
    const std::string flushed = flush_ok(g);
    if (expect_unarmed_before_flush) {
        CHECK_EQ(flushed, "");
    }
    out += flushed;
    return out;
}

/* ------------------------------------------------------------------ testes */

/* Smoke de integracao: eco + resposta em chunks na mesma sequencia de
 * chamadas, exercitando o driver `run`. */
void test_echo_then_reply_sequence()
{
    CHECK_EQ(run("cmd\n", {"cmd\n", "reply\n"}), "reply\n");
    CHECK_EQ(run("cmd\n", {"cm", "d\n", "reply\n"}), "reply\n");
    CHECK_EQ(run("cmd\n", {"nope\n"}), "nope\n");
    CHECK_EQ(run("cmd\n", {"cmd"}, false), ""); /* retido ate o flush */
    CHECK_EQ(run("cmd\n", {"cmd"}), "cmd");     /* flush preserva o parcial */
}

/* IDLE (nao armado): pass-through opaco byte a byte. */
void test_passthrough_when_idle()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.armed() == false);
    CHECK_EQ(feed_ok(g, "hello world"), "hello world");
    CHECK_EQ(feed_ok(g, "line1\nline2\n"), "line1\nline2\n");
    CHECK_EQ(feed_ok(g, "caf\xC3\xA9 \xF0\x9F\x94\xA5\n"), "caf\xC3\xA9 \xF0\x9F\x94\xA5\n");
    CHECK_EQ(flush_ok(g), "");
}

/* Contrato de arm(): payload valido arma; entradas invalidas sao recusadas
 * sem alterar o estado. */
void test_arm_contract()
{
    // Arrange
    cyberdeck_ssh_echo_guard g;

    // Assert: estado inicial deve ser IDLE
    CHECK(g.armed() == false);

    // Act & Assert: entradas invalidas em IDLE devem ser recusadas sem armar
    CHECK(g.arm("", 0) == false);
    CHECK(g.armed() == false);

    CHECK(g.arm("cmd\n", 0) == false);
    CHECK(g.armed() == false);

    CHECK(g.arm(nullptr, 0) == false);
    CHECK(g.armed() == false);

    CHECK(g.arm(nullptr, 4) == false);
    CHECK(g.armed() == false);

    // Act: payload valido arma o guard
    const bool armed_ok = g.arm("cmd\n", 4);

    // Assert
    CHECK(armed_ok == true);
    CHECK(g.armed() == true);

    // Act & Assert: rearmar com deteccao pendente e recusado (nunca descarta saida)
    CHECK(g.arm("other\n", 6) == false);
    CHECK(g.armed() == true);

    // Act & Assert: entradas invalidas enquanto armado tambem sao recusadas sem alterar o payload
    CHECK(g.arm(nullptr, 3) == false);
    CHECK(g.arm(nullptr, 0) == false);
    CHECK(g.arm("x", 0) == false);
    CHECK(g.armed() == true);

    // Assert: o payload ORIGINAL ("cmd\n") continua ativo e casa perfeitamente
    CHECK_EQ(feed_ok(g, "cmd\n"), "");
    CHECK(g.armed() == false);

    // Act & Assert: em IDLE apos consumo, armar de novo funciona
    CHECK(g.arm("cmd\n", 4) == true);
    CHECK(g.armed() == true);
    CHECK_EQ(flush_ok(g), "");
    CHECK(g.armed() == false);
}

/* Rearme recusado enquanto ha retencao ativa (m_matched > 0): a retencao
 * deve permanecer integralmente preservada sem descarte silencioso. */
void test_arm_rejection_with_active_retention()
{
    // Arrange
    cyberdeck_ssh_echo_guard g;
    const bool armed = g.arm("longcmd\n", 8);
    CHECK(armed == true);

    // Act: alimentar prefixo parcial ("long")
    const std::string out_feed = feed_ok(g, "long");

    // Assert: prefixo retido, guard ainda armado
    CHECK_EQ(out_feed, "");
    CHECK(g.armed() == true);

    // Act: tentar rearmar com novo comando enquanto ha retencao ativa
    const bool rearm = g.arm("other\n", 6);

    // Assert: rearme DEVE ser recusado e a retencao anterior mantida intacta
    CHECK(rearm == false);
    CHECK(g.armed() == true);

    // Act: chamar flush()
    const std::string flushed = flush_ok(g);

    // Assert: o prefixo retido original ("long") e devolvido sem perda
    CHECK_EQ(flushed, "long");
    CHECK(g.armed() == false);
}

/* Eco exato (um unico feed) e suprimido; a saida posterior passa. */
void test_exact_echo_suppressed()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(g, "cmd\n"), "");
    CHECK(g.armed() == false);
    CHECK_EQ(feed_ok(g, "result line\n"), "result line\n");

    /* Eco exato + saida no MESMO chunk unico. */
    cyberdeck_ssh_echo_guard g_combo;
    CHECK(g_combo.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(g_combo, "cmd\ncombo line\n"), "combo line\n");
    CHECK(g_combo.armed() == false);

    /* Payload maior (comando SSH tipico) tambem casado integralmente. */
    cyberdeck_ssh_echo_guard h;
    const std::string p = "ssh user@host\n";
    CHECK(h.arm(p.data(), p.size()) == true);
    CHECK_EQ(feed_ok(h, p), "");
    CHECK_EQ(feed_ok(h, "Welcome to Ubuntu\n"), "Welcome to Ubuntu\n");
}

/* Eco fragmentado em 1, 2 ou varios bytes: suprimido sem vazar. */
void test_fragmented_echo_suppressed()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(g, "c"), "");
    CHECK(g.armed() == true);
    CHECK_EQ(feed_ok(g, "m"), "");
    CHECK_EQ(feed_ok(g, "d"), "");
    CHECK(g.armed() == true);
    CHECK_EQ(feed_ok(g, "\n"), "");
    CHECK(g.armed() == false);
    CHECK_EQ(feed_ok(g, "ok"), "ok");

    /* Dois chunks. */
    cyberdeck_ssh_echo_guard h;
    CHECK(h.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(h, "cm"), "");
    CHECK_EQ(feed_ok(h, "d\n"), "");

    /* Um byte por chunk + saida no mesmo fluxo. */
    cyberdeck_ssh_echo_guard i;
    CHECK(i.arm("ls\n", 3) == true);
    CHECK_EQ(feed_ok(i, "l"), "");
    CHECK_EQ(feed_ok(i, "s"), "");
    CHECK_EQ(feed_ok(i, "\n"), "");
    CHECK_EQ(feed_ok(i, "a b\n"), "a b\n");
}

/* Sem eco: a divergencia no primeiro byte preserva TODA a saida. */
void test_no_echo_preserves_output()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(g, "output\n"), "output\n");
    CHECK(g.armed() == false);
    CHECK_EQ(feed_ok(g, "second\n"), "second\n");
}

/* Divergencia APOS prefixo parcial: o prefixo retido e liberado junto. */
void test_divergence_after_partial_prefix()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(g, "cmX"), "cmX");
    CHECK(g.armed() == false);
    CHECK_EQ(feed_ok(g, "yz"), "yz");

    /* Prefixo parcial no fim do chunk; divergencia no chunk seguinte. */
    cyberdeck_ssh_echo_guard h;
    CHECK(h.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(h, "cm"), "");
    CHECK(h.armed() == true);
    CHECK_EQ(feed_ok(h, "X"), "cmX");
    CHECK_EQ(feed_ok(h, "tail"), "tail");

    /* Prefixo retido de 3 bytes + divergencia no newline. */
    cyberdeck_ssh_echo_guard i;
    CHECK(i.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(i, "cmd"), "");
    CHECK_EQ(feed_ok(i, "Xy"), "cmdXy");
    CHECK(i.armed() == false);
}

/* Saida legitima que POR ACASO comeca com o payload e entao diverge e
 * integralmente preservada. */
void test_output_prefix_matches_but_diverges()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("ls\n", 3) == true);
    CHECK_EQ(feed_ok(g, "ls: cannot access 'x'\n"), "ls: cannot access 'x'\n");
    CHECK(g.armed() == false);
}

/* Saida imediata: divergencia do primeiro byte e emitida na mesma chamada. */
void test_immediate_output_on_first_byte()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(g, "zzz111"), "zzz111");

    cyberdeck_ssh_echo_guard h;
    CHECK(h.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(h, "z"), "z");

    cyberdeck_ssh_echo_guard i;
    CHECK(i.arm("cmd\n", 4) == true);
    const std::string high = std::string("\xFF", 1);
    CHECK_EQ(feed_ok(i, high), high);
}

/* Multiplos comandos: arma/eco/saida em sequencia, com e sem eco. */
void test_multiple_commands()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("ls\n", 3) == true);
    CHECK_EQ(feed_ok(g, "ls\n"), "");
    CHECK_EQ(feed_ok(g, "a.txt\n"), "a.txt\n");

    CHECK(g.arm("pwd\n", 4) == true);
    CHECK_EQ(feed_ok(g, "pwd\n"), "");
    CHECK_EQ(feed_ok(g, "/home/user\n"), "/home/user\n");

    /* Linha vazia (payload "\n"). */
    CHECK(g.arm("\n", 1) == true);
    CHECK_EQ(feed_ok(g, "\n"), "");
    CHECK_EQ(feed_ok(g, "next prompt\n"), "next prompt\n");

    /* Primeiro comando sem eco, segundo com eco. */
    cyberdeck_ssh_echo_guard h;
    CHECK(h.arm("one\n", 4) == true);
    CHECK_EQ(feed_ok(h, "no echo output\n"), "no echo output\n");
    CHECK(h.arm("two\n", 4) == true);
    CHECK_EQ(feed_ok(h, "two\n"), "");
    CHECK_EQ(feed_ok(h, "done\n"), "done\n");
}

/* Linha vazia: o Enter remoto (LF) e suprimido; sem eco, a saida passa. */
void test_empty_line()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("\n", 1) == true);
    CHECK_EQ(feed_ok(g, "\n"), "");
    CHECK(g.armed() == false);
    CHECK_EQ(feed_ok(g, "prompt$ "), "prompt$ ");

    cyberdeck_ssh_echo_guard h;
    CHECK(h.arm("\n", 1) == true);
    CHECK_EQ(feed_ok(h, "output\n"), "output\n");
}

/* UTF-8: eco fragmentado no meio de um codepoint continua suprimido. */
void test_utf8_echo_fragmented()
{
    const std::string p = "echo caf\xC3\xA9\n"; /* 'e' acentuado = C3 A9 */
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm(p.data(), p.size()) == true);
    CHECK_EQ(feed_ok(g, "echo caf"), "");
    CHECK_EQ(feed_ok(g, "\xC3"), "");
    CHECK(g.armed() == true);
    CHECK_EQ(feed_ok(g, "\xA9"), "");
    CHECK(g.armed() == true);
    CHECK_EQ(feed_ok(g, "\n"), "");
    CHECK(g.armed() == false);
    CHECK_EQ(feed_ok(g, "\xC3\xA9 ok\n"), "\xC3\xA9 ok\n");

    cyberdeck_ssh_echo_guard h;
    CHECK(h.arm(p.data(), p.size()) == true);
    CHECK_EQ(feed_ok(h, p), "");
}

/* UTF-8: divergencia no meio de um codepoint preserva todos os bytes. */
void test_utf8_divergence_preserves_bytes()
{
    const std::string p = "echo caf\xC3\xA9\n";
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm(p.data(), p.size()) == true);
    /* Casa "echo caf" + lead 0xC3; diverge em 0xA8 (vs 0xA9). */
    const std::string in = "echo caf\xC3\xA8!";
    CHECK_EQ(feed_ok(g, in), in);
    CHECK(g.armed() == false);

    cyberdeck_ssh_echo_guard h;
    CHECK(h.arm(p.data(), p.size()) == true);
    CHECK_EQ(feed_ok(h, "nada\n"), "nada\n");
}

/* flush(): prefixo parcial retido e preservado; guard volta a IDLE e reusa. */
void test_flush_partial_prefix_preserved()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(g, "cmd"), "");
    CHECK(g.armed() == true);
    CHECK_EQ(flush_ok(g), "cmd");
    CHECK(g.armed() == false);
    CHECK_EQ(feed_ok(g, "next"), "next");
    CHECK_EQ(flush_ok(g), "");

    cyberdeck_ssh_echo_guard h;
    CHECK(h.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(h, "c"), "");
    CHECK_EQ(flush_ok(h), "c");
    CHECK(h.armed() == false);

    cyberdeck_ssh_echo_guard i;
    CHECK(i.arm("cmd\n", 4) == true);
    CHECK_EQ(flush_ok(i), ""); /* nada retido */
    CHECK(i.armed() == false);

    cyberdeck_ssh_echo_guard j;
    CHECK_EQ(flush_ok(j), ""); /* guard novo */
    CHECK(j.armed() == false);
}

/* flush() e idempotente: chamadas consecutivas em IDLE retornam 0 sem alterar o estado. */
void test_flush_idempotence()
{
    // Arrange
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("test\n", 5) == true);
    CHECK_EQ(feed_ok(g, "te"), "");

    // Act & Assert 1: primeiro flush emite "te" e transita para IDLE
    CHECK_EQ(flush_ok(g), "te");
    CHECK(g.armed() == false);

    // Act & Assert 2: segundo flush imediato nao emite nada e segue em IDLE
    CHECK_EQ(flush_ok(g), "");
    CHECK(g.armed() == false);

    // Act & Assert 3: flush com buffer nulo em IDLE retorna 0
    CHECK(g.flush(nullptr, 0) == 0);
    CHECK(g.armed() == false);
}

/* flush() apos match completo do eco e no-op. */
void test_flush_after_full_match_noop()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(g, "cmd\n"), "");
    CHECK_EQ(flush_ok(g), "");
    CHECK(g.armed() == false);
    CHECK_EQ(feed_ok(g, "after\n"), "after\n");
}

/* flush(out == nullptr/out_cap == 0) descarta a retencao. */
void test_flush_zero_capacity_discards()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(g, "cmd"), "");
    CHECK(g.flush(nullptr, 0) == 0);
    CHECK(g.armed() == false);
    CHECK_EQ(feed_ok(g, "ok"), "ok"); /* "cmd" retido foi descartado pelo flush */
}

/* flush() com out_cap menor que a retencao trunca e reseta o estado. */
void test_flush_capacity_truncates()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("abcdef\n", 7) == true);
    CHECK_EQ(feed_ok(g, "abc"), "");
    std::vector<char> buf(2);
    const size_t n = g.flush(buf.data(), 2);
    CHECK(n == 2);
    CHECK_EQ(std::string(buf.data(), n), "ab");
    CHECK(g.armed() == false);
    CHECK_EQ(feed_ok(g, "z"), "z");
}

/* Casos extremos de capacidade de saida na divergencia: cap 0, cap 1 e truncamento no stream subsequente. */
void test_divergence_capacity_edge_cases()
{
    // 1) Divergencia com out_cap == 0 (descarta retencao e chunk, mas desarma sem crash)
    {
        cyberdeck_ssh_echo_guard g;
        CHECK(g.arm("prefix\n", 7) == true);
        CHECK_EQ(feed_ok(g, "pref"), "");
        CHECK(g.armed() == true);

        size_t written = 999;
        const std::string out = feed_cap(g, "iXtail", 0, &written);
        CHECK_EQ(out, "");
        CHECK(written == 0);
        CHECK(g.armed() == false);

        // Apos o descarte, guard esta IDLE e passa bytes normalmente
        CHECK_EQ(feed_ok(g, "next"), "next");
    }

    // 2) Divergencia com out_cap == 1 (trunca no meio do prefixo retido)
    {
        cyberdeck_ssh_echo_guard g;
        CHECK(g.arm("prefix\n", 7) == true);
        CHECK_EQ(feed_ok(g, "pref"), "");
        CHECK(g.armed() == true);

        size_t written = 0;
        const std::string out = feed_cap(g, "X", 1, &written);
        // "pref" (4) + 'X' (1) = 5 bytes gerados; cap 1 emite apenas 'p'
        CHECK_EQ(out, "p");
        CHECK(written == 1);
        CHECK(g.armed() == false);
    }

    // 3) Divergencia onde out_cap trunca durante a emissao dos bytes posteriores ao divergente
    {
        cyberdeck_ssh_echo_guard g;
        CHECK(g.arm("ab\n", 3) == true);
        CHECK_EQ(feed_ok(g, "a"), "");
        CHECK(g.armed() == true);

        // 'a' retido (1) + divergente 'X' (1) + cauda "12345" (5) = 7 bytes gerados
        size_t written = 0;
        const std::string out = feed_cap(g, "X12345", 4, &written);
        CHECK_EQ(out, "aX12");
        CHECK(written == 4);
        CHECK(g.armed() == false);
    }
}

/* Capacidade de feed(): truncamento e out nullptr/0 nao corrompem o estado. */
void test_output_capacity_truncation()
{
    size_t w = 0;

    cyberdeck_ssh_echo_guard g; /* IDLE */
    CHECK_EQ(feed_cap(g, "abcdef", 6, &w), "abcdef");
    CHECK(w == 6);
    CHECK_EQ(feed_cap(g, "ghijkl", 4, &w), "ghij");
    CHECK(w == 4);
    CHECK_EQ(feed_cap(g, "zz", 0, &w), "");
    CHECK(w == 0);
    CHECK_EQ(feed_ok(g, "qq"), "qq"); /* "zz" consumido e descartado */

    /* Armado: divergencia com prefixo retido maior que o cap. */
    cyberdeck_ssh_echo_guard h;
    CHECK(h.arm("abc\n", 4) == true);
    CHECK_EQ(feed_cap(h, "abX", 2, &w), "ab");
    CHECK(w == 2);
    CHECK(h.armed() == false);
    CHECK_EQ(feed_ok(h, "Z"), "Z");

    /* Cap suficiente: prefixo retido + byte divergente. */
    cyberdeck_ssh_echo_guard i;
    CHECK(i.arm("abc\n", 4) == true);
    CHECK_EQ(feed_cap(i, "abX", 7, &w), "abX");
    CHECK(w == 3);
    CHECK(i.armed() == false);
}

/* Entrada JA filtrada e tratada como bytes opacos; bytes de controle
 * inesperados nao sao interpretados (o contrato nao faz parsing de ANSI). */
void test_prefiltered_stream_opaque()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("cmd\n", 4) == true);
    CHECK_EQ(feed_ok(g, "cmd\n"), "");
    CHECK_EQ(feed_ok(g, "[OK] caf\xC3\xA9 \xE2\x82\xAC 100%\n"),
             "[OK] caf\xC3\xA9 \xE2\x82\xAC 100%\n");

    cyberdeck_ssh_echo_guard h;
    CHECK(h.arm("x\n", 2) == true);
    CHECK_EQ(feed_ok(h, "x\n"), "");
    const std::string esc = std::string("\x1B[31m", 5);
    CHECK_EQ(feed_ok(h, esc), esc);
}

/* Entradas nulas/vazias: no-op sem alterar o estado. */
void test_null_and_empty_inputs()
{
    cyberdeck_ssh_echo_guard g; /* IDLE */
    CHECK(g.feed(nullptr, 0, nullptr, 0) == 0);
    CHECK(g.feed("", 0, nullptr, 0) == 0);
    CHECK(g.feed(nullptr, 5, nullptr, 0) == 0);
    CHECK_EQ(feed_ok(g, "ab"), "ab");
    CHECK(g.flush(nullptr, 0) == 0);
    CHECK(g.arm(nullptr, 0) == false);
    CHECK(g.armed() == false);

    /* Armado + feed vazio: estado intacto. */
    CHECK(g.arm("cmd\n", 4) == true);
    CHECK(g.feed(nullptr, 0, nullptr, 0) == 0);
    CHECK(g.armed() == true);
    CHECK(g.feed("", 0, nullptr, 0) == 0);
    CHECK(g.armed() == true);
    CHECK_EQ(feed_ok(g, "cmd\n"), "");
}

/* Feed nulo ou vazio enquanto ha retencao ativa (m_matched > 0): nao deve resetar m_matched nem corromper a deteccao. */
void test_empty_feed_with_active_retention()
{
    // Arrange
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("cmd\n", 4) == true);

    // Act: reter 2 bytes ("cm")
    CHECK_EQ(feed_ok(g, "cm"), "");
    CHECK(g.armed() == true);

    // Act: feeds vazios/nulos nao devem resetar m_matched nem desarmar
    char dummy[16];
    CHECK(g.feed(nullptr, 0, dummy, sizeof(dummy)) == 0);
    CHECK(g.armed() == true);
    CHECK(g.feed("", 0, dummy, sizeof(dummy)) == 0);
    CHECK(g.armed() == true);
    CHECK(g.feed(nullptr, 5, dummy, sizeof(dummy)) == 0);
    CHECK(g.armed() == true);

    // Act: completar o eco com os bytes restantes ("d\n")
    const std::string rest = feed_ok(g, "d\n");

    // Assert: eco concluido e suprimido integralmente
    CHECK_EQ(rest, "");
    CHECK(g.armed() == false);
}

/* UTF-8 avancado: codepoints de 3 bytes (Euro) e 4 bytes (Emoji fogo) fatiados byte a byte e divergencia no meio. */
void test_utf8_multibyte_3_and_4_bytes()
{
    // 1) 3-byte UTF-8: Euro '€' (\xE2\x82\xAC) fragmentado byte a byte
    {
        const std::string p = "preco \xE2\x82\xAC 10\n";
        cyberdeck_ssh_echo_guard g;
        CHECK(g.arm(p.data(), p.size()) == true);

        CHECK_EQ(feed_ok(g, "preco "), "");
        CHECK_EQ(feed_ok(g, "\xE2"), "");
        CHECK(g.armed() == true);
        CHECK_EQ(feed_ok(g, "\x82"), "");
        CHECK(g.armed() == true);
        CHECK_EQ(feed_ok(g, "\xAC"), "");
        CHECK(g.armed() == true);
        CHECK_EQ(feed_ok(g, " 10\n"), "");
        CHECK(g.armed() == false);

        // Saida posterior liberada
        CHECK_EQ(feed_ok(g, "ok\n"), "ok\n");
    }

    // 2) 4-byte UTF-8: Emoji Fogo '🔥' (\xF0\x9F\x94\xA5) fragmentado byte a byte
    {
        const std::string p = "fire \xF0\x9F\x94\xA5\n";
        cyberdeck_ssh_echo_guard g;
        CHECK(g.arm(p.data(), p.size()) == true);

        CHECK_EQ(feed_ok(g, "fire "), "");
        CHECK_EQ(feed_ok(g, "\xF0"), "");
        CHECK(g.armed() == true);
        CHECK_EQ(feed_ok(g, "\x9F"), "");
        CHECK(g.armed() == true);
        CHECK_EQ(feed_ok(g, "\x94"), "");
        CHECK(g.armed() == true);
        CHECK_EQ(feed_ok(g, "\xA5"), "");
        CHECK(g.armed() == true);
        CHECK_EQ(feed_ok(g, "\n"), "");
        CHECK(g.armed() == false);

        CHECK_EQ(feed_ok(g, "\xF0\x9F\x94\xA5 flame\n"), "\xF0\x9F\x94\xA5 flame\n");
    }

    // 3) Divergencia no 3º byte de um codepoint de 4 bytes
    {
        const std::string p = "\xF0\x9F\x94\xA5\n"; // 🔥
        cyberdeck_ssh_echo_guard g;
        CHECK(g.arm(p.data(), p.size()) == true);

        // Stream envia \xF0, \x9F e depois diverge com \x90 (em vez de \x94)
        const std::string in = "\xF0\x9F\x90\x80"; // 🐀 (\xF0\x9F\x90\x80)
        CHECK_EQ(feed_ok(g, in), in);
        CHECK(g.armed() == false);
    }
}

/* Determinismo: a mesma sequencia de chamadas produz os mesmos bytes. */
void test_deterministic()
{
    const auto run_once = []() {
        cyberdeck_ssh_echo_guard g;
        std::string out;
        g.arm("cmd\n", 4);
        out += feed_ok(g, "cm");
        out += feed_ok(g, "d");
        out += feed_ok(g, "\n");
        out += feed_ok(g, "out\n");
        out += flush_ok(g);
        return out;
    };
    CHECK_EQ(run_once(), run_once());
    CHECK_EQ(run_once(), "out\n");
}

/* Guard reutilizavel apos flush parcial e apos match. */
void test_guard_reusable_after_flush()
{
    cyberdeck_ssh_echo_guard g;
    CHECK(g.arm("one\n", 4) == true);
    CHECK_EQ(feed_ok(g, "one\n"), "");
    CHECK_EQ(flush_ok(g), "");
    CHECK(g.arm("two\n", 4) == true);
    CHECK_EQ(feed_ok(g, "two\n"), "");
    CHECK_EQ(feed_ok(g, "ok"), "ok");

    cyberdeck_ssh_echo_guard h;
    CHECK(h.arm("abc\n", 4) == true);
    CHECK_EQ(feed_ok(h, "ab"), "");
    CHECK_EQ(flush_ok(h), "ab");
    CHECK(h.arm("xyz\n", 4) == true);
    CHECK_EQ(feed_ok(h, "xyz\n"), "");
    CHECK(h.armed() == false);
}

/* REGRESSAO: o resultado exibido NAO depende do fatiamento do fluxo remoto.
 * Para cada caso, um unico feed e qualquer particao (1..7 bytes) produzem a
 * mesma sequencia exata. */
void test_partition_invariance()
{
    struct Case {
        std::string payload;
        std::string stream;
        std::string expected;
        bool expect_unarmed_before_flush;
    };
    const Case cases[] = {
        {"cmd\n", "cmd\nresult 1\nresult 2\n", "result 1\nresult 2\n", true},
        {"cmd\n", "no echo here\n", "no echo here\n", true},
        {"cmd\n", "cmdXtail\n", "cmdXtail\n", true},
        {"cmd\n", "cmd", "cmd", false}, /* eco incompleto: flush preserva */
        {"ls\n", "ls: cannot access 'x'\n", "ls: cannot access 'x'\n", true},
        {"echo caf\xC3\xA9\n", "echo caf\xC3\xA9\ndone\n", "done\n", true},
        {"\n", "\nprompt$ ", "prompt$ ", true},
        {"abc\n", "ab", "ab", false}, /* divergencia implicita no flush */
        {"preco \xE2\x82\xAC 10\n", "preco \xE2\x82\xAC 10\nsaldo ok\n", "saldo ok\n", true}, /* UTF-8 3-byte */
        {"fire \xF0\x9F\x94\xA5\n", "fire \xF0\x9F\x94\xA5\nburn\n", "burn\n", true}, /* UTF-8 4-byte */
    };
    for (const Case &c : cases) {
        CHECK_EQ(partitioned(c.payload, c.stream, c.stream.size(), c.expect_unarmed_before_flush), c.expected);
        const size_t steps[] = {1, 2, 3, 5, 7};
        for (size_t step : steps) {
            CHECK_EQ(partitioned(c.payload, c.stream, step, c.expect_unarmed_before_flush), c.expected);
        }
    }
}

} // namespace

int main()
{
    test_echo_then_reply_sequence();
    test_passthrough_when_idle();
    test_arm_contract();
    test_arm_rejection_with_active_retention();
    test_exact_echo_suppressed();
    test_fragmented_echo_suppressed();
    test_no_echo_preserves_output();
    test_divergence_after_partial_prefix();
    test_output_prefix_matches_but_diverges();
    test_immediate_output_on_first_byte();
    test_multiple_commands();
    test_empty_line();
    test_utf8_echo_fragmented();
    test_utf8_divergence_preserves_bytes();
    test_utf8_multibyte_3_and_4_bytes();
    test_flush_partial_prefix_preserved();
    test_flush_idempotence();
    test_flush_after_full_match_noop();
    test_flush_zero_capacity_discards();
    test_flush_capacity_truncates();
    test_divergence_capacity_edge_cases();
    test_output_capacity_truncation();
    test_prefiltered_stream_opaque();
    test_null_and_empty_inputs();
    test_empty_feed_with_active_retention();
    test_deterministic();
    test_guard_reusable_after_flush();
    test_partition_invariance();

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_ssh_echo_guard (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
