/*
 * Testes unitarios host-side para o filtro puro e incremental da saida SSH
 * (contrato em cyberdeck_terminal_filter.h, implementacao futura em
 * components/cyberdeck/src/features/shell/cyberdeck_terminal_filter.cpp).
 *
 * Plano aprovado (recorte sob teste):
 *   - unidade pura e incremental para limpar o fluxo bruto do canal SSH
 *     (stdout/stderr) antes da exibicao no terminal TUI;
 *   - remover ANSI/CSI (ESC [ ... final) e OSC (ESC ] ... ST|BEL);
 *   - remover escapes curtos (ESC c, ESC 7, ESC ( B, ESC # 8, ...);
 *   - normalizar quebras de linha: CRLF e CR isolado viram LF;
 *   - preservar UTF-8/texto byte a byte (sem re-encode nem validacao);
 *   - suportar fragmentacao: chunks de qualquer tamanho, sequencias cortadas
 *     retomadas nas chamadas seguintes, CR no fim do chunk aguardando
 *     lookahead;
 *   - C0 (exceto \r, \n, \t) e DEL removidos; \t preservado.
 *
 * Os testes fixam o contrato ANTES da implementacao (TDD, mesmo padrao de
 * test_wifi_indicator): enquanto cyberdeck_terminal_filter.cpp nao existir,
 * `make test_terminal_filter` falha por modulo ausente ("No rule to make
 * target"). Nenhuma implementacao fake e usada para fazer os testes passar.
 *
 * Estruturado segundo o padrao AAA (Arrange, Act, Assert) e principios
 * F.I.R.S.T. Propriedade de regressao central: INVARIANCIA DE PARTICIONAMENTO
 * (qualquer divisao em chunks do mesmo fluxo produz o mesmo texto final).
 *
 * Build: make test_terminal_filter (veja Makefile; so g++/make).
 */
#include "features/shell/cyberdeck_terminal_filter.h"

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

/* ------------------------------------------------------------------ drivers */

/* Feed com buffer de saida len + 1. Pelo contrato, uma chamada de feed()
 * escreve no maximo len + 1 bytes (o +1 e o LF de um '\r' pendente do chunk
 * anterior resolvido pelo primeiro byte do chunk); portanto com
 * out_cap >= len + 1 a saida e sempre completa (sem truncamento). */
std::string feed_ok(cyberdeck_terminal_filter &f, const std::string &in)
{
    if (in.empty()) {
        return std::string();
    }
    std::string out(in.size() + 1, '\0');
    const size_t n = f.feed(in.data(), in.size(), &out[0], out.size());
    out.resize(n);
    return out;
}

/* Feed com controle explicito de capacidade: exercita truncamento e
 * out == nullptr/out_cap == 0. Opcionalmente devolve o retorno de feed(). */
std::string feed_cap(cyberdeck_terminal_filter &f, const std::string &in, size_t cap,
                     size_t *written = nullptr)
{
    std::vector<char> buf(cap ? cap : 1);
    const size_t n = f.feed(in.data(), in.size(), cap ? buf.data() : nullptr, cap);
    if (written) {
        *written = n;
    }
    return std::string(buf.data(), n);
}

std::string flush_ok(cyberdeck_terminal_filter &f)
{
    char buf[16]; /* flush emite no maximo 1 byte ('\n' do CR pendente) */
    const size_t n = f.flush(buf, sizeof(buf));
    return std::string(buf, n);
}

/* Filtro novo + feed de cada chunk + flush opcional (concatena tudo). */
std::string transform(std::initializer_list<std::string> chunks, bool do_flush = true)
{
    cyberdeck_terminal_filter f;
    std::string out;
    for (const std::string &c : chunks) {
        out += feed_ok(f, c);
    }
    if (do_flush) {
        out += flush_ok(f);
    }
    return out;
}

/* Particiona `s` em chunks de `step` bytes e processa em um filtro novo. */
std::string partitioned(const std::string &s, size_t step)
{
    cyberdeck_terminal_filter f;
    std::string out;
    for (size_t i = 0; i < s.size(); i += step) {
        out += feed_ok(f, s.substr(i, step));
    }
    out += flush_ok(f);
    return out;
}

/* ------------------------------------------------------------------ testes */

