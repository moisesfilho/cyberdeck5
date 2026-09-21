/*
 * Testes de REGRESSAO host-side para a composicao local/remota da linha SSH
 * (contrato em components/cyberdeck/include/features/shell/cyberdeck_ssh_line_composer.h;
 * implementacao em
 * components/cyberdeck/src/features/shell/cyberdeck_ssh_line_composer.cpp).
 *
 * Composicao aprovada sob teste (banda local + eco remoto da linha SSH):
 *   - begin() exibe o comando local SEM o '\n' final; o payload enviado ao
 *     remoto e `command + "\n"`; exatamente UM separador '\n' separa o
 *     comando da primeira saida remota;
 *   - o eco remoto `command\n` e suprimido byte a byte, mas a sua quebra
 *     (eco fatiado, incompleto ou com divergencia) nunca produz linha vazia
 *     nem duplica o comando;
 *   - sem eco remoto, a saida legitima recebe o separador exatamente uma vez
 *     e nenhum byte e engolido;
 *   - saida que comeca com '\n' (sem eco) usa esse '\n' como o separador —
 *     sem linha vazia espuria; um '\n' real apos o eco completo e preservado;
 *   - comando vazio (payload "\n"), reset via flush(), UTF-8 opaco fatiado em
 *     codepoints e Enter virtual sem duplicacao (uma ativacao = uma linha
 *     com o comando uma vez e o separador uma vez).
 *
 * POLITICA SERIALIZADA (plano aprovado: "Seriação do envio SSH", opcao 1):
 *   - o compositor mantem NO MAXIMO UM comando pendente (IDLE/PENDING); nao ha
 *     fila FIFO nem type-ahead;
 *   - begin() adicional enquanto active() == true e RECUSADO: retorna 0, nao
 *     escreve comando nem separador e NAO consome a pendencia; o casamento do
 *     eco em curso (inclusive fatiado) permanece intacto;
 *   - apos a pendencia ser resolvida por eco completo, divergencia ou flush(),
 *     begin() volta a ser aceito;
 *   - separador unico com/sem eco, fragmentacao, UTF-8, comando vazio e reset
 *     permanecem cobertos.
 *
 * TDD (mesmo padrao de test_ssh_echo_guard/test_terminal_filter): o contrato
 * serializado removeu o estado FIFO/late_echo (deques), de modo que o
 * cyberdeck_ssh_line_composer.cpp ATUAL (politica FIFO antiga) nao compila e
 * `make test_ssh_line_composer`/`make test` fica vermelho por design ate o
 * coder reimplementar o estado unico (m_active/m_payload/m_matched/
 * m_separator_pending) previsto no header. Quando o build voltar, esta suite e
 * o criterio de aceitacao (613 checks); nenhuma implementacao fake e usada.
 *
 * Estruturado segundo AAA (Arrange, Act, Assert) e principios F.I.R.S.T.
 * Regressao central: INVARIANCIA DE PARTICIONAMENTO — a faixa exibida nao
 * depende de como o fluxo remoto chega fatiado em chunks.
 *
 * Build: make test_ssh_line_composer -> ver Makefile (so g++/make).
 */
#include "features/shell/cyberdeck_ssh_line_composer.h"

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

/* CHECK_EQ tambem compara contagens (nao-vacuidade): numeros sao impressos
 * como texto plano nas mensagens de falha. */
inline std::string escape_string(size_t v) { return std::to_string(v); }
inline std::string escape_string(int v) { return std::to_string(v); }

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++s_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);          \
        }                                                                              \
    } while (0)

/* Avalia `actual`/`expected` UMA unica vez: as chamadas de teste (begin_ok,
 * feed_ok, flush_ok, ...) mutam o estado do compositor, entao nao podem ser
 * reavaliadas na mensagem de falha. */
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

/* Prova de NAO-VACUIDADE: o separador/comando nao podem simplesmente
 * "desaparecer"; contam-se ocorrencias reais na sequencia exata. `needle`
 * NUNCA pode ser vazio (find("", pos) nao avanca). */
size_t count_occurrences(const std::string &text, const char *needle)
{
    if (needle == nullptr || needle[0] == '\0') return 0;
    size_t count = 0;
    for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string::npos;
         pos += strlen(needle)) {
        ++count;
    }
    return count;
}

/* ------------------------------------------------------------------ drivers */

/* begin() com buffer folgado: o comando exibido e curto e o contrato garante
 * out_cap >= len para saida integral. */
std::string begin_ok(cyberdeck_ssh_line_composer &c, const std::string &command)
{
    std::string out(command.size() + 1, '\0');
    const size_t n = c.begin(command.data(), command.size(), &out[0], out.size());
    out.resize(n);
    return out;
}

/* begin() com controle explicito de capacidade (exercita truncamento e out
 * nullptr/out_cap 0). Devolve os bytes efetivamente escritos. */
std::string begin_cap(cyberdeck_ssh_line_composer &c, const std::string &command,
                      size_t cap, size_t *written = nullptr)
{
    std::vector<char> buf(cap ? cap : 1);
    const size_t n = c.begin(command.data(), command.size(),
                             cap ? buf.data() : nullptr, cap);
    if (written) {
        *written = n;
    }
    return std::string(buf.data(), n);
}

/* begin() adicional enquanto armado deve ser RECUSADO: retorna 0 e nao toca
 * `out`. Preenche o buffer com um canario (0xAA) e o devolve em raw_out para
 * provar que NENHUM byte foi escrito (nao-vacuidade da recusa). */
