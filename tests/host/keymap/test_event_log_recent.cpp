/*
 * Testes unitarios host-side para a selecao de indices do anel "recent" do
 * log de eventos (contrato em event_log_recent.h; logica pura extraida de
 * event_log_latest()/event_log.cpp, sem FreeRTOS, LVGL ou SD).
 *
 * Objetivo da extracao (plano aprovado): o comando `log` estoura a stack da
 * UI porque event_log_latest() copia LogRecord records[RECENT_COUNT] (10 x 256
 * bytes) + formata uma linha por record no mesmo frame. A funcao pura abaixo
 * seleciona apenas OS INDICES do anel (oldest -> newest) permitindo que a
 * implementacao de producao itere registro a registro, sem copiar o anel.
 *
 * Contrato sob teste (event_log_recent.h):
 *   - recent_count em 0..EVENT_LOG_RECENT_CAPACITY (10, paridade com
 *     RECENT_COUNT de event_log.cpp via static_assert);
 *   - max_events (count) == 0 -> retorna 0 e nao escreve nada (paridade com
 *     event_log_latest(max_events = 0));
 *   - count < recent_count -> apenas os `count` mais recentes, descartando os
 *     mais antigos; count > recent_count -> clamp para recent_count;
 *   - next com wrap-around: indices sao (out[0] + i) % capacity, todos em
 *     [0, capacity) — NENHUM indice fora do anel e produzido;
 *   - ordem cronologica: mais antigo -> mais recente;
 *   - estados invalidos (capacity == 0, recent_count > capacity,
 *     next >= capacity, out_indices == nullptr) -> retorno 0 deterministico,
 *     buffer intacto (funcao total e segura);
 *   - nenhuma escrita alem de n = min(count, recent_count) (canario).
 *
 * A verificacao usa um oraculo INDEPENDENTE (simulacao do layout do anel a
 * partir do estado recent_count/next/capacity, sem reutilizar a formula de
 * selecao), evitando teste tautologico.
 *
 * A implementacao e compilada junto ao teste; a suite permanece independente
 * de FreeRTOS, LVGL e acesso ao SD.
 *
 * Build: make test (veja Makefile; sem dependencias alem de g++/make).
 */
#include "platform/logging/event_log_recent.h"

#include <algorithm>
#include <cstdio>
#include <vector>

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

