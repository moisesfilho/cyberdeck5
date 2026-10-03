/*
 * Testes de REGRESSAO host-side para a composicao local/remota da linha SSH
 * (contrato em components/cyberdeck/include/apps/shell/cyberdeck_ssh_line_composer.h;
 * implementacao em
 * components/cyberdeck/src/apps/shell/cyberdeck_ssh_line_composer.cpp).
 *
 * REQ-SSH-01/AC-SSH-01 (nenhum LF artificial nos retornos)
 * -------------------------------------------------------
 * O compositor NAO fabrica quebras de linha.  A composicao sob teste e a banda
 * local + o eco remoto da linha SSH, e ela preserva os bytes remotos:
 *   - begin() exibe o comando local e NADA mais: o payload enviado ao remoto e
 *     `command + "\n"` e o '\n' do payload NAO aparece na banda exibida;
 *   - eco remoto completo (`command\n`, fatiado ou nao) e suprimido byte a byte
 *     e devolve ZERO bytes — a quebra que fechou o eco e do proprio eco e some
 *     com ele, porque o eco inteiro e suprimido;
 *   - divergencia da saida em relacao ao payload libera o prefixo retido, o
 *     byte divergente e o resto do chunk VERBATIM, sem injetar nenhum '\n'
 *     antes deles (paridade com "o prefixo retido nunca e engolido" da guard);
 *   - saida que comeca com '\n' sem eco: esse byte e-output remoto e preservado
 *     como separador natural, sem nenhum byte extra;
 *   - flush() resolve a pendencia e devolve ZERO bytes: o prefixo parcial ja foi
 *     exibido por begin() e nao e reemitido, e nenhuma quebra e fabricada;
 *   - um '\n' REAL recebido do remoto — inclusive a linha em branco que vem
 *     depois do eco completo — e sempre preservado integralmente.
 *
 * REQ-SSH-02/AC-SSH-02 (preservar saida remota, eco, payload e desconexao)
 * ----------------------------------------------------------------------
 *   - saida completa COM LF final e SEM LF final: nos dois casos a faixa exibida
 *     contem exatamente os bytes que o remoto enviou, sem LF acrescentado;
 *   - o eco do payload nunca aparece duas vezes e nunca e engolido por engano
 *     (divergencia libera o prefixo, mesmo apos recusa de begin());
 *   - begin()/<PII type="BIC_SWIFT" id="32"/> intactos: `command + "\n"` vai para o PTY e a
 *     banda local mostra o comando cru;
 *   - flush() e a unica coisa que rearma o compositor, e continua sendo o reset
 *     usado na desconexao.
 *
 * POLITICA SERIALIZADA (plano aprovado: "Seriação do envio SSH", opcao 1):
 *   - o compositor mantem NO MAXIMO UM comando pendente (IDLE/PENDING); nao ha
 *     fila FIFO nem type-ahead;
 *   - begin() adicional enquanto active() == true e RECUSADO: retorna 0, nao
 *     escreve comando algum e NAO consome a pendencia; o casamento do eco em curso
 *     (inclusive fatiado) permanece intacto;
 *   - apos a pendencia ser resolvida por eco completo, divergencia ou flush(),
 *     begin() volta a ser aceito.
 *
 * Estruturado segundo AAA (Arrange, Act, Assert) e principios F.I.R.S.T.
 * Regressoes centrais:
 *   - INVARIANCIA DE PARTICIONAMENTO — a faixa exibida nao depende de como o
 *     fluxo remoto chega fatiado em chunks;
 *   - CONTAGEM EXATA DE LF — o numero de '\n' na faixa exibida e o numero de
 *     '\n' que o remoto realmente enviou (menos o '\n' do eco suprimido), nunca
 *     mais e nunca menos.  Essa contagem e a prova de nao-fabricacao: um LF
 *     artificial apareceria como "\n" a mais, e um LF real engolido como um a menos.
 *
 * Build: make test_ssh_line_composer -> ver Makefile (so g++/make).
 */
#include "apps/shell/cyberdeck_ssh_line_composer.h"

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

/* Prova de NAO-VACUIDADE: comando, saida remota e LF real nao podem
 * simplesmente "desaparecer"; contam-se ocorrencias reais na sequencia exata.
 * `needle` NUNCA pode ser vazio (find("", pos) nao avanca). */
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

/* Feed com buffer folgado: pelo contrato, feed() escreve no maximo len bytes
 * (prefixo retido + byte divergente + resto do chunk, ou pass-through opaco),
 * logo len + 4096 elimina qualquer truncamento nos casos de teste. */
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


/* flush() com controle explicito de capacidade e preenchimento com canario:
 * prova que flush() nao escreve NENHUM byte em nenhuma capacidade, nem quando a
 * pendencia existe e o orcamento sobra (REQ-SSH-01). */