size_t begin_refused_capture(cyberdeck_ssh_line_composer &c, const std::string &command,
                             size_t cap, std::string *raw_out)
{
    std::vector<char> buf(cap ? cap : 1, static_cast<char>(0xAA));
    const size_t n = c.begin(command.data(), command.size(),
                             cap ? buf.data() : nullptr, cap);
    if (raw_out) {
        *raw_out = std::string(buf.data(), cap);
    }
    return n;
}

/* Feed com buffer folgado: pelo contrato, feed() escreve no maximo len + 1
 * (separador + prefixo retido + byte divergente + resto), logo len + 4096
 * elimina qualquer truncamento nos casos de teste. */
std::string feed_ok(cyberdeck_ssh_line_composer &c, const std::string &in)
{
    std::string out(in.size() + 4096, '\0');
    const size_t n = c.feed(in.data(), in.size(), &out[0], out.size());
    out.resize(n);
    return out;
}

/* Feed com controle explicito de capacidade (exercita truncamento e out
 * nullptr/out_cap 0). Devolve os bytes efetivamente escritos. */
std::string feed_cap(cyberdeck_ssh_line_composer &c, const std::string &in, size_t cap,
                     size_t *written = nullptr)
{
    std::vector<char> buf(cap ? cap : 1);
    const size_t n = c.feed(in.data(), in.size(), cap ? buf.data() : nullptr, cap);
    if (written) {
        *written = n;
    }
    return std::string(buf.data(), n);
}

std::string flush_ok(cyberdeck_ssh_line_composer &c)
{
    std::string out(64, '\0');
    const size_t n = c.flush(&out[0], out.size());
    out.resize(n);
    return out;
}

/* begin + feed de cada chunk + flush opcional; concatena a faixa exibida
 * (o comando local vem de begin(), SEM newline). */
std::string compose(const std::string &command, std::initializer_list<std::string> chunks,
                    bool do_flush = true)
{
    cyberdeck_ssh_line_composer c;
    std::string out = begin_ok(c, command);
    CHECK(c.active() == true);
    for (const std::string &ch : chunks) {
        out += feed_ok(c, ch);
    }
    if (do_flush) {
        out += flush_ok(c);
    }
    return out;
}

/* Particiona `stream` em chunks de `step` bytes, com o mesmo begin(); a faixa
 * exibida final deve coincidir com o feed unico. Quando
 * expect_inactive_before_flush for true, valida que o eco concluiu/divergiu e
 * o compositor ja estava em IDLE antes do flush. */
std::string partitioned(const std::string &command, const std::string &stream, size_t step,
                        bool expect_inactive_before_flush = false)
{
    cyberdeck_ssh_line_composer c;
    std::string out = begin_ok(c, command);
    for (size_t i = 0; i < stream.size(); i += step) {
        out += feed_ok(c, stream.substr(i, step));
    }
    if (expect_inactive_before_flush) {
        CHECK(c.active() == false);
    }
    const std::string flushed = flush_ok(c);
    if (expect_inactive_before_flush) {
        CHECK_EQ(flushed, "");
    }
    out += flushed;
    return out;
}

/* ------------------------------------------------------------------ testes */

/* begin(): o comando local e exibido SEM o '\n' final (separador unico fica
 * pendente), com rejeicao de entradas invalidas e de ativacao dupla. */
void test_begin_displays_command_without_newline()
{
    // Arrange: ILHA da linha; a faixa exibida comeca com o comando.
    cyberdeck_ssh_line_composer g;

    // Act & Assert: begin("cmd") exibe "cmd" EXATAMENTE (sem '\n' proprio).
    CHECK_EQ(begin_ok(g, "cmd"), "cmd");
    CHECK(g.active() == true);
    // A linha conclui com o separador unico via flush (sem nenhuma saida).
    CHECK_EQ(flush_ok(g), "\n");
    CHECK(g.active() == false);

    // UTF-8 no comando: bytes opacos exibidos byte a byte, sem newline.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "echo caf\xC3\xA9"), "echo caf\xC3\xA9");
    CHECK(h.active() == true);

    // Comando vazio: begin() nao escreve nada (payload "\n" foi enviado).
    cyberdeck_ssh_line_composer i;
    CHECK_EQ(begin_ok(i, ""), "");
    CHECK(i.active() == true);

    // Handles invalidos: nullptr (com ou sem len) e recusado sem armar.
    cyberdeck_ssh_line_composer j;
    CHECK(j.begin(nullptr, 0, nullptr, 0) == 0);
    CHECK(j.begin(nullptr, 4, nullptr, 0) == 0);
    CHECK(j.active() == false);
    CHECK_EQ(flush_ok(j), "");

    // SERIALIZADO: begin() enquanto armado e recusado (retorna 0, nao escreve
    // nada e nao consome a pendencia); o eco do comando original continua
    // casando e, apos resolve-lo, begin() e aceito de novo.
    cyberdeck_ssh_line_composer k;
    CHECK_EQ(begin_ok(k, "one"), "one");
    CHECK_EQ(begin_ok(k, "two"), "");
    CHECK(k.active() == true);
    CHECK_EQ(feed_ok(k, "one\n"), "\n"); /* eco do pendente suprimido (sep unico) */
    CHECK(k.active() == false);
    CHECK_EQ(begin_ok(k, "two"), "two"); /* aceito apos resolucao */
    CHECK_EQ(feed_ok(k, "two\n"), "\n");
    CHECK(k.active() == false);

    // begin() com out_cap menor que o comando satura sem corromper o armado:
    // o eco a suprimir usa o payload completo de 6 bytes ("...f\n"), nao "abc".
    cyberdeck_ssh_line_composer m;
    size_t w = 0;
    CHECK_EQ(begin_cap(m, "abcdef", 3, &w), "abc");
    CHECK(w == 3);
    CHECK(m.active() == true);
    CHECK_EQ(feed_ok(m, "abcdef\n"), "\n");
    CHECK(m.active() == false);

    // NAO-VACUIDADE: "cmd" apareceu e o separador existe — sequencia exata.
    CHECK_EQ(compose("cmd", {}), "cmd\n");
    CHECK_EQ(count_occurrences(compose("cmd", {}), "cmd"), 1);
    CHECK_EQ(count_occurrences(compose("cmd", {}), "\n"), 1);
}