void test_plain_text_passthrough()
{
    // Arrange & Act & Assert: texto ASCII simples passa intacto.
    CHECK_EQ(transform({"hello world"}), "hello world");
    CHECK_EQ(transform({"123 456 !@#$%^&*()_+-=[]{}|;:'."}), "123 456 !@#$%^&*()_+-=[]{}|;:'.");
    CHECK_EQ(transform({"line1\nline2\nline3"}), "line1\nline2\nline3");
    CHECK_EQ(transform({""}), "");
    /* Multiplos chunks continuos sem pendencia: concatenacao identica. */
    CHECK_EQ(transform({"ab", "cd", "ef"}), "abcdef");
}

void test_tab_and_lone_lf_preserved()
{
    // \t e \n (sem \r) sao texto preservado, nao controle removido.
    CHECK_EQ(transform({"a\tb"}), "a\tb");
    CHECK_EQ(transform({"\t"}), "\t");
    CHECK_EQ(transform({"a\nb"}), "a\nb");
    CHECK_EQ(transform({"a\n\nb"}), "a\n\nb");
}

void test_csi_sequences_removed()
{
    // Cores/SGR.
    CHECK_EQ(transform({"\x1B[31mred\x1B[0m"}), "red");
    CHECK_EQ(transform({"\x1B[1;32;44mbold\x1B[0m"}), "bold");
    CHECK_EQ(transform({"\x1B[38;5;196m256color\x1B[0m"}), "256color");
    // Apagar tela/linha e cursor.
    CHECK_EQ(transform({"\x1B[2J\x1B[Hwelcome"}), "welcome");
    CHECK_EQ(transform({"a\x1B[2Kb"}), "ab");
    // Modos privados (?).
    CHECK_EQ(transform({"\x1B[?25lhide\x1B[?25h"}), "hide");
    // CSI com intermediate (espaco) antes do final.
    CHECK_EQ(transform({"x\x1B[ 3my"}), "xy");
    // CSI sem parametros.
    CHECK_EQ(transform({"\x1B[mplain"}), "plain");
    // CSI com parametros estendidos (modificadores >).
    CHECK_EQ(transform({"\x1B[>4;2m"}), "");
}

void test_osc_bel_terminated()
{
    // OSC terminado por BEL (0x07): removido por inteiro, inclusive o BEL.
    CHECK_EQ(transform({"\x1B]0;titulo\x07"}), "");
    CHECK_EQ(transform({"\x1B]2;user@host\x07"}), "");
    CHECK_EQ(transform({"\x1B]0;titulo\x07text\x1B]0;\x07"}), "text");
    // Payload OSC com UTF-8: consumido como payload, nada vaza.
    CHECK_EQ(transform({"\x1B]8;;http://ex.com/\x07link\x1B]8;;\x07"}), "link");
    // OSC + BEL seguido de texto no MESMO chunk.
    CHECK_EQ(transform({"a\x1B]0;t\x07" "b"}), "ab");
}

void test_osc_st_terminated()
{
    // OSC terminado por ST (ESC \, dois bytes): removido por inteiro.
    CHECK_EQ(transform({"\x1B]0;titulo\x1B\\"}), "");
    CHECK_EQ(transform({"\x1B]2;path;arg\x1B\\"}), "");
    CHECK_EQ(transform({"a\x1B]0;x\x1B\\b"}), "ab");
    // Terminadores BEL e ST equivalentes no mesmo fluxo.
    CHECK_EQ(transform({"\x1B]0;t1\x07\x1B]0;t2\x1B\\"}), "");
}

void test_short_escapes_removed()
{
    // Escapes curtos de 2 bytes (ESC + final 0x30-0x7E).
    CHECK_EQ(transform({"\x1B" "7abc\x1B" "8"}), "abc");   /* save/restore cursor */
    CHECK_EQ(transform({"\x1B=keypad\x1B>"}), "keypad"); /* aplicacao/numerico */
    CHECK_EQ(transform({"\x1B" "creset"}), "reset");     /* reset */
    CHECK_EQ(transform({"\x1BM"}), "");                  /* reverse index */
    CHECK_EQ(transform({"\x1B" "D"}), "");               /* index */
    CHECK_EQ(transform({"\x1B" "E"}), "");               /* next line */
    CHECK_EQ(transform({"\x1BH"}), "");                  /* tab set */
    CHECK_EQ(transform({"\x1BZ"}), "");                  /* identify */
    /* 3 bytes: ESC + intermediate (0x20-0x2F) + final. */
    CHECK_EQ(transform({"\x1B(Bascii"}), "ascii");       /* G0 = ASCII */
    CHECK_EQ(transform({"\x1B)0x"}), "x");               /* G1 = DEC special */
    CHECK_EQ(transform({"\x1B#8fill"}), "fill");         /* double-height */
    CHECK_EQ(transform({"\x1B%Gutf8"}), "utf8");         /* UTF-8 designated */
    CHECK_EQ(transform({"\x1B$Bjis"}), "jis");           /* JIS designation */
    /* 4 bytes: dois intermediates + final. */
    CHECK_EQ(transform({"\x1B$(Ccjk"}), "cjk");          /* G0 = full-width */
    /* ESC + final engole APENAS os dois bytes: o texto seguinte sobrevive. */
    CHECK_EQ(transform({"a\x1Bzb"}), "ab");              /* ESC z: 2-char escape */
}