size_t flush_canary(cyberdeck_ssh_line_composer &c, size_t cap,
                    std::string *raw_out = nullptr)
{
    std::vector<char> buf(cap ? cap : 1, static_cast<char>(0xAA));
    const size_t n = c.flush(cap ? buf.data() : nullptr, cap);
    if (raw_out) {
        *raw_out = std::string(buf.data(), cap);
    }
    return n;
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

/* begin(): o comando local e exibido SEM o '\n' final, e nenhuma quebra e
 * fabricada em nenhuma das saidas — nem em begin(), nem em flush(). */
void test_begin_displays_command_without_newline()
{
    // Arrange: ILHA da linha; a faixa exibida comeca com o comando.
    cyberdeck_ssh_line_composer g;

    // Act & Assert: begin("cmd") exibe "cmd" EXATAMENTE (sem '\n' proprio).
    CHECK_EQ(begin_ok(g, "cmd"), "cmd");
    CHECK(g.active() == true);
    // A linha conclui no flush sem nenhuma saida e sem NENHUM byte inventado.
    CHECK_EQ(flush_ok(g), "");
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
    CHECK_EQ(feed_ok(k, "one\n"), ""); /* eco do pendente suprimido por inteiro */
    CHECK(k.active() == false);
    CHECK_EQ(begin_ok(k, "two"), "two"); /* aceito apos resolucao */
    CHECK_EQ(feed_ok(k, "two\n"), "");
    CHECK(k.active() == false);

    // begin() com out_cap menor que o comando satura sem corromper o armado:
    // o eco a suprimir usa o payload completo de 6 bytes ("...f\n"), nao "abc".
    cyberdeck_ssh_line_composer m;
    size_t w = 0;
    CHECK_EQ(begin_cap(m, "abcdef", 3, &w), "abc");
    CHECK(w == 3);
    CHECK(m.active() == true);
    CHECK_EQ(feed_ok(m, "abcdef\n"), "");
    CHECK(m.active() == false);

    // NAO-VACUIDADE: "cmd" apareceu exatamente uma vez e ZERO '\n' foi
    // fabricado — sequencia exata, nao uma comparacao vaga.
    CHECK_EQ(compose("cmd", {}), "cmd");
    CHECK_EQ(count_occurrences(compose("cmd", {}), "cmd"), 1);
    CHECK_EQ(count_occurrences(compose("cmd", {}), "\n"), 0);
}

/* Eco remoto `cmd\n` suprimido: o eco inteiro some, sem deixar para tras uma
 * quebra nem duplicar o comando.  O LF que fechou o eco e do proprio eco. */
void test_echo_suppressed_without_artificial_lf()
{
    // Eco completo em um unico feed: suprimido por inteiro, zero bytes de saida.
    cyberdeck_ssh_line_composer g;
    const std::string b = begin_ok(g, "cmd");
    CHECK_EQ(b, "cmd");
    const std::string f = feed_ok(g, "cmd\n");
    CHECK_EQ(f, "");
    CHECK(f.empty());
    CHECK(g.active() == false);
    CHECK_EQ(feed_ok(g, "result line\n"), "result line\n");
    CHECK_EQ(flush_ok(g), "");
    const std::string full_output = b + f + "result line\n";
    CHECK_EQ(full_output, "cmdresult line\n"); /* exato */
    CHECK_EQ(count_occurrences(full_output, "cmd"), 1);
    /* O unico '\n' da faixa e o da saida real do remoto; nada foi acrescentado. */
    CHECK_EQ(count_occurrences(full_output, "\n"), 1);

    // Eco fatiado byte a byte: suprimido sem vazar e sem devolver nada.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "cmd"), "cmd");
    CHECK_EQ(feed_ok(h, "c"), "");
    CHECK(h.active() == true);
    CHECK_EQ(feed_ok(h, "m"), "");
    CHECK_EQ(feed_ok(h, "d"), "");
    CHECK(h.active() == true);
    CHECK_EQ(feed_ok(h, "\n"), "");
    CHECK(h.active() == false);
    /* Nenhum byte sobrou do eco, nem mesmo a sua quebra. */
    CHECK_EQ(count_occurrences(b + f, "\n"), 0);
    CHECK_EQ(count_occurrences(b + f, "cmd"), 1);

    // Eco completo + saida no MESMO chunk: a saida passa intacta e a quebra que
    // ela traz e preservada exatamente uma vez.
    cyberdeck_ssh_line_composer i;
    const std::string bi = begin_ok(i, "cmd");
    const std::string fi = feed_ok(i, "cmd\ncombo line\n");
    CHECK_EQ(fi, "combo line\n");
    CHECK(i.active() == false);
    CHECK_EQ(bi + fi, "cmdcombo line\n");
    CHECK_EQ(count_occurrences(bi + fi, "\n"), 1); /* so o LF real da saida */

    // Eco QUEBRADO no fim do fluxo (sem o '\n' final): flush() NAO reemite o
    // prefixo parcial (ja exibido por begin), NAO duplica o comando e NAO
    // acrescenta quebra alguma.
    cyberdeck_ssh_line_composer j;
    const std::string bj = begin_ok(j, "cmd");
    CHECK_EQ(bj, "cmd");
    CHECK_EQ(feed_ok(j, "c"), "");
    CHECK_EQ(feed_ok(j, "md"), "");
    CHECK(j.active() == true);
    const std::string fj = flush_ok(j);
    CHECK_EQ(fj, "");
    CHECK(j.active() == false);
    CHECK_EQ(bj + fj, "cmd");
    CHECK_EQ(count_occurrences(bj + fj, "cmd"), 1);   /* comando nao duplica */
    CHECK_EQ(count_occurrences(bj + fj, "\n"), 0);    /* nenhuma quebra criada */
    CHECK_EQ(count_occurrences(bj + fj, "\n\n"), 0);  /* sem linha vazia */
}

/* Sem eco remoto: a saida legitima chega VERBATIM, com ou sem LF final, e
 * nenhum byte e engolido nem acrescentado. */
void test_no_echo_output_is_verbatim()
{
    // Divergencia no primeiro byte: a saida integral na mesma chamada.
    cyberdeck_ssh_line_composer g;
    const std::string b = begin_ok(g, "cmd");
    CHECK_EQ(b, "cmd");
    const std::string f = feed_ok(g, "result\n");
    CHECK_EQ(f, "result\n");
    CHECK(g.active() == false);
    CHECK_EQ(b + f, "cmdresult\n");
    CHECK_EQ(count_occurrences(b + f, "\n"), 1); /* so o LF real da saida */

    // Saida fragmentada: cada pedaco passa como chegou.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "cmd"), "cmd");
    CHECK_EQ(feed_ok(h, "out"), "out");
    CHECK(h.active() == false);
    CHECK_EQ(feed_ok(h, "put\n"), "put\n");
    CHECK_EQ(flush_ok(h), "");

    // Sem eco e sem saida: flush() nao inventa nada.
    cyberdeck_ssh_line_composer i;
    const std::string bi = begin_ok(i, "cmd");
    CHECK_EQ(bi, "cmd");
    CHECK_EQ(flush_ok(i), "");
    CHECK(i.active() == false);

    // NAO-VACUIDADE: saida longa nunca perde bytes nem ganha LF (contagem exata).
    const std::string text = "0123456789abcdef\n";
    cyberdeck_ssh_line_composer z;
    CHECK_EQ(begin_ok(z, "cmd"), "cmd");
    const std::string fz = feed_ok(z, text);
    CHECK_EQ(fz, text);
    CHECK_EQ(fz.size(), text.size());
    CHECK_EQ(count_occurrences(fz, "\n"), count_occurrences(text, "\n"));
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
        /* REQ-SSH-01: '\n' que o remoto realmente enviou e que DEVE sobreviver
         * na faixa exibida.  A contagem e exata e independente do fatiamento:
         * um LF artificial a deixaria maior e um LF real engolido menor. */
        size_t expected_lf;
        /* Linha em branco real do remoto: o par "\n\n" e o que distingue
         * "LF preservado" de "LF acrescentado" numa faixa exibida. */
        size_t expected_blank_lines;
    };
    const Case cases[] = {
        /* eco + saida: o '\n' do eco e suprimido junto com ele */
        {"cmd", "cmd\nresult 1\nresult 2\n", "cmdresult 1\nresult 2\n", true, 2, 0},
        /* sem eco: a saida real entra inteira */
        {"cmd", "result only\n", "cmdresult only\n", true, 1, 0},
        /* eco completo + linha em branco REAL do remoto: o '\n' extra do fluxo
         * sobrevive, porque pertence a saida e nao ao eco */
        {"cmd", "cmd\n\nblank\n", "cmd\nblank\n", true, 2, 0},
        /* eco completo + DUAS linhas em branco reais: ambas preservadas */
        {"cmd", "cmd\n\n\nreal\n", "cmd\n\nreal\n", true, 3, 1},
        /* divergencia apos prefixo parcial: o prefixo e saida legitima */
        {"cmd", "cmdXtail\n", "cmdcmdXtail\n", true, 1, 0},
        /* divergencia + linha em branco real preservada */
        {"cmd", "cmdX\n\nblank\n", "cmdcmdX\n\nblank\n", true, 3, 1},
        /* saida que comeca com o comando (ecoa "ls:" divergente no 3º byte) */
        {"ls", "ls: cannot access 'x'\n", "lsls: cannot access 'x'\n", true, 1, 0},
        /* comando vazio: eco "\n" suprimido por inteiro, saida a seguir */
        {"", "\nnext\n", "next\n", true, 1, 0},
        /* comando vazio: eco "\n" + linha em branco real preservada */
        {"", "\n\nprompt\n", "\nprompt\n", true, 2, 0},
        /* eco incompleto: flush descarta o prefixo e nao fabrica nada */
        {"cmd", "cmd", "cmd", false, 0, 0},
        /* eco incompleto com saida sem LF: verbatim, zero LF na faixa */
        {"cmd", "cmdpartial", "cmdcmdpartial", true, 0, 0},
        /* eco incompleto com saida COM LF: verbatim, um LF na faixa */
        {"cmd", "cmdrest\n", "cmdcmdrest\n", true, 1, 0},
        /* UTF-8 2-byte no comando (eco + saida). O literal e quebrado em duas
         * concatenacoes porque "\xA9d" seria lido como um unico escape hex de
         * tres digitos (\xA9D, fora de faixa) — quebra de codepoint, nao texto. */
        {"echo caf\xC3\xA9", "echo caf\xC3\xA9\ndone\n",
         "echo caf\xC3\xA9" "done\n", true, 1, 0},
        /* UTF-8 3-byte (Euro) e 4-byte (Fogo) no comando */
        {"preco \xE2\x82\xAC 10", "preco \xE2\x82\xAC 10\nok\n",
         "preco \xE2\x82\xAC 10" "ok\n", true, 1, 0},
        {"fire \xF0\x9F\x94\xA5", "fire \xF0\x9F\x94\xA5\nburn\n",
         "fire \xF0\x9F\x94\xA5" "burn\n", true, 1, 0},
    };
    for (const Case &c : cases) {
        // NAO-VACUIDADE estrutural: a sequencia esperada existe e o comando ou a
        // saida remota sobrevivem inteiros (sequencias nao vazias). Para comando
        // vazio o find() seria vacuo, entao a prova exige um LF real sobrevivente.
        CHECK(c.expected.size() > 0);
        if (!c.command.empty()) {
            CHECK(c.expected.find(c.command) != std::string::npos);
        } else {
            CHECK(c.expected.find('\n') != std::string::npos);
        }
        // Feed unico == feed fatiado.
        const std::string whole = partitioned(c.command, c.stream, c.stream.size(),
                                              c.expect_inactive_before_flush);
        CHECK_EQ(whole, c.expected);
        // REQ-SSH-01: contagem exata de LF real preservado, sem fabricacao e
        // sem engolimento, e as linhas em branco reais sobrevivem exatas.
        CHECK_EQ(count_occurrences(whole, "\n"), c.expected_lf);
        CHECK_EQ(count_occurrences(whole, "\n\n"), c.expected_blank_lines);
        const size_t steps[] = {1, 2, 3, 5, 7};
        for (size_t step : steps) {
            const std::string sliced = partitioned(c.command, c.stream, step,
                                                   c.expect_inactive_before_flush);
            CHECK_EQ(sliced, c.expected);
            CHECK_EQ(count_occurrences(sliced, "\n"), c.expected_lf);
            CHECK_EQ(count_occurrences(sliced, "\n\n"), c.expected_blank_lines);
        }
    }
}