#define CHECK_EQ_SIZE(actual, expected)                                                \
    do {                                                                               \
        ++s_checks;                                                                    \
        const size_t a_ = static_cast<size_t>(actual);                                 \
        const size_t e_ = static_cast<size_t>(expected);                               \
        if (a_ != e_) {                                                                \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  CHECK_EQ_SIZE(%s): expected %zu, actual %zu\n",   \
                        __FILE__, __LINE__, #actual, e_, a_);                          \
        }                                                                              \
    } while (0)

/* Capacidade especificada pelo plano e pela producao (RECENT_COUNT = 10 em
 * event_log.cpp). O literal e o contrato: se a producao mudar, este
 * static_assert quebra o build e forcara revisao consciente. */
constexpr size_t kSpecCapacity = 10;
static_assert(EVENT_LOG_RECENT_CAPACITY == kSpecCapacity,
              "RECENT_COUNT de event_log.cpp mudou: reveja o contrato e atualize os testes");

/* Canario: slots extras alem de capacity para detectar escrita fora do
 * contrato (alem de n). */
constexpr size_t kCanarySlots = 4;
constexpr size_t kNoSeq = static_cast<size_t>(-1);
constexpr size_t kSentinel = static_cast<size_t>(0xDEADBEEFDEADBEEFULL);

void fill_sentinel(size_t *buf, size_t len)
{
    std::fill(buf, buf + len, kSentinel);
}

bool suffix_untouched(const size_t *buf, size_t start, size_t len)
{
    for (size_t i = start; i < len; ++i) {
        if (buf[i] != kSentinel) {
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------- oraculo
 *
 * Constrói o layout do anel a partir do estado (recent_count, next,
 * capacity) simulando as escritas sequenciais de remember_record(): o ultimo
 * registro escrito vive em (next - 1 + capacity) % capacity e recebe a maior
 * seq (recent_count - 1); caminhando para tras no anel as seqs decrescem ate
 * 0 (mais antigo). Não reutiliza a formula de selecao do contrato.
 */
std::vector<size_t> ring_seq_map(size_t recent_count, size_t next, size_t capacity)
{
    std::vector<size_t> seq_map(capacity, kNoSeq);
    if (recent_count == 0 || capacity == 0) {
        return seq_map;
    }
    size_t slot = (next + capacity - 1) % capacity; /* slot do mais recente */
    for (size_t seq = recent_count; seq > 0; --seq) {
        seq_map[slot] = seq - 1;
        slot = (slot + capacity - 1) % capacity;
    }
    return seq_map;
}

/* Slots esperados para os `n` eventos mais recentes, em ordem cronologica
 * (mais antigo -> mais recente): os ultimos n slots da ordem cronologica
 * completa do anel. Requer n <= recent_count. */
std::vector<size_t> expected_indices(size_t recent_count, size_t next, size_t capacity,
                                     size_t n)
{
    std::vector<size_t> result;
    if (recent_count == 0 || n == 0 || capacity == 0) {
        return result;
    }
    const std::vector<size_t> seq_map = ring_seq_map(recent_count, next, capacity);
    size_t oldest = capacity;
    for (size_t i = 0; i < capacity; ++i) {
        if (seq_map[i] == 0) {
            oldest = i;
            break;
        }
    }
    if (oldest == capacity) {
        return result; /* recent_count <= capacity garante que seq 0 existe */
    }
    std::vector<size_t> chrono;
    chrono.reserve(recent_count);
    size_t slot = oldest;
    for (size_t i = 0; i < recent_count; ++i) {
        chrono.push_back(slot);
        slot = (slot + 1) % capacity;
    }
    for (size_t i = recent_count - n; i < recent_count; ++i) {
        result.push_back(chrono[i]);
    }
    return result;
}

/* ------------------------------------------------------------ verificacao
 *
 * Roda a funcao sob teste com buffer canario e valida o contrato completo:
 *   - retorno == min(count, recent_count);
 *   - sequencia de indices identica ao oraculo (ordem cronologica correta);
 *   - todos os indices em [0, capacity);
 *   - continuidade com wrap: out[i] == (out[0] + i) % capacity;
 *   - nenhuma escrita alem de n (canario intacto).
 *
 * capacity default = producao (10); aceita valores menores para o teste de
 * generalizacao do parametro.
 */
void verify_state(size_t recent_count, size_t next, size_t count,
                  size_t capacity = kSpecCapacity)
{
    size_t buf[kSpecCapacity + kCanarySlots];
    fill_sentinel(buf, kSpecCapacity + kCanarySlots);

    const size_t n = event_log_recent_indices(recent_count, next, count, capacity, buf);
    const size_t expected_n = count < recent_count ? count : recent_count;
    CHECK_EQ_SIZE(n, expected_n);

    const std::vector<size_t> expected = expected_indices(recent_count, next, capacity, n);
    CHECK_EQ_SIZE(expected.size(), n);
    for (size_t i = 0; i < n; ++i) {
        CHECK_EQ_SIZE(buf[i], expected[i]);
        CHECK(buf[i] < capacity);
        if (i > 0) {
            CHECK_EQ_SIZE(buf[i], (buf[0] + i) % capacity);
        }
    }
    /* Nenhuma escrita alem de n (canario preservado). */
    CHECK(suffix_untouched(buf, n, kSpecCapacity + kCanarySlots));
}

/* ----------------------------------------------------------------- testes */

void test_capacity_parity()
{
    // Capacidade fixada pelo plano/producao (10). O static_assert acima ja
    // pina o contrato; este CHECK registra o valor em runtime tambem.
    CHECK(EVENT_LOG_RECENT_CAPACITY == 10);
}

void test_empty_ring_returns_zero()
{
    // recent_count == 0 para todo next valido e qualquer count: retorna 0 e
    // nao escreve nada.
    for (size_t next = 0; next < kSpecCapacity; ++next) {
        const size_t counts[] = {0, 1, 3, kSpecCapacity, kSpecCapacity + 1};
        for (size_t count : counts) {
            size_t buf[kSpecCapacity + kCanarySlots];
            fill_sentinel(buf, kSpecCapacity + kCanarySlots);
            CHECK_EQ_SIZE(event_log_recent_indices(0, next, count, kSpecCapacity, buf), 0);
            CHECK(suffix_untouched(buf, 0, kSpecCapacity + kCanarySlots));
        }
    }
}

void test_max_events_zero_returns_zero_and_writes_nothing()
{
    // count == 0 (max_events == 0): paridade com event_log_latest(0), que
    // retorna 0 sem tocar no anel. Vale para aneis validos e cheios.
    for (size_t recent_count = 1; recent_count <= kSpecCapacity; ++recent_count) {
        const size_t nexts[] = {0, 3, kSpecCapacity - 1};
        for (size_t next : nexts) {
            size_t buf[kSpecCapacity + kCanarySlots];
            fill_sentinel(buf, kSpecCapacity + kCanarySlots);
            CHECK_EQ_SIZE(
                event_log_recent_indices(recent_count, next, 0, kSpecCapacity, buf), 0);
            CHECK(suffix_untouched(buf, 0, kSpecCapacity + kCanarySlots));
        }
    }
}

void test_max_events_equals_recent_count_returns_all()
{
    // count == recent_count: todos os eventos validos, pondos os indices
    // mais antigos -> mais recentes, sem descarte.
    for (size_t recent_count = 1; recent_count <= kSpecCapacity; ++recent_count) {
        const size_t nexts[] = {0, 5, kSpecCapacity - 1};
        for (size_t next : nexts) {
            verify_state(recent_count, next, recent_count);
        }
    }
    // Casos explicitos para documentacao:
    // recent_count 3, next 0: entradas nos slots 7,8,9.
    {
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(3, 0, 3, kSpecCapacity, buf), 3);
        CHECK_EQ_SIZE(buf[0], 7);
        CHECK_EQ_SIZE(buf[1], 8);
        CHECK_EQ_SIZE(buf[2], 9);
    }
    // recent_count 3, next 7 (wrap): entradas nos slots 4,5,6.
    {
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(3, 7, 3, kSpecCapacity, buf), 3);
        CHECK_EQ_SIZE(buf[0], 4);
        CHECK_EQ_SIZE(buf[1], 5);
        CHECK_EQ_SIZE(buf[2], 6);
    }
}

void test_max_events_exceeds_recent_count_clamps()
{
    // count > recent_count: clamp para recent_count (todos os validos).
    verify_state(3, 0, kSpecCapacity);
    verify_state(3, 7, kSpecCapacity + 1);
    verify_state(3, 9, kSpecCapacity + 50); /* count muito maior */
    // Anel cheio com count acima da capacidade: n == recent_count.
    verify_state(kSpecCapacity, 0, kSpecCapacity + 5);
    verify_state(kSpecCapacity, 9, kSpecCapacity + 5);
    // Caso explicito: recent_count 3, next 9 (entradas 6,7,8) com count 60.
    {
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(3, 9, 60, kSpecCapacity, buf), 3);
        CHECK_EQ_SIZE(buf[0], 6);
        CHECK_EQ_SIZE(buf[1], 7);
        CHECK_EQ_SIZE(buf[2], 8);
    }
}

void test_max_events_less_than_recent_count_keeps_most_recent()
{
    // count < recent_count: apenas os `count` mais recentes, descartando os
    // mais antigos, preservando a ordem cronologica dos selecionados.
    // Anel cheio (10), next 0, count 3: os 3 mais recentes = slots 7,8,9.
    verify_state(kSpecCapacity, 0, 3);
    {
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(kSpecCapacity, 0, 3, kSpecCapacity, buf), 3);
        CHECK_EQ_SIZE(buf[0], 7);
        CHECK_EQ_SIZE(buf[1], 8);
        CHECK_EQ_SIZE(buf[2], 9);
    }
    // Anel parcial (5), next 8 (entradas 3,4,5,6,7), count 2: 6,7.
    verify_state(5, 8, 2);
    {
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(5, 8, 2, kSpecCapacity, buf), 2);
        CHECK_EQ_SIZE(buf[0], 6);
        CHECK_EQ_SIZE(buf[1], 7);
    }
    // Anel cheio com wrap: next 3, count 4 -> 9,0,1,2 (cruzando o wrap).
    verify_state(kSpecCapacity, 3, 4);
    {
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(kSpecCapacity, 3, 4, kSpecCapacity, buf), 4);
        CHECK_EQ_SIZE(buf[0], 9);
        CHECK_EQ_SIZE(buf[1], 0);
        CHECK_EQ_SIZE(buf[2], 1);
        CHECK_EQ_SIZE(buf[3], 2);
    }
}