void test_c0_controls_removed()
{
    /* C0 (exceto \r, \n, \t) e DEL sao removidos onde estiverem. */
    const unsigned char controls[] = {0x00, 0x01, 0x02, 0x07, 0x08, 0x0B, 0x0C,
                                      0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
                                      0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1C,
                                      0x1D, 0x1E, 0x1F, 0x7F};
    for (unsigned char c : controls) {
        const std::string raw = std::string("a") + static_cast<char>(c) + "b";
        CHECK_EQ(transform({raw}), "ab");
    }
    /* BEL isolado (fora de OSC) nao toca o texto vizinho. */
    CHECK_EQ(transform({"a\x07" "b"}), "ab");
    /* NUL rodeado de texto. */
    CHECK_EQ(transform({std::string("a\x00", 2) + "b"}), "ab");
}

void test_crlf_normalization_complete()
{
    // CRLF -> um unico LF.
    CHECK_EQ(transform({"a\r\nb"}), "a\nb");
    CHECK_EQ(transform({"\r\n"}), "\n");
    // CR isolado -> LF.
    CHECK_EQ(transform({"a\rb"}), "a\nb");
    CHECK_EQ(transform({"a\r"}), "a\n"); /* flush emite o CR pendente */
    // LF isolado preservado.
    CHECK_EQ(transform({"a\nb"}), "a\nb");
    // Mistura completa.
    CHECK_EQ(transform({"x\r\ny\rz\n"}), "x\ny\nz\n");
    // CR CR LF: cada CR vira LF; o par CRLF colapsa.
    CHECK_EQ(transform({"a\r\r\nb"}), "a\n\nb");
    // CR seguido de UTF-8: LF antes dos bytes de texto.
    CHECK_EQ(transform({"x\r\xC3\xA9"}), "x\n\xC3\xA9");
}

void test_crlf_fragmented()
{
    // CR no fim do chunk + LF no inicio do proximo: CRLF colapsado.
    CHECK_EQ(transform({"a\r", "\nb"}), "a\nb");
    // CR no fim do chunk + byte nao-LF: CR vira LF antes do byte.
    CHECK_EQ(transform({"a\r", "b"}), "a\nb");
    // CR no fim do chunk + fim do fluxo: flush emite LF.
    CHECK_EQ(transform({"a\r"}, true), "a\n");
    CHECK_EQ(transform({"a\r"}, false), "a"); /* sem flush: fica pendente */
    // CR CR em chunks diferentes.
    CHECK_EQ(transform({"a\r", "\rb"}), "a\n\nb");
    // CR pendente + CRLF no chunk seguinte.
    CHECK_EQ(transform({"x\r", "\r\n"}), "x\n\n");
    // Chunk so com CR, flush, e uso continuado do filtro.
    cyberdeck_terminal_filter f;
    CHECK_EQ(feed_ok(f, "a\r"), "a");
    CHECK_EQ(flush_ok(f), "\n");
    CHECK_EQ(feed_ok(f, "b"), "b");
    CHECK_EQ(flush_ok(f), "");
}

void test_escape_fragmentation()
{
    // Sequencias de controle cortadas em qualquer ponto continuam removidas.
    CHECK_EQ(transform({"\x1B", "[31m"}), "");
    CHECK_EQ(transform({"\x1B[", "31m"}), "");
    CHECK_EQ(transform({"\x1B[3", "1m"}), "");
    CHECK_EQ(transform({"\x1B[31", "m"}), "");
    CHECK_EQ(transform({"\x1B", "[", "?25", "h"}), "");
    CHECK_EQ(transform({"\x1B(" , "B"}), "");            /* intermediate + final */
    CHECK_EQ(transform({"\x1B", "(", "B"}), "");         /* ESC sozinho no chunk */
    CHECK_EQ(transform({"\x1B", "$", "(", "C"}), "");    /* dois intermediates */
    /* ESC duplo (`ESC ESC`): o primeiro e abortado, o segundo reinicia. */
    CHECK_EQ(transform({"\x1B", "\x1B[31m"}), "");
    CHECK_EQ(transform({"\x1B\x1B[31m"}), "");
    /* Texto no meio continua passando. */
    CHECK_EQ(transform({"a\x1B", "[31mb"}), "ab");
    CHECK_EQ(transform({"a\x1B", "zb"}), "ab");
}