/* Eco remoto `cmd\n` suprimido: a quebra do eco (completo, fatiado ou
 * incompleto) nunca produz linha vazia nem duplica o comando; o separador e
 * emitido exatamente uma vez. */
void test_echo_suppressed_with_single_separator()
{
    // Eco completo em um unico feed: suprimido; separador emitido uma vez.
    cyberdeck_ssh_line_composer g;
    const std::string b = begin_ok(g, "cmd");
    CHECK_EQ(b, "cmd");
    const std::string f = feed_ok(g, "cmd\n");
    CHECK_EQ(f, "\n");
    CHECK(g.active() == false);
    CHECK_EQ(feed_ok(g, "result line\n"), "result line\n");
    CHECK_EQ(flush_ok(g), "");
    const std::string full_output = b + f + "result line\n";
    CHECK_EQ(full_output, "cmd\nresult line\n"); /* exato */
    CHECK_EQ(count_occurrences(full_output, "cmd"), 1);

    // Eco fatiado byte a byte: suprimido sem vazar; o separador chega apenas
    // quando o '\n' do eco completa o payload (UMA unica vez).
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "cmd"), "cmd");
    CHECK_EQ(feed_ok(h, "c"), "");
    CHECK(h.active() == true);
    CHECK_EQ(feed_ok(h, "m"), "");
    CHECK_EQ(feed_ok(h, "d"), "");
    CHECK(h.active() == true);
    CHECK_EQ(feed_ok(h, "\n"), "\n");
    CHECK(h.active() == false);

    // Eco completo + saida no MESMO chunk: separador antes da saida, quebra
    // sem linha vazia (nenhum '\n' a mais no eco).
    cyberdeck_ssh_line_composer i;
    const std::string bi = begin_ok(i, "cmd");
    const std::string fi = feed_ok(i, "cmd\ncombo line\n");
    CHECK_EQ(fi, "\ncombo line\n");
    CHECK(i.active() == false);
    CHECK_EQ(bi + fi, "cmd\ncombo line\n");
    CHECK_EQ(count_occurrences(bi + fi, "\n"), 2); /* separador + final */

    // Eco QUEBRADO no fim do fluxo (sem o '\n' final): flush() NAO reemite o
    // prefixo parcial (ja exibido por begin) nem acrescenta linha vazia.
    cyberdeck_ssh_line_composer j;
    const std::string bj = begin_ok(j, "cmd");
    CHECK_EQ(bj, "cmd");
    CHECK_EQ(feed_ok(j, "c"), "");
    CHECK_EQ(feed_ok(j, "md"), "");
    CHECK(j.active() == true);
    const std::string fj = flush_ok(j);
    CHECK_EQ(fj, "\n");
    CHECK(j.active() == false);
    CHECK_EQ(bj + fj, "cmd\n");
    CHECK_EQ(count_occurrences(bj + fj, "cmd"), 1);   /* comando nao duplica */
    CHECK_EQ(count_occurrences(bj + fj, "\n"), 1);    /* separador unico */
    CHECK_EQ(count_occurrences(bj + fj, "\n\n"), 0);  /* sem linha vazia */
}

/* Sem eco remoto: a saida legitima recebe o separador exatamente uma vez e
 * nenhum byte e engolido. */
void test_no_echo_separator_exactly_once()
{
    // Divergencia no primeiro byte: separador + saida integral na mesma chamada.
    cyberdeck_ssh_line_composer g;
    const std::string b = begin_ok(g, "cmd");
    CHECK_EQ(b, "cmd");
    const std::string f = feed_ok(g, "result\n");
    CHECK_EQ(f, "\nresult\n");
    CHECK(g.active() == false);
    CHECK_EQ(b + f, "cmd\nresult\n");
    CHECK_EQ(count_occurrences(b + f, "\n"), 2); /* separador + newline da saida */

    // Saida fragmentada: o separador aparece apenas no primeiro feed.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "cmd"), "cmd");
    CHECK_EQ(feed_ok(h, "out"), "\nout");
    CHECK(h.active() == false);
    CHECK_EQ(feed_ok(h, "put\n"), "put\n");
    CHECK_EQ(flush_ok(h), "");

    // Sem eco e sem saida: flush() emite o separador uma unica vez.
    cyberdeck_ssh_line_composer i;
    const std::string bi = begin_ok(i, "cmd");
    CHECK_EQ(bi, "cmd");
    CHECK_EQ(flush_ok(i), "\n");
    CHECK(i.active() == false);

    // NAO-VACUIDADE: saida longa nunca perde bytes (contagem exata).
    const std::string text = "0123456789abcdef\n";
    cyberdeck_ssh_line_composer z;
    CHECK_EQ(begin_ok(z, "cmd"), "cmd");
    const std::string fz = feed_ok(z, text);
    CHECK_EQ(fz, "\n" + text);
    CHECK_EQ(fz.size(), text.size() + 1);
}

/* Invariancia de particionamento: o resultado exibido NAO depende do
 * fatiamento do fluxo remoto (feed unico ou chunks de 1..7 bytes). */