/* Saida que comeca com '\n': o byte e-output remoto e preservado como
 * separador natural, sem nenhum byte extra; apos o eco completo, '\n' real do
 * remoto e preservado. */
void test_output_starting_with_newline()
{
    // Sem eco e saida comecando com '\n': o proprio byte e-output do remoto e
    // exibido uma vez e nenhum '\n' extra e injetado.
    cyberdeck_ssh_line_composer g;
    const std::string b = begin_ok(g, "cmd");
    CHECK_EQ(b, "cmd");
    const std::string f = feed_ok(g, "\nfirst line\n");
    CHECK_EQ(f, "\nfirst line\n");
    CHECK(g.active() == false);
    CHECK_EQ(b + f, "cmd\nfirst line\n");
    CHECK_EQ(count_occurrences(b + f, "\n"), 2);  /* os dois LFs do remoto */
    CHECK_EQ(count_occurrences(b + f, "\n\n"), 0);

    // O '\n' inicial chega sozinho (chunk de 1 byte): continua sendo o
    // separador; o restante passa como saida.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "cmd"), "cmd");
    CHECK_EQ(feed_ok(h, "\n"), "\n");
    CHECK(h.active() == false);
    CHECK_EQ(feed_ok(h, "rest\n"), "rest\n");

    // Eco completo SEGUIDO de '\n' real: a linha em branco e saida legitima
    // do remoto e nao pode ser engolida nem duplicada.
    cyberdeck_ssh_line_composer i;
    const std::string bi = begin_ok(i, "cmd");
    CHECK_EQ(bi, "cmd");
    const std::string fi = feed_ok(i, "cmd\n\nreal blank\n");
    CHECK_EQ(fi, "\nreal blank\n"); /* a quebra do eco foi com o eco */
    CHECK(i.active() == false);
    CHECK_EQ(bi + fi, "cmd\nreal blank\n");
    CHECK_EQ(count_occurrences(bi + fi, "\n"), 2);
}