void test_osc_fragmentation()
{
    // OSC cortado em varios pontos, terminador BEL no ultimo chunk.
    CHECK_EQ(transform({"\x1B]", "0;hi", "\x07"}), "");
    CHECK_EQ(transform({"\x1B]0", ";hi", "\x07"}), "");
    // ST (ESC \) cortado entre dois chunks.
    CHECK_EQ(transform({"\x1B]0;x", "\x1B", "\\"}), "");
    // OSC longo com URL e conteudo.
    CHECK_EQ(transform({"\x1B]8;;h", "ttp://example.com/a?b=c", "\x07link", "\x1B]8;;", "\x07"}), "link");
    // OSC sem terminador ate o fim do fluxo: flush descarta.
    CHECK_EQ(transform({"\x1B]0;sem-terminador"}), "");
    CHECK_EQ(transform({"\x1B]0", ";sem", "-term"}), "");
    // OSC interrompido, depois texto: o texto apos o BEL sobrevive.
    CHECK_EQ(transform({"a\x1B]", "0;t", "\x07" "b"}), "ab");
}

void test_utf8_preserved()
{
    // UTF-8 completo (2, 3 e 4 bytes), byte a byte.
    CHECK_EQ(transform({"caf\xC3\xA9"}), "caf\xC3\xA9");
    CHECK_EQ(transform({"\xE6\x97\xA5\xE6\x9C\xAC"}), "\xE6\x97\xA5\xE6\x9C\xAC");
    CHECK_EQ(transform({"\xF0\x9F\x94\xA5"}), "\xF0\x9F\x94\xA5");
    CHECK_EQ(transform({"ol\xC3\xA1, \xE2\x82\xAC 5"}), "ol\xC3\xA1, \xE2\x82\xAC 5");
    // UTF-8 fragmentado entre chunks: intacto (sem divisao de codepoint).
    CHECK_EQ(transform({"caf", "\xC3", "\xA9"}), "caf\xC3\xA9");
    CHECK_EQ(transform({"\xE6", "\x97\xA5", "\xE6\x9C\xAC"}), "\xE6\x97\xA5\xE6\x9C\xAC");
    CHECK_EQ(transform({"\xF0", "\x9F", "\x94\xA5", "!"}), "\xF0\x9F\x94\xA5!");
    // UTF-8 no limite com sequencias de escape.
    CHECK_EQ(transform({"\xF0\x9F\x94\xA5\x1B[0m"}), "\xF0\x9F\x94\xA5");
    CHECK_EQ(transform({"\x1B[31m\xF0\x9F\x94\xA5\x1B[0m"}), "\xF0\x9F\x94\xA5");
    // Bytes invalidos/truncados: pass-through (transparencia, sem validacao).
    CHECK_EQ(transform({"x\x80y"}), "x\x80y");
    CHECK_EQ(transform({"\xC3", "x"}), "\xC3" "x");
    CHECK_EQ(transform({"\xC3"}), "\xC3"); /* lead truncado no fim: preservado */
}

void test_escape_abort_on_control_and_utf8()
{
    /* C0 dentro de sequencia CSI aborta a sequencia; o byte e reprocessado
     * em GROUND (aqui: \n vira LF do texto; o CSI parcial e descartado). */
    CHECK_EQ(transform({"\x1B[31\nm"}), "\nm");
    /* Byte alto (UTF-8) dentro de CSI aborta a sequencia e passa como texto. */
    CHECK_EQ(transform({"a\x1B[31\xE2\x82\xACm"}), "a\xE2\x82\xACm");
    /* ESC_INT abortado por byte alto: intermediate descartado, byte passado. */
    CHECK_EQ(transform({"a\x1B(\xE2\x82\xAC" "b"}), "a\xE2\x82\xAC" "b");
    /* ESC abortado por NUL: NUL removido no GROUND, ESC anterior descartado. */
    CHECK_EQ(transform({std::string("a\x1B\x00", 3) + "b"}), "ab");
    /* ESC interno ao OSC seguido de nao-'\\': descarta OSC e reprocessa. */
    CHECK_EQ(transform({"\x1B]0;x\x1Bzb"}), "zb");
}