void test_partition_invariance()
{
    struct Case {
        std::string command;
        std::string stream;
        std::string expected;
        bool expect_inactive_before_flush;
    };
    const Case cases[] = {
        /* eco + saida */
        {"cmd", "cmd\nresult 1\nresult 2\n", "cmd\nresult 1\nresult 2\n", true},
        /* sem eco: separador exatamente uma vez */
        {"cmd", "result only\n", "cmd\nresult only\n", true},
        /* eco completo + linha em branco REAL do remoto: preservada */
        {"cmd", "cmd\n\nblank\n", "cmd\n\nblank\n", true},
        /* divergencia apos prefixo parcial: o prefixo e saida legitima */
        {"cmd", "cmdXtail\n", "cmd\ncmdXtail\n", true},
        /* saida que comeca com o comando (ecoa "ls:" divergente no 3º byte) */
        {"ls", "ls: cannot access 'x'\n", "ls\nls: cannot access 'x'\n", true},
        /* comando vazio: eco "\n" suprimido, separador "\n", saida a seguir */
        {"", "\nnext\n", "\nnext\n", true},
        /* comando vazio: eco "\n" + linha em branco real */
        {"", "\n\nprompt\n", "\n\nprompt\n", true},
        /* eco incompleto: flush descarta o prefixo e emite o separador */
        {"cmd", "cmd", "cmd\n", false},
        /* UTF-8 2-byte no comando (eco + saida) */
        {"echo caf\xC3\xA9", "echo caf\xC3\xA9\ndone\n", "echo caf\xC3\xA9\ndone\n", true},
        /* UTF-8 3-byte (Euro) e 4-byte (Fogo) no comando */
        {"preco \xE2\x82\xAC 10", "preco \xE2\x82\xAC 10\nok\n", "preco \xE2\x82\xAC 10\nok\n", true},
        {"fire \xF0\x9F\x94\xA5", "fire \xF0\x9F\x94\xA5\nburn\n", "fire \xF0\x9F\x94\xA5\nburn\n", true},
    };
    for (const Case &c : cases) {
        // NAO-VACUIDADE estrutural: a sequencia esperada existe e os bytes do
        // comando/saida nao somem nem duplicam (sequencias nao vazias).
        CHECK(c.expected.size() > 0);
        CHECK(c.expected.find(c.command) != std::string::npos ||
              (c.command.empty() && c.expected.find('\n') == 0));
        // Feed unico == feed fatiado.
        CHECK_EQ(partitioned(c.command, c.stream, c.stream.size(),
                             c.expect_inactive_before_flush),
                 c.expected);
        const size_t steps[] = {1, 2, 3, 5, 7};
        for (size_t step : steps) {
            CHECK_EQ(partitioned(c.command, c.stream, step,
                                 c.expect_inactive_before_flush),
                     c.expected);
        }
    }
}

/* Saida que comeca com '\n': sem eco, o '\n' do fluxo E o separador (sem
 * linha vazia espuria); apos o eco completo, '\n' real do remoto e
 * preservado. */
void test_output_starting_with_newline()
{
    // Sem eco e saida comecando com '\n': o proprio byte e o separador —
    // nenhum '\n' extra e injetado e a juncao nao tem linha vazia.
    cyberdeck_ssh_line_composer g;
    const std::string b = begin_ok(g, "cmd");
    CHECK_EQ(b, "cmd");
    const std::string f = feed_ok(g, "\nfirst line\n");
    CHECK_EQ(f, "\nfirst line\n");
    CHECK(g.active() == false);
    CHECK_EQ(b + f, "cmd\nfirst line\n");
    CHECK_EQ(count_occurrences(b + f, "\n\n"), 0);

    // O '\n' inicial chega sozinho (chunk de 1 byte): continua sendo o
    // separador; o restante passa como saida.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "cmd"), "cmd");
    CHECK_EQ(feed_ok(h, "\n"), "\n");
    CHECK(h.active() == false);
    CHECK_EQ(feed_ok(h, "rest\n"), "rest\n");

    // Eco completo SEGUIDO de '\n' real: a linha em branco e saida legitima
    // do remoto e nao pode ser engolida.
    cyberdeck_ssh_line_composer i;
    const std::string bi = begin_ok(i, "cmd");
    CHECK_EQ(bi, "cmd");
    const std::string fi = feed_ok(i, "cmd\n\nreal blank\n");
    CHECK_EQ(fi, "\n\nreal blank\n"); /* separador + linha em branco real */
    CHECK(i.active() == false);
    CHECK_EQ(bi + fi, "cmd\n\nreal blank\n");
}

/* Comando vazio (payload "\n"): Enter em linha vazia compoe a quebra com o
 * separador unico; com e sem eco; com e sem saida. */
void test_empty_command()
{
    // Eco do '\n' (o unico byte do payload): suprimido; separador emitido
    // uma vez — a linha vazia vira apenas o '\n'.
    cyberdeck_ssh_line_composer g;
    CHECK_EQ(begin_ok(g, ""), "");
    CHECK_EQ(feed_ok(g, "\n"), "\n");
    CHECK(g.active() == false);

    // Eco do '\n' + saida no mesmo chunk.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, ""), "");
    CHECK_EQ(feed_ok(h, "\nnext prompt\n"), "\nnext prompt\n");
    CHECK(h.active() == false);

    // Sem eco: separador antes da saida.
    cyberdeck_ssh_line_composer i;
    CHECK_EQ(begin_ok(i, ""), "");
    CHECK_EQ(feed_ok(i, "output\n"), "\noutput\n");
    CHECK(i.active() == false);

    // Sem eco e sem saida: flush() emite o separador.
    cyberdeck_ssh_line_composer j;
    CHECK_EQ(begin_ok(j, ""), "");
    CHECK_EQ(flush_ok(j), "\n");
    CHECK(j.active() == false);

    // Feed nulo/vazio com comando vazio pendente: estado intacto.
    cyberdeck_ssh_line_composer k;
    CHECK_EQ(begin_ok(k, ""), "");
    CHECK(k.feed(nullptr, 0, nullptr, 0) == 0);
    CHECK(k.feed("", 0, nullptr, 0) == 0);
    CHECK(k.active() == true);
    CHECK_EQ(flush_ok(k), "\n");

    // NAO-VACUIDADE: a linha do comando vazio existe (nunca vira "" silencioso).
    CHECK_EQ(compose("", {"\n"}), "\n");
    CHECK_EQ(compose("", {"\nnext prompt\n"}), "\nnext prompt\n");
}