void test_single_record_returns_its_slot()
{
    // recent_count 1: o unico registro vive sempre em (next - 1) % capacity.
    for (size_t next = 0; next < kSpecCapacity; ++next) {
        verify_state(1, next, 1);
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(1, next, 1, kSpecCapacity, buf), 1);
        CHECK_EQ_SIZE(buf[0], (next + kSpecCapacity - 1) % kSpecCapacity);
    }
}

void test_full_ring_next_wraps_all_positions()
{
    // Anel cheio (recent_count == capacity): a ordem cronologica inteira e
    // um ciclo completo com wrap; o indice inicial e `next` (o mais antigo).
    for (size_t next = 0; next < kSpecCapacity; ++next) {
        verify_state(kSpecCapacity, next, kSpecCapacity);
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(
            event_log_recent_indices(kSpecCapacity, next, kSpecCapacity, kSpecCapacity, buf),
            kSpecCapacity);
        for (size_t i = 0; i < kSpecCapacity; ++i) {
            CHECK_EQ_SIZE(buf[i], (next + i) % kSpecCapacity);
        }
    }
}

void test_wrap_around_partial_ring()
{
    // recent_count 6, next 2: entradas 6,7,8,9,0,1 (mais antigo 6).
    verify_state(6, 2, 6);
    verify_state(6, 2, 3); /* 3 mais recentes: 9,0,1 (cruzando o wrap) */
    {
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(6, 2, 3, kSpecCapacity, buf), 3);
        CHECK_EQ_SIZE(buf[0], 9);
        CHECK_EQ_SIZE(buf[1], 0);
        CHECK_EQ_SIZE(buf[2], 1);
    }
    // recent_count 6, next 9: entradas 3,4,5,6,7,8.
    verify_state(6, 9, 6);
    verify_state(6, 9, 2);
    {
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(6, 9, 2, kSpecCapacity, buf), 2);
        CHECK_EQ_SIZE(buf[0], 7);
        CHECK_EQ_SIZE(buf[1], 8);
    }
    // recent_count 5, next 1: entradas 6,7,8,9,0; 2 mais recentes: 9,0.
    verify_state(5, 1, 5);
    verify_state(5, 1, 2);
    {
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(5, 1, 2, kSpecCapacity, buf), 2);
        CHECK_EQ_SIZE(buf[0], 9);
        CHECK_EQ_SIZE(buf[1], 0);
    }
}