void test_flush_pending_discards()
{
    // Sequencias de controle incompletas no fim do fluxo: flush descarta.
    CHECK_EQ(transform({"\x1B[31"}), "");
    CHECK_EQ(transform({"\x1B[31;4"}), "");
    CHECK_EQ(transform({"\x1B]0;tit"}), "");
    CHECK_EQ(transform({"\x1B("}), "");
    CHECK_EQ(transform({"\x1B"}), "");
    CHECK_EQ(transform({"ab\x1B"}), "ab");        /* texto antes do ESC intacto */
    CHECK_EQ(transform({"x\x1B[31"}), "x");       /* texto antes do CSI intacto */
    CHECK_EQ(transform({"ab\x1B[31m"}), "ab");    /* sequencia completa: removida */
    /* Flush repetido e inofensivo. */
    cyberdeck_terminal_filter f;
    CHECK_EQ(feed_ok(f, "\x1B[31"), "");
    CHECK_EQ(flush_ok(f), "");
    CHECK_EQ(flush_ok(f), "");
    CHECK_EQ(feed_ok(f, "ok"), "ok"); /* filtro segue utilizavel */
}

void test_output_capacity_truncation()
{
    size_t w = 0;
    cyberdeck_terminal_filter f;

    /* out_cap >= len: sem truncamento (texto longo). */
    CHECK_EQ(feed_cap(f, "abcdef", 6, &w), "abcdef");
    CHECK(w == 6);

    /* out_cap < len: trunca para caber; retorno == bytes escritos. */
    CHECK_EQ(feed_cap(f, "ghijkl", 4, &w), "ghij");
    CHECK(w == 4);

    /* Truncamento com escape: produzido e menor que a entrada. */
    CHECK_EQ(feed_cap(f, "\x1B[31mABCDEF", 3, &w), "ABC");
    CHECK(w == 3);

    /* out_cap == 0 / out == nullptr: consome e descarta, estado avanca. */
    CHECK_EQ(feed_cap(f, "zz", 0, &w), "");
    CHECK(w == 0);
    CHECK_EQ(feed_ok(f, "qq"), "qq"); /* "zz" consumido e descartado antes */

    /* Truncamento preserva o estado interno (CR pendente sobrevive). */
    CHECK_EQ(feed_cap(f, "abc\r", 2, &w), "ab"); /* 'c' nao coube, CR pendente */
    CHECK(w == 2);
    CHECK_EQ(flush_ok(f), "\n"); /* CR pendente vira LF no flush */

    /* CRLF colapsado com cap minima: 1 byte por entrada e suficiente. */
    cyberdeck_terminal_filter g;
    CHECK_EQ(feed_cap(g, "a\r\n", 1, &w), "a");
    CHECK(w == 1);
    CHECK_EQ(flush_ok(g), "");

    /* Capacidade zero ainda avanca CR pendente (via feed cap 0). */
    cyberdeck_terminal_filter h;
    CHECK_EQ(feed_cap(h, "\r", 0, &w), "");
    CHECK(w == 0);
    CHECK_EQ(feed_cap(h, "\nb", 1, &w), "\n"); /* CR pendente + LF -> um LF */
    CHECK(w == 1);
    CHECK_EQ(feed_ok(h, "c"), "c");
}

void test_null_and_empty_inputs()
{
    cyberdeck_terminal_filter f;

    /* data nullptr / vazio: no-op, retorna 0, nada muda. */
    CHECK(f.feed(nullptr, 0, nullptr, 0) == 0);
    CHECK(f.feed("", 0, nullptr, 0) == 0);
    CHECK(f.feed(nullptr, 5, nullptr, 0) == 0); /* defensivo, sem crash */
    CHECK_EQ(feed_ok(f, "ab"), "ab"); /* estado inalterado */

    /* flush em filtro novo: 0 bytes, segue utilizavel. */
    CHECK(cyberdeck_terminal_filter().flush(nullptr, 0) == 0);
    cyberdeck_terminal_filter g;
    CHECK(g.flush(nullptr, 0) == 0);
    CHECK_EQ(feed_ok(g, "x"), "x");

    /* data NULL nao pode desviar o fluxo normal. */
    cyberdeck_terminal_filter h;
    CHECK_EQ(feed_ok(h, "a"), "a");
    CHECK(h.feed(nullptr, 3, nullptr, 0) == 0);
    CHECK_EQ(feed_ok(h, "b"), "b");
}