/* Multiplos comandos e reset: cada ativacao compoe uma linha independente e
 * somente apos a resolucao (eco/divergencia/flush) begin() e aceito de novo. */
void test_multiple_commands_and_reset()
{
    // Dois comandos sequenciais com eco e saida entre eles.
    cyberdeck_ssh_line_composer g;
    CHECK_EQ(begin_ok(g, "one"), "one");
    CHECK_EQ(feed_ok(g, "one\n"), "\n");
    CHECK(g.active() == false);
    CHECK_EQ(feed_ok(g, "r1\n"), "r1\n");

    CHECK_EQ(begin_ok(g, "two"), "two");
    CHECK_EQ(feed_ok(g, "two\nr2\n"), "\nr2\n");
    CHECK(g.active() == false);

    // flush() como reset apos eco incompleto: proximo comando arma normal.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "ab"), "ab");
    CHECK_EQ(feed_ok(h, "ab"), "");
    CHECK_EQ(flush_ok(h), "\n");
    CHECK(h.active() == false);
    CHECK_EQ(begin_ok(h, "cd"), "cd");
    CHECK_EQ(feed_ok(h, "cd\n"), "\n");
    CHECK(h.active() == false);

    // Comando vazio entre comandos nao corrompe o estado.
    CHECK_EQ(compose("x", {"x\nout\n"}), "x\nout\n");
    CHECK_EQ(compose("", {"\n"}), "\n");
    CHECK_EQ(compose("y", {"y\n"}), "y\n");

    // SERIALIZADO: begin() enquanto armado e recusado; apos o eco do comando
    // pendente, o compositor volta a IDLE e aceita o proximo begin().
    cyberdeck_ssh_line_composer i;
    CHECK_EQ(begin_ok(i, "cmd"), "cmd");
    CHECK_EQ(begin_ok(i, "other"), "");       /* recusado: nada escrito */
    CHECK(i.active() == true);
    CHECK_EQ(feed_ok(i, "cmd\n"), "\n");      /* eco do pendente */
    CHECK(i.active() == false);
    CHECK_EQ(begin_ok(i, "other"), "other");  /* aceito apos resolucao */
    CHECK_EQ(feed_ok(i, "other\n"), "\n");
    CHECK(i.active() == false);

    // flush(nullptr, 0) descarta a pendencia (separador tambem) e rearma.
    cyberdeck_ssh_line_composer j;
    CHECK_EQ(begin_ok(j, "x"), "x");
    CHECK_EQ(feed_ok(j, "x"), "");
    CHECK(j.flush(nullptr, 0) == 0);
    CHECK(j.active() == false);
    CHECK_EQ(begin_ok(j, "y"), "y");
    CHECK_EQ(feed_ok(j, "y\n"), "\n");
}

/* UTF-8: bytes altos comparados e emitidos byte a byte; um codepoint pode
 * ser fatiado entre chunks sem corromper a supressao do eco nem a saida. */
void test_utf8()
{
    // Eco UTF-8 fragmentado no meio do codepoint: suprimido, separador unico.
    cyberdeck_ssh_line_composer g;
    const std::string command = "echo caf\xC3\xA9"; /* 'e' acentuado = C3 A9 */
    CHECK_EQ(begin_ok(g, command), command);
    CHECK_EQ(feed_ok(g, "echo caf"), "");
    CHECK_EQ(feed_ok(g, "\xC3"), "");
    CHECK(g.active() == true);
    CHECK_EQ(feed_ok(g, "\xA9"), "");
    CHECK_EQ(feed_ok(g, "\n"), "\n");
    CHECK(g.active() == false);
    CHECK_EQ(feed_ok(g, "\xC3\xA9 ok\n"), "\xC3\xA9 ok\n");

    // Eco + saida UTF-8 no mesmo chunk.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "caf\xC3\xA9"), "caf\xC3\xA9");
    CHECK_EQ(feed_ok(h, "caf\xC3\xA9\n\xE2\x82\xAC 10\n"), "\n\xE2\x82\xAC 10\n");
    CHECK(h.active() == false);

    // Divergencia no meio de um codepoint: prefixo retido + byte divergente
    // emitidos integralmente (nada e engolido).
    cyberdeck_ssh_line_composer i;
    const std::string bi = begin_ok(i, "p\xC3\xA9");
    CHECK_EQ(bi, "p\xC3\xA9");
    const std::string fi = feed_ok(i, "p\xC3\xA8!"); /* diverge no 3º byte */
    CHECK_EQ(fi, "\np\xC3\xA8!");
    CHECK(i.active() == false);
    CHECK_EQ(bi + fi, "p\xC3\xA9\np\xC3\xA8!");

    // Codepoint de 4 bytes fatiado byte a byte no eco.
    cyberdeck_ssh_line_composer j;
    CHECK_EQ(begin_ok(j, "\xF0\x9F\x94\xA5"), "\xF0\x9F\x94\xA5");
    CHECK_EQ(feed_ok(j, "\xF0"), "");
    CHECK_EQ(feed_ok(j, "\x9F"), "");
    CHECK_EQ(feed_ok(j, "\x94"), "");
    CHECK_EQ(feed_ok(j, "\xA5"), "");
    CHECK_EQ(feed_ok(j, "\n"), "\n");
    CHECK(j.active() == false);
    CHECK_EQ(feed_ok(j, "\xF0\x9F\x94\xA5 flame\n"), "\xF0\x9F\x94\xA5 flame\n");
}