/* Comando vazio (payload "\n"): o eco do '\n' e suprimido por inteiro e a
 * saida que vem depois passa verbatim, com ou sem LF real. */
void test_empty_command()
{
    // Eco do '\n' (o unico byte do payload): suprimido; a linha vazia nao
    // fabrica nenhuma quebra na faixa exibida.
    cyberdeck_ssh_line_composer g;
    CHECK_EQ(begin_ok(g, ""), "");
    CHECK_EQ(feed_ok(g, "\n"), "");
    CHECK(g.active() == false);

    // Eco do '\n' + saida no mesmo chunk.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, ""), "");
    CHECK_EQ(feed_ok(h, "\nnext prompt\n"), "next prompt\n");
    CHECK(h.active() == false);

    // Sem eco: a saida passa verbatim, sem LF acrescentado.
    cyberdeck_ssh_line_composer i;
    CHECK_EQ(begin_ok(i, ""), "");
    CHECK_EQ(feed_ok(i, "output\n"), "output\n");
    CHECK(i.active() == false);

    // Sem eco e sem saida: flush() nao inventa nada.
    cyberdeck_ssh_line_composer j;
    CHECK_EQ(begin_ok(j, ""), "");
    CHECK_EQ(flush_ok(j), "");
    CHECK(j.active() == false);

    // Feed nulo/vazio com comando vazio pendente: estado intacto.
    cyberdeck_ssh_line_composer k;
    CHECK_EQ(begin_ok(k, ""), "");
    CHECK(k.feed(nullptr, 0, nullptr, 0) == 0);
    CHECK(k.feed("", 0, nullptr, 0) == 0);
    CHECK(k.active() == true);
    CHECK_EQ(flush_ok(k), "");

    // NAO-VACUIDADE: um LF real do remoto continua visivel mesmo com o eco
    // vazio suprimido — o que prova que a supressao nao comeu LF legitimo.
    CHECK_EQ(compose("", {"\n"}), "");
    CHECK_EQ(compose("", {"\nnext prompt\n"}), "next prompt\n");
    CHECK_EQ(compose("", {"\n\nblank real\n"}), "\nblank real\n");
}