void test_most_recent_n_semantics()
{
    /* Semantica: os indices retornados mapeiam (via layout simulado) para as
     * seqs recent_count - n .. recent_count - 1 — ou seja, exatamente os n
     * EVENTOS MAIS RECENTES, em ordem cronologica de escrita. */
    const struct {
        size_t recent_count;
        size_t next;
        size_t count;
    } states[] = {
        {10, 0, 3},  {10, 3, 4},  {6, 2, 3}, {6, 9, 6},
        {5, 1, 2},   {3, 7, 1},   {3, 9, 2}, {8, 8, 8},
    };
    for (const auto &st : states) {
        size_t buf[kSpecCapacity];
        const size_t n =
            event_log_recent_indices(st.recent_count, st.next, st.count, kSpecCapacity, buf);
        const size_t expected_n = st.count < st.recent_count ? st.count : st.recent_count;
        CHECK_EQ_SIZE(n, expected_n);
        if (n == 0) {
            continue;
        }
        const std::vector<size_t> seq_map =
            ring_seq_map(st.recent_count, st.next, kSpecCapacity);
        /* Maior seq presente corresponde ao mais recente (deve ser a mais
         * alta do anel: recent_count - 1). */
        CHECK_EQ_SIZE(seq_map[buf[n - 1]], st.recent_count - 1);
        /* Primeiro retornado = evento mais antigo entre os selecionados. */
        CHECK_EQ_SIZE(seq_map[buf[0]], st.recent_count - n);
        /* Ordem cronologica estrita: seq cresce de 1 em 1. */
        for (size_t i = 1; i < n; ++i) {
            CHECK_EQ_SIZE(seq_map[buf[i]], seq_map[buf[i - 1]] + 1);
        }
    }
}