/* Enter virtual nao duplica: uma ativacao (begin) compoe exatamente uma
 * linha — comando uma vez e separador uma vez — e deixa o compositor em IDLE
 * na MESMA chamada (sem janela de re-processamento). */
void test_virtual_enter_no_duplicate()
{
    // Eco completo + saida: IDLE imediato ao concluir o eco; comando UMA vez.
    cyberdeck_ssh_line_composer g;
    const std::string b = begin_ok(g, "cmd");
    CHECK_EQ(b, "cmd");
    const std::string f = feed_ok(g, "cmd\nresult\n");
    CHECK_EQ(f, "\nresult\n");
    CHECK(g.active() == false); /* IDLE na mesma chamada: nada re-processavel */
    const std::string display = b + f;
    CHECK_EQ(display, "cmd\nresult\n");
    CHECK_EQ(count_occurrences(display, "cmd"), 1); /* comando UMA vez */
    CHECK_EQ(count_occurrences(display, "\n"), 2);  /* separador + final */

    // Dois Enters virtuais sequenciais (dois comandos iguais): cada linha tem
    // o comando e o separador exatamente uma vez; sem linha vazia entre elas.
    cyberdeck_ssh_line_composer h;
    std::string d2 = begin_ok(h, "cmd");
    d2 += feed_ok(h, "cmd\n");
    d2 += begin_ok(h, "cmd");
    d2 += feed_ok(h, "cmd\n");
    CHECK_EQ(d2, "cmd\ncmd\n");
    CHECK_EQ(count_occurrences(d2, "cmd"), 2);
    CHECK_EQ(count_occurrences(d2, "\n\n"), 0);

    // Enter duplicado enquanto a linha ainda esta pendente: a segunda
    // ativacao e RECUSADA (nada escrito), o eco da primeira e suprimido e a
    // faixa local nao duplica o comando nem o separador.
    cyberdeck_ssh_line_composer i;
    std::string di = begin_ok(i, "v");
    CHECK_EQ(di, "v");
    CHECK_EQ(begin_ok(i, "v"), ""); /* recusado */
    CHECK(i.active() == true);
    const std::string fi = feed_ok(i, "v\n");
    CHECK_EQ(fi, "\n");
    CHECK(i.active() == false);
    di += fi;
    CHECK_EQ(di, "v\n");
    CHECK_EQ(count_occurrences(di, "v"), 1);    /* comando UMA vez */
    CHECK_EQ(count_occurrences(di, "\n"), 1);   /* um separador */
    // Depois de resolvida, o Enter seguinte e aceito normalmente.
    CHECK_EQ(begin_ok(i, "v"), "v");
    CHECK_EQ(feed_ok(i, "v\n"), "\n");
    CHECK(i.active() == false);

    // Eco tardio apos flush (linha ja resolvida): e saida legitima, o
    // compositor nunca duplica estado.
    cyberdeck_ssh_line_composer j;
    CHECK_EQ(begin_ok(j, "cmd"), "cmd");
    CHECK_EQ(feed_ok(j, "cmd"), "");
    CHECK_EQ(flush_ok(j), "\n");
    CHECK(j.active() == false);
    CHECK_EQ(feed_ok(j, "cmd\n"), "cmd\n");
}

/* SERIALIZADO: enquanto ha comando pendente, begin() adicional e recusado —
 * retorna 0, nao escreve comando nem separador (buffer canario intacto) e nao
 * consome a pendencia. */
void test_begin_refused_while_pending()
{
    // Arrange: linha armada.
    cyberdeck_ssh_line_composer c;
    CHECK_EQ(begin_ok(c, "cmd"), "cmd");
    CHECK(c.active() == true);

    // Act: begin() adicional com buffer canario (prova de nao-escrita).
    std::string raw;
    const size_t n = begin_refused_capture(c, "other", 32, &raw);

    // Assert: retorno 0, buffer intacto byte a byte e estado preservado.
    CHECK_EQ(n, static_cast<size_t>(0));
    CHECK_EQ(raw, std::string(32, '\xAA')); /* NAO-VACUIDADE: nenhum byte tocado */
    CHECK(c.active() == true);

    // Recusa tambem com out nullptr/cap 0 e com comando nullptr.
    CHECK(c.begin("third", 5, nullptr, 0) == 0);
    CHECK(c.active() == true);
    CHECK(c.begin(nullptr, 3, nullptr, 0) == 0);
    CHECK(c.active() == true);

    // PENDENCIA PRESERVADA: o eco do comando original ainda e suprimido.
    CHECK_EQ(feed_ok(c, "cmd\n"), "\n");
    CHECK(c.active() == false);
}

/* SERIALIZADO: a recusa de begin() nao interfere no casamento do eco pendente,
 * inclusive com eco fragmentado e UTF-8; a divergencia tambem nao e afetada. */