/* Multiplos comandos e reset: cada ativacao compoe uma linha independente e
 * somente apos a resolucao (eco/divergencia/flush) begin() e aceito de novo. */
void test_multiple_commands_and_reset()
{
    // Dois comandos sequenciais com eco e saida entre eles.
    cyberdeck_ssh_line_composer g;
    CHECK_EQ(begin_ok(g, "one"), "one");
    CHECK_EQ(feed_ok(g, "one\n"), "");
    CHECK(g.active() == false);
    CHECK_EQ(feed_ok(g, "r1\n"), "r1\n");

    CHECK_EQ(begin_ok(g, "two"), "two");
    CHECK_EQ(feed_ok(g, "two\nr2\n"), "r2\n");
    CHECK(g.active() == false);

    // flush() como reset apos eco incompleto: proximo comando arma normal.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "ab"), "ab");
    CHECK_EQ(feed_ok(h, "ab"), "");
    CHECK_EQ(flush_ok(h), "");
    CHECK(h.active() == false);
    CHECK_EQ(begin_ok(h, "cd"), "cd");
    CHECK_EQ(feed_ok(h, "cd\n"), "");
    CHECK(h.active() == false);

    // Comando vazio entre comandos nao corrompe o estado.
    CHECK_EQ(compose("x", {"x\nout\n"}), "xout\n");
    CHECK_EQ(compose("", {"\n"}), "");
    CHECK_EQ(compose("y", {"y\n"}), "y");

    // SERIALIZADO: begin() enquanto armado e recusado; apos o eco do comando
    // pendente, o compositor volta a IDLE e aceita o proximo begin().
    cyberdeck_ssh_line_composer i;
    CHECK_EQ(begin_ok(i, "cmd"), "cmd");
    CHECK_EQ(begin_ok(i, "other"), "");       /* recusado: nada escrito */
    CHECK(i.active() == true);
    CHECK_EQ(feed_ok(i, "cmd\n"), "");      /* eco do pendente */
    CHECK(i.active() == false);
    CHECK_EQ(begin_ok(i, "other"), "other");  /* aceito apos resolucao */
    CHECK_EQ(feed_ok(i, "other\n"), "");
    CHECK(i.active() == false);

    // flush(nullptr, 0) descarta a pendencia e rearma sem escrever nada.
    cyberdeck_ssh_line_composer j;
    CHECK_EQ(begin_ok(j, "x"), "x");
    CHECK_EQ(feed_ok(j, "x"), "");
    CHECK(j.flush(nullptr, 0) == 0);
    CHECK(j.active() == false);
    CHECK_EQ(begin_ok(j, "y"), "y");
    CHECK_EQ(feed_ok(j, "y\n"), "");
}

/* UTF-8: bytes altos comparados e emitidos byte a byte; um codepoint pode
 * ser fatiado entre chunks sem corromper a supressao do eco nem a saida. */
void test_utf8()
{
    // Eco UTF-8 fragmentado no meio do codepoint: suprimido, nada invented.
    cyberdeck_ssh_line_composer g;
    const std::string command = "echo caf\xC3\xA9"; /* 'e' acentuado = C3 A9 */
    CHECK_EQ(begin_ok(g, command), command);
    CHECK_EQ(feed_ok(g, "echo caf"), "");
    CHECK_EQ(feed_ok(g, "\xC3"), "");
    CHECK(g.active() == true);
    CHECK_EQ(feed_ok(g, "\xA9"), "");
    CHECK_EQ(feed_ok(g, "\n"), "");
    CHECK(g.active() == false);
    CHECK_EQ(feed_ok(g, "\xC3\xA9 ok\n"), "\xC3\xA9 ok\n");

    // Eco + saida UTF-8 no mesmo chunk.
    cyberdeck_ssh_line_composer h;
    CHECK_EQ(begin_ok(h, "caf\xC3\xA9"), "caf\xC3\xA9");
    CHECK_EQ(feed_ok(h, "caf\xC3\xA9\n\xE2\x82\xAC 10\n"), "\xE2\x82\xAC 10\n");
    CHECK(h.active() == false);

    // Divergencia no meio de um codepoint: prefixo retido + byte divergente
    // emitidos integralmente (nada e engolido, nada e acrescentado).
    cyberdeck_ssh_line_composer i;
    const std::string bi = begin_ok(i, "p\xC3\xA9");
    CHECK_EQ(bi, "p\xC3\xA9");
    const std::string fi = feed_ok(i, "p\xC3\xA8!"); /* diverge no 3º byte */
    CHECK_EQ(fi, "p\xC3\xA8!");
    CHECK(i.active() == false);
    CHECK_EQ(bi + fi, "p\xC3\xA9p\xC3\xA8!");

    // Codepoint de 4 bytes fatiado byte a byte no eco.
    cyberdeck_ssh_line_composer j;
    CHECK_EQ(begin_ok(j, "\xF0\x9F\x94\xA5"), "\xF0\x9F\x94\xA5");
    CHECK_EQ(feed_ok(j, "\xF0"), "");
    CHECK_EQ(feed_ok(j, "\x9F"), "");
    CHECK_EQ(feed_ok(j, "\x94"), "");
    CHECK_EQ(feed_ok(j, "\xA5"), "");
    CHECK_EQ(feed_ok(j, "\n"), "");
    CHECK(j.active() == false);
    CHECK_EQ(feed_ok(j, "\xF0\x9F\x94\xA5 flame\n"), "\xF0\x9F\x94\xA5 flame\n");
}