void test_deterministic()
{
    /* Mesma sequencia de chamadas em filtros novos -> mesmos bytes. */
    const auto run_once = []() {
        cyberdeck_terminal_filter f;
        std::string out;
        out += feed_ok(f, "a\x1B[31m");
        out += feed_ok(f, "bcd\r\n\x1B]0;t\x07" "e");
        out += feed_ok(f, "\x1B");
        out += feed_ok(f, "(Bf");
        out += flush_ok(f);
        return out;
    };
    CHECK_EQ(run_once(), run_once());
    /* Copia (estado trivialmente copiavel) continua o fluxo do original. */
    cyberdeck_terminal_filter f1;
    CHECK_EQ(feed_ok(f1, "a\x1B"), "a");
    cyberdeck_terminal_filter f2 = f1; /* copia com ESC pendente */
    CHECK_EQ(feed_ok(f2, "[31mb"), "b");
    CHECK_EQ(feed_ok(f1, "[31mb"), "b");
}

void test_partition_invariance()
{
    /* REGRESSAO: o texto final NAO depende de como o fluxo foi fatiado em
     * chunks (fragmentacao dos dados SSH e arbitraria). Para cada fluxo,
     * o resultado com um unico feed == resultado com qualquer particao. */
    const std::string streams[] = {
        "plain text 123 !@#\nsecond line\n",
        "a\r\nb\rc\r\r\nd\n",
        "\x1B[31mred\x1B[0m \x1B[1;32mgreen\x1B[0m\r\n",
        "\x1B]0;title\x07\x1B]2;path\x1B\\text",
        "\x1B" "7\x1B" "8\x1B" "=\x1B" ">\x1B" "c\x1B" "(B\x1B" ")0\x1B" "#8\x1B" "%G",
        "caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x94\xA5\r\n",
        "\x1B[2J\x1B[Huser@host:~$\x1B[?25h $ \x1B[38;5;196mERR\x1B[0m\r\n",
        "mis\xC3\xA9\r\x1B[K\rline2\x1B[?25l end\x07",
    };
    for (const std::string &s : streams) {
        const std::string whole = partitioned(s, s.size());
        const size_t steps[] = {1, 2, 3, 5, 7, 11};
        for (size_t step : steps) {
            CHECK_EQ(partitioned(s, step), whole);
        }
    }
}

void test_regression_ssh_flow()
{
    /* Fluxo SSH realista (banner + prompt + saida colorida + OSC de titulo
     * + repaint de linha com \r): o TUI deve exibir apenas o texto. */
    const std::string raw =
        "\x1B[2J\x1B[H"                                     /* limpa tela + home */
        "Welcome to Ubuntu\r\n"
        "\x1B]0;user@host\x07"                              /* titulo via OSC+BEL */
        "user@host:~$"
        "\x1B[?25h "                                       /* cursor + espaco */
        "\x1B[32mok\x1B[0m\r\n"
        "\x1B[1A\x1B[2K\r"                                 /* repaint (progresso) */
        "done\n";
    const std::string expected =
        "Welcome to Ubuntu\n"
        "user@host:~$ ok\n"
        "\ndone\n"; /* "\x1B[1A\x1B[2K\r" vira um LF extra antes de "done" */
    CHECK_EQ(transform({raw}), expected);
    /* O mesmo fluxo fatiado em chunks finos produz o mesmo texto. */
    CHECK_EQ(partitioned(raw, 5), expected);
    CHECK_EQ(partitioned(raw, 7), expected);
}

} // namespace

int main()
{
    test_plain_text_passthrough();
    test_tab_and_lone_lf_preserved();
    test_csi_sequences_removed();
    test_osc_bel_terminated();
    test_osc_st_terminated();
    test_short_escapes_removed();
    test_c0_controls_removed();
    test_crlf_normalization_complete();
    test_crlf_fragmented();
    test_escape_fragmentation();
    test_osc_fragmentation();
    test_utf8_preserved();
    test_escape_abort_on_control_and_utf8();
    test_flush_pending_discards();
    test_output_capacity_truncation();
    test_null_and_empty_inputs();
    test_deterministic();
    test_partition_invariance();
    test_regression_ssh_flow();

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_terminal_filter (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