void test_begin_refused_keeps_pending_echo()
{
    // Eco fragmentado: recusa no meio da retencao nao perde o casamento.
    cyberdeck_ssh_line_composer c;
    CHECK_EQ(begin_ok(c, "cmd"), "cmd");
    CHECK_EQ(feed_ok(c, "cm"), "");           /* prefixo retido */
    CHECK(c.active() == true);
    CHECK_EQ(begin_ok(c, "other"), "");       /* recusado no meio do casamento */
    CHECK(c.active() == true);
    CHECK_EQ(feed_ok(c, "d"), "");            /* casamento continua */
    CHECK(c.active() == true);
    CHECK_EQ(feed_ok(c, "\n"), "\n");         /* completa: separador unico */
    CHECK(c.active() == false);
    CHECK_EQ(feed_ok(c, "clean\n"), "clean\n"); /* saida pos-resolucao intacta */

    // UTF-8 fragmentado + recusa: supressao do eco preservada.
    cyberdeck_ssh_line_composer h;
    const std::string cmd = "caf\xC3\xA9";
    CHECK_EQ(begin_ok(h, cmd), cmd);
    CHECK_EQ(feed_ok(h, "caf"), "");
    CHECK_EQ(begin_ok(h, "x"), "");           /* recusado */
    CHECK_EQ(feed_ok(h, "\xC3"), "");
    CHECK_EQ(feed_ok(h, "\xA9"), "");
    CHECK_EQ(feed_ok(h, "\n"), "\n");
    CHECK(h.active() == false);

    // Divergencia do pendente apos recusa: saida integral, sem engolir bytes.
    cyberdeck_ssh_line_composer i;
    CHECK_EQ(begin_ok(i, "cmd"), "cmd");
    CHECK_EQ(begin_ok(i, "zzz"), "");         /* recusado */
    CHECK_EQ(feed_ok(i, "cmX\n"), "\ncmX\n"); /* prefixo retido + divergente */
    CHECK(i.active() == false);
    CHECK_EQ(begin_ok(i, "after"), "after");  /* aceito apos divergencia */
    CHECK_EQ(feed_ok(i, "after\n"), "\n");
    CHECK(i.active() == false);
}

/* SERIALIZADO: apos a pendencia ser resolvida por eco completo ou por
 * divergencia, begin() volta a ser aceito (estado IDLE). */
void test_begin_accepted_after_resolution()
{
    // Apos eco completo.
    cyberdeck_ssh_line_composer c;
    CHECK_EQ(begin_ok(c, "one"), "one");
    CHECK_EQ(feed_ok(c, "one\n"), "\n");
    CHECK(c.active() == false);
    CHECK_EQ(begin_ok(c, "two"), "two");
    CHECK(c.active() == true);
    CHECK_EQ(feed_ok(c, "two\n"), "\n");
    CHECK(c.active() == false);

    // Apos divergencia (sem eco).
    cyberdeck_ssh_line_composer d;
    CHECK_EQ(begin_ok(d, "ab"), "ab");
    CHECK_EQ(feed_ok(d, "XY"), "\nXY"); /* divergencia k=0: sem prefixo retido */
    CHECK(d.active() == false);
    CHECK_EQ(begin_ok(d, "cd"), "cd");
    CHECK(d.active() == true);
    CHECK_EQ(feed_ok(d, "cd\n"), "\n");
    CHECK(d.active() == false);

    // Apos divergencia cujo primeiro byte e '\n' (o byte E o separador).
    cyberdeck_ssh_line_composer e;
    CHECK_EQ(begin_ok(e, "ab"), "ab");
    CHECK_EQ(feed_ok(e, "\nout\n"), "\nout\n");
    CHECK(e.active() == false);
    CHECK_EQ(begin_ok(e, "cd"), "cd");
    CHECK_EQ(feed_ok(e, "cd\n"), "\n");
    CHECK(e.active() == false);
}

/* SERIALIZADO: apos flush() (inclusive o reset que descarta a pendencia),
 * begin() e aceito para o proximo comando. */
void test_begin_accepted_after_flush()
{
    // flush() apos eco incompleto: separador unico e rearme.
    cyberdeck_ssh_line_composer c;
    CHECK_EQ(begin_ok(c, "cmd"), "cmd");
    CHECK_EQ(feed_ok(c, "cm"), "");
    CHECK_EQ(flush_ok(c), "\n");
    CHECK(c.active() == false);
    CHECK_EQ(begin_ok(c, "next"), "next");
    CHECK_EQ(feed_ok(c, "next\n"), "\n");
    CHECK(c.active() == false);

    // flush(nullptr, 0) descarta a pendencia e rearma sem escrever nada.
    cyberdeck_ssh_line_composer d;
    CHECK_EQ(begin_ok(d, "cmd"), "cmd");
    CHECK_EQ(feed_ok(d, "cmd"), "");
    CHECK(d.flush(nullptr, 0) == 0);
    CHECK(d.active() == false);
    CHECK_EQ(begin_ok(d, "next"), "next");
    CHECK_EQ(feed_ok(d, "next\n"), "\n");
    CHECK(d.active() == false);
}

/* SERIALIZADO: flush() da UNICA pendencia respeita out_cap (satura sem
 * rearmar mal) e e idempotente em IDLE. */
void test_flush_single_pending_capacity()
{
    cyberdeck_ssh_line_composer c;
    CHECK_EQ(begin_ok(c, "x"), "x");
    CHECK(c.active() == true);

    // cap 0: descarta a pendencia, retorna 0 e nao toca o buffer.
    char buf[1] = {'Z'};
    CHECK(c.flush(buf, 0) == 0);
    CHECK(buf[0] == 'Z');
    CHECK(c.active() == false);

    // IDLE: flush() retorna 0 e nao altera nada.
    CHECK(c.flush(buf, sizeof(buf)) == 0);
    CHECK(c.active() == false);

    // Nova pendencia: cap 1 emite exatamente o separador.
    CHECK_EQ(begin_ok(c, "y"), "y");
    CHECK(c.flush(buf, sizeof(buf)) == 1);
    CHECK_EQ(buf[0], '\n');
    CHECK(c.active() == false);

    // Pendencia ja resolvida por eco completo: flush nao reemite separador.
    CHECK_EQ(begin_ok(c, "z"), "z");
    CHECK_EQ(feed_ok(c, "z\n"), "\n");
    CHECK(c.active() == false);
    CHECK(c.flush(buf, sizeof(buf)) == 0);
}