/* Enter virtual nao duplica: uma ativacao (begin) exibe o comando uma vez e
 * deixa o compositor em IDLE na MESMA chamada (sem janela de re-processamento). */
void test_virtual_enter_no_duplicate()
{
    // Eco completo + saida: IDLE imediato ao concluir o eco; comando UMA vez.
    cyberdeck_ssh_line_composer g;
    const std::string b = begin_ok(g, "cmd");
    CHECK_EQ(b, "cmd");
    const std::string f = feed_ok(g, "cmd\nresult\n");
    CHECK_EQ(f, "result\n");
    CHECK(g.active() == false); /* IDLE na mesma chamada: nada re-processavel */
    const std::string display = b + f;
    CHECK_EQ(display, "cmdresult\n");
    CHECK_EQ(count_occurrences(display, "cmd"), 1); /* comando UMA vez */
    CHECK_EQ(count_occurrences(display, "\n"), 1);  /* so o LF real da saida */

    // Dois Enters virtuais sequenciais (dois comandos iguais): cada comando
    // aparece exatamente uma vez e nenhum byte extra e inserido entre eles.
    cyberdeck_ssh_line_composer h;
    std::string d2 = begin_ok(h, "cmd");
    d2 += feed_ok(h, "cmd\n");
    d2 += begin_ok(h, "cmd");
    d2 += feed_ok(h, "cmd\n");
    CHECK_EQ(d2, "cmdcmd");
    CHECK_EQ(count_occurrences(d2, "cmd"), 2);
    CHECK_EQ(count_occurrences(d2, "\n"), 0);

    // Enter duplicado enquanto a linha ainda esta pendente: a segunda
    // ativacao e recusada (nada escrito), o eco da primeira e suprimido e a
    // faixa local nao duplica o comando nem fabrica quebra.
    cyberdeck_ssh_line_composer i;
    std::string di = begin_ok(i, "v");
    CHECK_EQ(di, "v");
    CHECK_EQ(begin_ok(i, "v"), ""); /* recusado */
    CHECK(i.active() == true);
    const std::string fi = feed_ok(i, "v\n");
    CHECK_EQ(fi, "");
    CHECK(i.active() == false);
    di += fi;
    CHECK_EQ(di, "v");
    CHECK_EQ(count_occurrences(di, "v"), 1);    /* comando UMA vez */
    CHECK_EQ(count_occurrences(di, "\n"), 0);   /* nenhuma quebra criada */
    // Depois de resolvida, o Enter seguinte e aceito normalmente.
    CHECK_EQ(begin_ok(i, "v"), "v");
    CHECK_EQ(feed_ok(i, "v\n"), "");
    CHECK(i.active() == false);

    // Eco tardio apos flush (linha ja resolvida): e saida legitima, o
    // compositor nunca duplica estado.
    cyberdeck_ssh_line_composer j;
    CHECK_EQ(begin_ok(j, "cmd"), "cmd");
    CHECK_EQ(feed_ok(j, "cmd"), "");
    CHECK_EQ(flush_ok(j), "");
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
    CHECK_EQ(feed_ok(c, "cmd\n"), "");
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
    CHECK_EQ(feed_ok(c, "\n"), "");         /* completa: eco inteiro suprimido */
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
    CHECK_EQ(feed_ok(h, "\n"), "");
    CHECK(h.active() == false);

    // Divergencia do pendente apos recusa: saida integral, sem engolir bytes
    // e sem acrescentar nenhum.
    cyberdeck_ssh_line_composer i;
    CHECK_EQ(begin_ok(i, "cmd"), "cmd");
    CHECK_EQ(begin_ok(i, "zzz"), "");         /* recusado */
    CHECK_EQ(feed_ok(i, "cmX\n"), "cmX\n");   /* prefixo retido + divergente */
    CHECK(i.active() == false);
    CHECK_EQ(begin_ok(i, "after"), "after");  /* aceito apos divergencia */
    CHECK_EQ(feed_ok(i, "after\n"), "");
    CHECK(i.active() == false);
}