void test_sweep_all_valid_states()
{
    /* Varredura exaustiva dos estados validos (recent_count 0..10, next 0..9,
     * count 0..15): o contrato inteiro — retorno, ordem, wrap, limites e
     * ausencia de escrita fora de n — vale para todas as combinacoes. */
    for (size_t recent_count = 0; recent_count <= kSpecCapacity; ++recent_count) {
        for (size_t next = 0; next < kSpecCapacity; ++next) {
            for (size_t count = 0; count <= kSpecCapacity + 5; ++count) {
                verify_state(recent_count, next, count);
            }
        }
    }
}

void test_invalid_inputs_return_zero_and_write_nothing()
{
    size_t buf[kSpecCapacity + kCanarySlots];
    fill_sentinel(buf, kSpecCapacity + kCanarySlots);

    // out_indices == nullptr: retorno 0, sem escrita.
    CHECK_EQ_SIZE(event_log_recent_indices(5, 0, 3, kSpecCapacity, nullptr), 0);
    CHECK_EQ_SIZE(event_log_recent_indices(0, 0, 3, kSpecCapacity, nullptr), 0);

    // capacity == 0: nao ha anel; retorno 0 deterministico.
    CHECK_EQ_SIZE(event_log_recent_indices(0, 0, 3, 0, buf), 0);
    CHECK_EQ_SIZE(event_log_recent_indices(5, 0, 3, 0, buf), 0);

    // recent_count > capacity: estado invalido (violacao de invariante).
    CHECK_EQ_SIZE(event_log_recent_indices(kSpecCapacity + 1, 0, 5, kSpecCapacity, buf), 0);
    CHECK_EQ_SIZE(event_log_recent_indices(kSpecCapacity + 10, 9, 8, kSpecCapacity, buf), 0);

    // next >= capacity com anel nao vazio: estado invalido.
    CHECK_EQ_SIZE(event_log_recent_indices(3, kSpecCapacity, 3, kSpecCapacity, buf), 0);
    CHECK_EQ_SIZE(event_log_recent_indices(3, kSpecCapacity + 1, 3, kSpecCapacity, buf), 0);
    CHECK_EQ_SIZE(event_log_recent_indices(kSpecCapacity, 2 * kSpecCapacity, 9, kSpecCapacity, buf), 0);

    // Combinacao de invalidos: tambem 0.
    CHECK_EQ_SIZE(event_log_recent_indices(kSpecCapacity + 1, kSpecCapacity, 3, kSpecCapacity, buf), 0);

    // next >= capacity com anel vazio: recent_count == 0 domina -> 0.
    CHECK_EQ_SIZE(event_log_recent_indices(0, kSpecCapacity, 5, kSpecCapacity, buf), 0);

    // Nenhum caso invalido escreve no buffer.
    CHECK(suffix_untouched(buf, 0, kSpecCapacity + kCanarySlots));
}

void test_honors_capacity_parameter()
{
    // A funcao respeita o parametro capacity (nao embute a constante 10):
    // capacity 3, anel cheio -> 0,1,2; parcial com wrap -> indice >= 0 < 3.
    verify_state(3, 0, 5, 3);
    verify_state(2, 2, 1, 3);
    verify_state(1, 2, 9, 3);
    verify_state(1, 0, 1, 1); /* capacity minima util */
    verify_state(0, 0, 5, 1);
    {
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(3, 0, 5, 3, buf), 3);
        CHECK_EQ_SIZE(buf[0], 0);
        CHECK_EQ_SIZE(buf[1], 1);
        CHECK_EQ_SIZE(buf[2], 2);
    }
    {
        size_t buf[kSpecCapacity];
        CHECK_EQ_SIZE(event_log_recent_indices(2, 2, 1, 3, buf), 1);
        CHECK_EQ_SIZE(buf[0], 1); /* mais recente = slot (2 - 1) % 3 */
    }
}

} // namespace

int main()
{
    test_capacity_parity();
    test_empty_ring_returns_zero();
    test_max_events_zero_returns_zero_and_writes_nothing();
    test_max_events_equals_recent_count_returns_all();
    test_max_events_exceeds_recent_count_clamps();
    test_max_events_less_than_recent_count_keeps_most_recent();
    test_single_record_returns_its_slot();
    test_full_ring_next_wraps_all_positions();
    test_wrap_around_partial_ring();
    test_most_recent_n_semantics();
    test_sweep_all_valid_states();
    test_invalid_inputs_return_zero_and_write_nothing();
    test_honors_capacity_parameter();

    if (s_failures == 0) {
        std::printf("PASS: event_log_recent (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