/* INVARIANCIA e NAO-VACUIDADE de begin() recusado: intercalar uma tentativa
 * de rearme (recusada) antes de cada feed NAO muda a faixa exibida em relacao
 * a sequencia sem tentativas; e cada recusa realmente ocorreu (contador > 0)
 * e nao escreveu bytes (canario intacto). */
void test_refused_begin_invariance()
{
    struct Case {
        std::string command;
        std::string stream;
    };
    const Case cases[] = {
        {"cmd", "cmd\nresult 1\nresult 2\n"},
        {"cmd", "result only\n"},
        {"cmd", "cmd\n\nblank\n"},
        {"cmd", "cmdXtail\n"},
        {"cmd", ""}, /* sem eco e sem saida: resolve no flush */
        {"", "\nnext\n"},
        {"echo caf\xC3\xA9", "echo caf\xC3\xA9\ndone\n"},
        {"fire \xF0\x9F\x94\xA5", "fire \xF0\x9F\x94\xA5\nburn\n"},
    };
    for (const Case &cse : cases) {
        // Baseline sem recusa: faixa exibida de referencia.
        const std::string expected = compose(cse.command, {cse.stream});
        CHECK(expected.size() > 0);

        // Mesma sequencia, com uma tentativa de rearme (deve ser recusada)
        // antes de cada feed enquanto o compositor estiver armado.
        cyberdeck_ssh_line_composer c;
        std::string out = begin_ok(c, cse.command);
        size_t refused = 0;
        {
            std::string raw;
            const size_t n = begin_refused_capture(c, "probe", 24, &raw);
            CHECK_EQ(n, static_cast<size_t>(0));
            CHECK_EQ(raw, std::string(24, '\xAA'));
            if (n == 0) ++refused;
        }
        for (size_t i = 0; i < cse.stream.size(); ++i) {
            out += feed_ok(c, cse.stream.substr(i, 1));
            if (c.active()) {
                std::string raw;
                const size_t n = begin_refused_capture(c, "probe", 24, &raw);
                CHECK_EQ(n, static_cast<size_t>(0));
                CHECK_EQ(raw, std::string(24, '\xAA'));
                ++refused;
            }
        }
        out += flush_ok(c);

        // NAO-VACUIDADE: houve recusa real e o resultado e identico ao baseline.
        CHECK(refused > 0);
        CHECK_EQ(out, expected);
        if (!cse.command.empty()) {
            CHECK_EQ(count_occurrences(out, cse.command.c_str()),
                     count_occurrences(expected, cse.command.c_str()));
        }
    }
}

/* Entradas nulas/vazias e limites de capacidade: no-op sem corromper o estado
 * e saturacao consistente com a guard. */
void test_null_inputs_and_capacity()
{
    // Feed nulo/vazio com retencao ativa: estado e m_matched intactos.
    cyberdeck_ssh_line_composer g;
    CHECK_EQ(begin_ok(g, "cmd"), "cmd");
    CHECK_EQ(feed_ok(g, "cm"), "");
    char dummy[16];
    CHECK(g.feed(nullptr, 0, dummy, sizeof(dummy)) == 0);
    CHECK(g.feed("", 0, dummy, sizeof(dummy)) == 0);
    CHECK(g.feed(nullptr, 5, dummy, sizeof(dummy)) == 0);
    CHECK(g.active() == true);
    CHECK_EQ(feed_ok(g, "d\n"), "\n");
    CHECK(g.active() == false);

    // Cap 0 no feed: o estado avanca mesmo sem emitir nada (o eco e consumido
    // e o compositor fica IDLE; a saida seguinte passa).
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "cmd"), "cmd");
    size_t w = 999;
    CHECK_EQ(feed_cap(h, "cmd\n", 0, &w), "");
    CHECK(w == 0);
    CHECK(h.active() == false);
    CHECK_EQ(feed_ok(h, "x"), "x");

    // IDLE: pass-through opaco byte a byte (incl. UTF-8 e fluxo pos-filtro).
    cyberdeck_ssh_line_composer i;
    CHECK(i.active() == false);
    CHECK_EQ(feed_ok(i, "opaque \xE2\x82\xAC \xF0\x9F\x94\xA5\n"),
             "opaque \xE2\x82\xAC \xF0\x9F\x94\xA5\n");
    CHECK_EQ(flush_ok(i), "");

    // flush() idempotente em IDLE.
    CHECK(i.flush(nullptr, 0) == 0);
    CHECK(i.active() == false);
}

} // namespace

int main()
{
    test_begin_displays_command_without_newline();
    test_echo_suppressed_with_single_separator();
    test_no_echo_separator_exactly_once();
    test_partition_invariance();
    test_output_starting_with_newline();
    test_empty_command();
    test_multiple_commands_and_reset();
    test_utf8();
    test_virtual_enter_no_duplicate();
    test_begin_refused_while_pending();
    test_begin_refused_keeps_pending_echo();
    test_begin_accepted_after_resolution();
    test_begin_accepted_after_flush();
    test_flush_single_pending_capacity();
    test_refused_begin_invariance();
    test_null_inputs_and_capacity();

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_ssh_line_composer (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