/* SERIALIZADO: apos a pendencia ser resolvida por eco completo ou por
 * divergencia, begin() volta a ser aceito (estado IDLE). */
void test_begin_accepted_after_resolution()
{
    // Apos eco completo.
    cyberdeck_ssh_line_composer c;
    CHECK_EQ(begin_ok(c, "one"), "one");
    CHECK_EQ(feed_ok(c, "one\n"), "");
    CHECK(c.active() == false);
    CHECK_EQ(begin_ok(c, "two"), "two");
    CHECK(c.active() == true);
    CHECK_EQ(feed_ok(c, "two\n"), "");
    CHECK(c.active() == false);

    // Apos divergencia (sem eco).
    cyberdeck_ssh_line_composer d;
    CHECK_EQ(begin_ok(d, "ab"), "ab");
    CHECK_EQ(feed_ok(d, "XY"), "XY"); /* divergencia k=0: sem prefixo retido */
    CHECK(d.active() == false);
    CHECK_EQ(begin_ok(d, "cd"), "cd");
    CHECK(d.active() == true);
    CHECK_EQ(feed_ok(d, "cd\n"), "");
    CHECK(d.active() == false);

    // Apos divergencia cujo primeiro byte e '\n' (o byte remoto e preservado).
    cyberdeck_ssh_line_composer e;
    CHECK_EQ(begin_ok(e, "ab"), "ab");
    CHECK_EQ(feed_ok(e, "\nout\n"), "\nout\n");
    CHECK(e.active() == false);
    CHECK_EQ(begin_ok(e, "cd"), "cd");
    CHECK_EQ(feed_ok(e, "cd\n"), "");
    CHECK(e.active() == false);
}

/* SERIALIZADO: apos flush() (inclusive o reset que descarta a pendencia),
 * begin() e aceito para o proximo comando. */
void test_begin_accepted_after_flush()
{
    // flush() apos eco incompleto: rearme sem byte algum.
    cyberdeck_ssh_line_composer c;
    CHECK_EQ(begin_ok(c, "cmd"), "cmd");
    CHECK_EQ(feed_ok(c, "cm"), "");
    CHECK_EQ(flush_ok(c), "");
    CHECK(c.active() == false);
    CHECK_EQ(begin_ok(c, "next"), "next");
    CHECK_EQ(feed_ok(c, "next\n"), "");
    CHECK(c.active() == false);

    // flush(nullptr, 0) descarta a pendencia e rearma sem escrever nada.
    cyberdeck_ssh_line_composer d;
    CHECK_EQ(begin_ok(d, "cmd"), "cmd");
    CHECK_EQ(feed_ok(d, "cmd"), "");
    CHECK(d.flush(nullptr, 0) == 0);
    CHECK(d.active() == false);
    CHECK_EQ(begin_ok(d, "next"), "next");
    CHECK_EQ(feed_ok(d, "next\n"), "");
    CHECK(d.active() == false);
}

/* REQ-SSH-01: flush() da UNICA pendencia nunca escreve byte algum, em nenhuma
 * capacidade, e ainda assim resolve a pendencia e e idempotente em IDLE. */
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

    // Nova pendencia: nem cap minimo nem cap folgado escrevem alguma coisa —
    // o canario 0xAA prova a nao-escrita byte a byte.
    CHECK_EQ(begin_ok(c, "y"), "y");
    std::string raw;
    CHECK_EQ(flush_canary(c, 1, &raw), static_cast<size_t>(0));
    CHECK_EQ(raw, std::string(1, '\xAA'));
    CHECK(c.active() == false);

    CHECK_EQ(begin_ok(c, "y"), "y");
    CHECK_EQ(flush_canary(c, 64, &raw), static_cast<size_t>(0));
    CHECK_EQ(raw, std::string(64, '\xAA'));
    CHECK(c.active() == false);

    // Pendencia ja resolvida por eco completo: flush permanece um no-op.
    CHECK_EQ(begin_ok(c, "z"), "z");
    CHECK_EQ(feed_ok(c, "z\n"), "");
    CHECK(c.active() == false);
    CHECK_EQ(flush_canary(c, 8, &raw), static_cast<size_t>(0));
    CHECK_EQ(raw, std::string(8, '\xAA'));

    // out == nullptr com capacidade sobrada tambem nao escreve nem inventa.
    CHECK_EQ(begin_ok(c, "w"), "w");
    CHECK(c.flush(nullptr, 64) == 0);
    CHECK(c.active() == false);
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
    CHECK_EQ(feed_ok(g, "d\n"), "");
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

/* REQ-SSH-01/AC-SSH-01 + REQ-001/AC-001 + REQ-003: nao existe marcador local da
 * linha em SSH, portanto o compositor nao conhece nenhum prompt.  O prompt remoto
 * chega pelo fluxo remoto e e opaco para a regra de eco: como ele diverge do
 * payload enviado (divergencia em k == 0), e emitido byte a byte VERBATIM, sem
 * nenhum LF antes dele e sem ser suprimido.  Isso impede a regressao em que a
 * supressao de eco passasse a casar um prompt sintetizado e engolisse a saida
 * legitima do remoto, e tambem a regressao em que o compositor voltasse a
 * fabricar um '\n' antes de qualquer saida remota. */
void test_remote_prompt_never_matches_the_echo_guard()
{
    /* Prompt tipico do host remoto logo apos o comando: divergencia em k == 0,
     * bytes remotos integralmente, ZERO byte acrescentado.  begin() e feed()
     * sao sequenciados em linhas separadas porque ambos mutam o compositor e
     * a ordem de avaliacao dos operandos de '+' nao e definida em C++. */
    cyberdeck_ssh_line_composer typed;
    const std::string band_typed = begin_ok(typed, "pwd");
    const std::string prompt_typed = feed_ok(typed, "user@host:~$ ");
    CHECK_EQ(prompt_typed, "user@host:~$ ");
    const std::string band_typed_full = band_typed + prompt_typed;
    CHECK_EQ(band_typed_full, "pwduser@host:~$ ");
    CHECK(typed.active() == false);
    /* NAO-VACUIDADE: o prompt chegou inteiro, uma vez, e nenhum '\n' foi
     * fabricado para "separar" comando e prompt remoto. */
    CHECK_EQ(count_occurrences(band_typed_full, "user@host:~$ "), 1);
    CHECK_EQ(count_occurrences(band_typed_full, "\n"), 0);

    /* O eco real do comando passa direto, porque a pendencia ja foi resolvida
     * pela divergencia e o compositor nunca conheceu o prompt. */
    CHECK_EQ(feed_ok(typed, "pwd\n"), "pwd\n");

    /* Prompt remoto com a grafia que o deck usava antes continua sendo texto
     * remoto ordinario: nao casa com o payload, nao e engolido e nao ganha
     * quebra. */
    cyberdeck_ssh_line_composer legacy_grafia;
    CHECK_EQ(begin_ok(legacy_grafia, "pwd"), "pwd");
    CHECK_EQ(feed_ok(legacy_grafia, "ssh> "), "ssh> ");
    CHECK_EQ(count_occurrences("ssh> ", "\n"), 0);
    CHECK(legacy_grafia.active() == false);

    /* Ausencia de prompt: a saida remota entra verbatim e o prompt so reaparece
     * no fluxo seguinte, integral e sem LF inventado. */
    cyberdeck_ssh_line_composer sem_prompt;
    CHECK_EQ(begin_ok(sem_prompt, "pwd"), "pwd");
    CHECK_EQ(feed_ok(sem_prompt, "/home/user\n"), "/home/user\n");
    const std::string band_prompt = feed_ok(sem_prompt, "user@host:~$ ");
    CHECK_EQ(band_prompt, "user@host:~$ ");
    CHECK_EQ(count_occurrences(band_prompt, "\n"), 0);

    /* Fragmentacao nao muda o resultado: um prompt remoto cortado byte a byte
     * continua divergindo do payload e sendo emitido integralmente, sem LF. */
    cyberdeck_ssh_line_composer fragmented;
    CHECK_EQ(begin_ok(fragmented, "pwd"), "pwd");
    std::string out;
    const std::string remote = "user@host:~$ ";
    for (std::size_t i = 0; i < remote.size(); ++i) {
        out += feed_ok(fragmented, remote.substr(i, 1));
    }
    CHECK_EQ(out, remote);
    CHECK_EQ(count_occurrences(out, "\n"), 0);
    CHECK(fragmented.active() == false);

    /* Um comando cujo prefixo coincide com o prompt remoto nao pode ser
     * confundido com ele: o eco exato do payload ainda e suprimido e a
     * divergencia libera prefixo + byte divergente, sem separador nenhum.
     * begin() e feed() sao sequenciados em linhas separadas (mutam o
     * mesmo compositor). */
    cyberdeck_ssh_line_composer prefixo;
    const std::string band_prefixo_a = begin_ok(prefixo, "user@host");
    const std::string band_prefixo_b = feed_ok(prefixo, "user@host:~$ ");
    const std::string band_prefixo = band_prefixo_a + band_prefixo_b;
    CHECK_EQ(band_prefixo, "user@hostuser@host:~$ ");
    CHECK_EQ(count_occurrences(band_prefixo, "user@host:~$ "), 1);
    CHECK_EQ(count_occurrences(band_prefixo, "\n"), 0);
    CHECK(prefixo.active() == false);

    /* REQ-003: nada do que o compositor devolve volta para o payload.  O
     * begin() seguinte nao carrega o prompt anterior e o eco do proximo comando
     * continua devolvendo zero bytes. */
    cyberdeck_ssh_line_composer separador;
    CHECK_EQ(begin_ok(separador, "pwd"), "pwd");
    CHECK_EQ(feed_ok(separador, "user@host:~$ "), "user@host:~$ ");
    CHECK_EQ(begin_ok(separador, "ls"), "ls");
    CHECK_EQ(feed_ok(separador, "ls\n"), "");
    CHECK(separador.active() == false);
}

} // namespace

int main()
{
    test_begin_displays_command_without_newline();
    test_echo_suppressed_without_artificial_lf();
    test_no_echo_output_is_verbatim();
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
    test_remote_prompt_never_matches_the_echo_guard();

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_ssh_line_composer (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
