/*
 * Testes unitarios host-side para o relogio do header em GMT-3 (contrato em
 * cyberdeck_clock.h).
 *
 * Plano aprovado (recorte sob teste):
 *   - header com: "CYBERDECK5" a esquerda, relogio "DD/MM/YYYY HH:MM" no
 *     centro em GMT-3 (fuso fixo, sem DST) e indicador de Wi-Fi a direita;
 *   - NENHUM status SSH no header;
 *   - SEM comando de timezone e SEM mudanca nos logs (os timestamps do log em
 *     event_log.cpp continuam via localtime_r/strftime, fora do escopo deste
 *     formatador);
 *   - a logica pura do relogio vive em cyberdeck_clock.{h,cpp}: conversao
 *     UTC -> local (GMT-3 = -180 minutos) e formatacao "DD/MM/YYYY HH:MM".
 *
 * Contrato sob teste (o coder cria components/cyberdeck/include/
 * cyberdeck_clock.h EXATAMENTE com este formato; detalhado abaixo):
 *
 *   typedef struct cyberdeck_clock_time {
 *       int16_t year;   // 1..9999 (4 digitos)
 *       uint8_t month;  // 1..12
 *       uint8_t day;    // 1..dias do mes (fevereiro ciente de bissexto)
 *       uint8_t hour;   // 0..23
 *       uint8_t minute; // 0..59
 *   } cyberdeck_clock_time_t;
 *
 *   #define CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN (-180)
 *
 *   bool cyberdeck_clock_from_utc(const cyberdeck_clock_time_t *utc,
 *                                 int32_t offset_minutes,
 *                                 cyberdeck_clock_time_t *out);
 *   size_t cyberdeck_format_clock(char *buf, size_t cap,
 *                                 const cyberdeck_clock_time_t *clock);
 *
 * Semantica:
 *   - from_utc(): valida utc (NULL, calendario real no ano 1..9999, dia
 *     valido do mes, hora 0..23, minuto 0..59) e calcula
 *     local = utc + offset_minutes. Retorna false e NAO toca em out quando
 *     utc/out == NULL, utc invalido OU o resultado cai fora do ano 1..9999.
 *   - format_clock(): estilo snprintf (igual a
 *     cyberdeck_format_wifi_indicator): retorna o comprimento da string
 *     COMPLETA "DD/MM/YYYY HH:MM" (16 chars, sem NUL), independente de cap
 *     (truncacao detectavel por retorno > cap - 1). Entrada VALIDA:
 *     cap == 0 nao escreve (buf pode ser NULL); buf == NULL com cap > 0 nao
 *     escreve e retorna 16; cap > 0 escreve min(16, cap - 1) bytes + NUL em
 *     buf[min(16, cap-1)]. Entrada INVALIDA (NULL ou qualquer campo fora do
 *     calendario/faixa): retorna 0 e NAO escreve NADA. Funcao total e
 *     deterministica.
 *
 * A verificacao usa um ORACULO independente (snprintf propria, sem reutilizar
 * a logica condicional da implementacao) e canarios de sentinela para
 * detectar escrita indevida/overflow.
 *
 * A implementacao (cyberdeck_clock.cpp) e criada pelo coder junto com a
 * integracao do relogio na UI; esta suite fixa o contrato antes da
 * implementacao (TDD). O target so compila quando o modulo existir.
 *
 * Build: make test (veja Makefile; sem dependencias alem de g++/make).
 */
#include "platform/display/cyberdeck_clock.h"

#include <algorithm>
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

#define CHECK_EQ_INT(actual, expected)                                                 \
    do {                                                                               \
        ++s_checks;                                                                    \
        const long a_ = static_cast<long>(actual);                                     \
        const long e_ = static_cast<long>(expected);                                   \
        if (a_ != e_) {                                                                \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  CHECK_EQ_INT(%s): expected %ld, actual %ld\n",    \
                        __FILE__, __LINE__, #actual, e_, a_);                          \
        }                                                                              \
    } while (0)

/* Constantes do contrato fixadas em tempo de compilacao. Se o formato ou o
 * fuso mudarem, o static_assert quebra o build e forcara revisao consciente. */
static_assert(sizeof("31/12/2000 23:59") == 17, "DD/MM/YYYY HH:MM = 16 chars + NUL");
static_assert(CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN == -180,
              "GMT-3 deve ser exatamente -180 minutos (fuso fixo, sem DST)");
static_assert(CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN == -3 * 60,
              "GMT-3 = -3 horas");
static_assert(sizeof(cyberdeck_clock_time_t) >= 6,
              "estrutura minimamente com 5 campos (ano int16 + 4 bytes)");

/* Buffer de saida tipico + area de canario para detectar overflow. */
constexpr size_t kBuf = 32;
constexpr size_t kCanary = 8;
constexpr char kSentinelCh = static_cast<char>(0xA5);

bool suffix_untouched(const char *buf, size_t start, size_t len)
{
    for (size_t i = start; i < len; ++i) {
        if (buf[i] != kSentinelCh) {
            return false;
        }
    }
    return true;
}

cyberdeck_clock_time_t make_time(int year, int month, int day, int hour, int minute)
{
    cyberdeck_clock_time_t t;
    t.year = static_cast<int16_t>(year);
    t.month = static_cast<uint8_t>(month);
    t.day = static_cast<uint8_t>(day);
    t.hour = static_cast<uint8_t>(hour);
    t.minute = static_cast<uint8_t>(minute);
    return t;
}

/* Materializa um temporario (prvalue) numa referencia const e devolve seu
 * endereco. Em C++17, &make_time(...) e ilicito ("taking address of rvalue",
 * rejeitado pelo -Werror do Makefile); a lifetime do temporario se estende
 * ate o fim da expressao completa, entao o ponteiro e valido durante a
 * chamada que o consome (mesma garantia de um argumento const& nomeado). */
const cyberdeck_clock_time_t *addr_of(const cyberdeck_clock_time_t &t)
{
    return &t;
}

bool same_time(const cyberdeck_clock_time_t &a, const cyberdeck_clock_time_t &b)
{
    return a.year == b.year && a.month == b.month && a.day == b.day &&
           a.hour == b.hour && a.minute == b.minute;
}

std::string describe_time(const cyberdeck_clock_time_t &t)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%04d-%02u-%02u %02u:%02u", (int)t.year,
             (unsigned)t.month, (unsigned)t.day, (unsigned)t.hour,
             (unsigned)t.minute);
    return std::string(tmp);
}

/* ------------------------------------------------------------ oraculo
 *
 * Monta a string "DD/MM/YYYY HH:MM" esperada com snprintf propria — uma
 * implementacao INDEPENDENTE (a implementacao sob teste pode usar qualquer
 * tecnica; o oraculo valida o resultado byte a byte).
 */
std::string oracle_format(const cyberdeck_clock_time_t &t)
{
    char tmp[32];
    const int n = snprintf(tmp, sizeof(tmp), "%02u/%02u/%04d %02u:%02u",
                           (unsigned)t.day, (unsigned)t.month, (int)t.year,
                           (unsigned)t.hour, (unsigned)t.minute);
    if (n != 16) {
        ++s_failures;
        std::printf("FAIL %s:%d  oraculo gerou %d chars (esperado 16)\n",
                    __FILE__, __LINE__, n);
    }
    return std::string(tmp, static_cast<size_t>(n));
}

/* ------------------------------------------------- converter: verificacao
 *
 * Roda cyberdeck_clock_from_utc com canario no buffer de saida (out).
 * expected == nullptr => a chamada DEVE retornar false e NAO tocar em out;
 * caso contrario, out deve ser exatamente expected (campo a campo).
 */
void verify_convert(const cyberdeck_clock_time_t &utc, int32_t offset,
                    const cyberdeck_clock_time_t *expected, const char *label)
{
    unsigned char raw[sizeof(cyberdeck_clock_time_t)];
    std::memset(raw, kSentinelCh, sizeof(raw));
    auto *out = reinterpret_cast<cyberdeck_clock_time_t *>(raw);

    ++s_checks;
    const bool ok = cyberdeck_clock_from_utc(&utc, offset, out);
    char utc_desc[64];
    snprintf(utc_desc, sizeof(utc_desc), "[%s] utc=%s offset=%ld", label,
             describe_time(utc).c_str(), static_cast<long>(offset));

    if (expected == nullptr) {
        ++s_checks;
        if (ok) {
            ++s_failures;
            std::printf("FAIL %s:%d  conversao deveria falhar %s\n",
                        __FILE__, __LINE__, utc_desc);
        }
        ++s_checks;
        if (!suffix_untouched(reinterpret_cast<const char *>(raw), 0,
                              sizeof(raw))) {
            ++s_failures;
            std::printf("FAIL %s:%d  out foi modificado em falha %s\n",
                        __FILE__, __LINE__, utc_desc);
        }
        return;
    }

    ++s_checks;
    if (!ok) {
        ++s_failures;
        std::printf("FAIL %s:%d  conversao deveria ter sucesso %s\n",
                    __FILE__, __LINE__, utc_desc);
        return;
    }
    ++s_checks;
    if (!same_time(*out, *expected)) {
        ++s_failures;
        std::printf("FAIL %s:%d  resultado %s != esperado %s %s\n",
                    __FILE__, __LINE__, describe_time(*out).c_str(),
                    describe_time(*expected).c_str(), utc_desc);
    }
}

/* ------------------------------------------------- formatador: verificacao
 *
 * Roda cyberdeck_format_clock com buffer canario. expect_valid == true =>
 * contrato snprintf completo (retorno 16, prefixo truncado, NUL, canario);
 * expect_valid == false => retorno 0 e NADA escrito.
 */
void verify_format(size_t cap, const cyberdeck_clock_time_t *clock,
                   bool expect_valid, const std::string &expected,
                   const char *label)
{
    char buf[kBuf + kCanary];
    std::memset(buf, kSentinelCh, sizeof(buf));
    const std::string full_desc =
        std::string(label) + " cap=" + std::to_string(cap);

    const size_t ret = cyberdeck_format_clock(buf, cap, clock);

    if (!expect_valid) {
        CHECK_EQ_SIZE(ret, 0);
        ++s_checks;
        if (!suffix_untouched(buf, 0, kBuf + kCanary)) {
            ++s_failures;
            std::printf("FAIL %s:%d  entrada invalida deve nao escrever NADA  [%s]\n",
                        __FILE__, __LINE__, full_desc.c_str());
        }
        return;
    }

    CHECK_EQ_SIZE(ret, expected.size());

    if (cap == 0) {
        ++s_checks;
        if (!suffix_untouched(buf, 0, kBuf + kCanary)) {
            ++s_failures;
            std::printf("FAIL %s:%d  cap==0 deve nao escrever nada  [%s]\n",
                        __FILE__, __LINE__, full_desc.c_str());
        }
        return;
    }

    const size_t written = std::min(expected.size(), cap - 1);
    ++s_checks;
    if (std::memcmp(buf, expected.data(), written) != 0) {
        ++s_failures;
        std::printf("FAIL %s:%d  conteudo diverge do esperado  [%s]\n",
                    __FILE__, __LINE__, full_desc.c_str());
    }
    ++s_checks;
    if (buf[written] != '\0') {
        ++s_failures;
        std::printf("FAIL %s:%d  NUL ausente em buf[%zu]  [%s]\n",
                    __FILE__, __LINE__, written, full_desc.c_str());
    }
    ++s_checks;
    if (!suffix_untouched(buf, written + 1, kBuf + kCanary)) {
        ++s_failures;
        std::printf("FAIL %s:%d  escrita alem do NUL (overflow)  [%s]\n",
                    __FILE__, __LINE__, full_desc.c_str());
    }
}

void verify_format_valid(size_t cap, const cyberdeck_clock_time_t &t,
                         const char *label)
{
    verify_format(cap, &t, true, oracle_format(t), label);
}

/* ----------------------------------------------------------------- testes */

void test_gmt_minus_3_no_rollover()
{
    /* Horas 03..23 UTC: subtracao de 3h mantem o dia. */
    verify_convert(make_time(2026, 9, 19, 23, 30), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 9, 19, 20, 30)), "no-rollover");
    verify_convert(make_time(2026, 9, 19, 18, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 9, 19, 15, 0)), "no-rollover");
    verify_convert(make_time(2026, 9, 19, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 9, 19, 9, 0)), "no-rollover");
    verify_convert(make_time(2026, 9, 19, 3, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 9, 19, 0, 0)), "no-rollover");
    verify_convert(make_time(2026, 9, 19, 3, 59), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 9, 19, 0, 59)), "no-rollover");
}

void test_gmt_minus_3_previous_day()
{
    /* Horas 00..02 UTC: subtracao de 3h cai no dia anterior. */
    verify_convert(make_time(2026, 9, 19, 2, 59), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 9, 18, 23, 59)), "previous-day");
    verify_convert(make_time(2026, 9, 19, 1, 30), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 9, 18, 22, 30)), "previous-day");
    verify_convert(make_time(2026, 9, 19, 0, 59), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 9, 18, 21, 59)), "previous-day");
    verify_convert(make_time(2026, 9, 19, 0, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 9, 18, 21, 0)), "previous-day");
}

void test_gmt_minus_3_month_boundaries()
{
    /* Setembro -> agosto (31 dias): dia 1 00:30 UTC vira 31/08 21:30. */
    verify_convert(make_time(2026, 9, 1, 0, 30), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 8, 31, 21, 30)), "month-boundary-31");
    verify_convert(make_time(2026, 9, 1, 2, 59), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 8, 31, 23, 59)), "month-boundary-31");
    /* Outubro -> setembro (30 dias): dia 1 00:00 UTC vira 30/09 21:00. */
    verify_convert(make_time(2026, 10, 1, 0, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 9, 30, 21, 0)), "month-boundary-30");
    /* Fevereiro (28 dias, 2026 nao bissexto): dia 1 00:00 UTC vira 28/02. */
    verify_convert(make_time(2026, 3, 1, 0, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 2, 28, 21, 0)), "feb-28");
}

void test_gmt_minus_3_leap_february()
{
    /* Fevereiro bissexto: 2024 tem 29 dias. */
    verify_convert(make_time(2024, 2, 29, 1, 30), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2024, 2, 28, 22, 30)), "leap-feb-29");
    verify_convert(make_time(2024, 3, 1, 0, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2024, 2, 29, 21, 0)), "leap-feb-29");
    /* Seculo nao bissexto (1900) vs seculo bissexto (2000). */
    verify_convert(make_time(1900, 3, 1, 0, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(1900, 2, 28, 21, 0)), "leap-1900");
    verify_convert(make_time(2000, 3, 1, 0, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2000, 2, 29, 21, 0)), "leap-2000");
}

void test_gmt_minus_3_year_boundary()
{
    verify_convert(make_time(2026, 1, 1, 0, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2025, 12, 31, 21, 0)), "year-boundary");
    verify_convert(make_time(2026, 1, 1, 2, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2025, 12, 31, 23, 0)), "year-boundary");
    verify_convert(make_time(2026, 1, 1, 3, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2026, 1, 1, 0, 0)), "year-boundary");
}

void test_offset_arithmetic_generic()
{
    /* A aritmetica de minutos e generica (offset parametrizado): os casos
     * acima fixam GMT-3; aqui provamos soma/subtracao nao multipla de 60. */
    verify_convert(make_time(2026, 9, 19, 12, 0), 90, addr_of(make_time(2026, 9, 19, 13, 30)),
                   "offset-+90");
    verify_convert(make_time(2026, 9, 19, 12, 0), -90, addr_of(make_time(2026, 9, 19, 10, 30)),
                   "offset--90");
    verify_convert(make_time(2026, 9, 19, 12, 34), 0, addr_of(make_time(2026, 9, 19, 12, 34)),
                   "offset-0");
    /* Cross day com minutos: 00:40 + 90 = 02:10 do mesmo dia. */
    verify_convert(make_time(2026, 9, 19, 0, 40), 90, addr_of(make_time(2026, 9, 19, 2, 10)),
                   "offset-+90-cross");
}

void test_representable_range_edges()
{
    /* Bordas do intervalo representavel (ano 1..9999): resultado fora do
     * intervalo e rejeitado com false (out intacto). */
    verify_convert(make_time(1, 1, 1, 0, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "edge-year-0");
    verify_convert(make_time(1, 1, 1, 2, 59), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "edge-year-0");
    verify_convert(make_time(1, 1, 1, 3, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(1, 1, 1, 0, 0)), "edge-year-1");
    verify_convert(make_time(9999, 12, 31, 23, 59), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(9999, 12, 31, 20, 59)), "edge-year-9999");
    verify_convert(make_time(9999, 12, 31, 22, 0), 240, nullptr, "edge-year-10000");
    verify_convert(make_time(9999, 12, 31, 19, 59), 240,
                   addr_of(make_time(9999, 12, 31, 23, 59)), "edge-year-9999");
}

void test_invalid_utc_rejected()
{
    /* NULL em qualquer argumento. */
    const cyberdeck_clock_time_t valid = make_time(2026, 9, 19, 12, 0);
    cyberdeck_clock_time_t out;
    CHECK(!cyberdeck_clock_from_utc(nullptr, CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN, &out));
    CHECK(!cyberdeck_clock_from_utc(&valid, CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN, nullptr));

    /* Ano fora de 1..9999. */
    verify_convert(make_time(0, 6, 15, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-year-0");
    verify_convert(make_time(10000, 6, 15, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-year-10000");

    /* Mes fora de 1..12. */
    verify_convert(make_time(2026, 0, 15, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-month-0");
    verify_convert(make_time(2026, 13, 15, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-month-13");

    /* Dia 0, dia 32 e dias impossiveis para o mes/ano. */
    verify_convert(make_time(2026, 9, 0, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-day-0");
    verify_convert(make_time(2026, 9, 32, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-day-32");
    verify_convert(make_time(2026, 2, 30, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-feb-30");
    verify_convert(make_time(2025, 2, 29, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-feb-29-2025");
    verify_convert(make_time(1900, 2, 29, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-feb-29-1900");
    verify_convert(make_time(2026, 4, 31, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-apr-31");
    verify_convert(make_time(2026, 6, 31, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-jun-31");

    /* Hora/minuto fora da faixa. */
    verify_convert(make_time(2026, 9, 19, 24, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-hour-24");
    verify_convert(make_time(2026, 9, 19, 12, 60), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-minute-60");
    verify_convert(make_time(2026, 9, 19, 12, 255), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   nullptr, "invalid-minute-255");

    /* Validacoes positivas de contorno (calendario real). */
    verify_convert(make_time(2024, 2, 29, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2024, 2, 29, 9, 0)), "valid-leap-2024");
    verify_convert(make_time(2000, 2, 29, 12, 0), CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                   addr_of(make_time(2000, 2, 29, 9, 0)), "valid-leap-2000");
}

void test_round_trip_gmt_minus_3()
{
    /* ida GMT-3 + volta +3h => volta ao UTC original (quando o intermediario
     * permanece no intervalo 1..9999). */
    const cyberdeck_clock_time_t utc_cases[] = {
        make_time(2026, 9, 19, 23, 30),
        make_time(2026, 1, 1, 0, 0),
        make_time(2024, 3, 1, 0, 0),
        make_time(1, 1, 1, 3, 0),
        make_time(9999, 12, 31, 21, 0),
        make_time(2026, 10, 1, 0, 45),
    };
    const int32_t back = -CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN;
    for (const auto &utc : utc_cases) {
        cyberdeck_clock_time_t local = {};
        const bool ok = cyberdeck_clock_from_utc(&utc, CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                                                 &local);
        CHECK(ok);
        if (!ok) {
            continue;
        }
        cyberdeck_clock_time_t roundtrip = {};
        const bool back_ok = cyberdeck_clock_from_utc(&local, back, &roundtrip);
        CHECK(back_ok);
        if (back_ok) {
            ++s_checks;
            if (!same_time(roundtrip, utc)) {
                ++s_failures;
                std::printf("FAIL %s:%d  round-trip %s -> %s -> %s\n",
                            __FILE__, __LINE__, describe_time(utc).c_str(),
                            describe_time(local).c_str(),
                            describe_time(roundtrip).c_str());
            }
        }
    }
}

void test_format_valid_length_and_padding()
{
    /* Comprimento fixo e zero-padding de dia/mes/hora/minuto. */
    verify_format_valid(0, make_time(2026, 9, 19, 20, 37), "len");
    verify_format_valid(0, make_time(2026, 1, 5, 8, 7), "zero-pad");
    verify_format_valid(0, make_time(2000, 12, 31, 23, 59), "max-fields");
    verify_format_valid(0, make_time(1, 1, 1, 0, 0), "min-fields");
    verify_format_valid(0, make_time(2024, 2, 29, 12, 0), "leap-format");

    /* Esperados literais (independem do oraculo por snprintf). */
    char buf[kBuf];
    cyberdeck_format_clock(buf, sizeof(buf), addr_of(make_time(2026, 9, 19, 20, 37)));
    CHECK(std::string(buf) == "19/09/2026 20:37");
    cyberdeck_format_clock(buf, sizeof(buf), addr_of(make_time(2026, 1, 5, 8, 7)));
    CHECK(std::string(buf) == "05/01/2026 08:07");
    cyberdeck_format_clock(buf, sizeof(buf), addr_of(make_time(1, 1, 1, 0, 0)));
    CHECK(std::string(buf) == "01/01/0001 00:00");
}

void test_format_from_utc_integration()
{
    /* Pipeline completo: UTC -> GMT-3 -> string. */
    const cyberdeck_clock_time_t utc = make_time(2026, 9, 19, 23, 30);
    cyberdeck_clock_time_t local = {};
    CHECK(cyberdeck_clock_from_utc(&utc, CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN, &local));
    verify_format(kBuf, &local, true, "19/09/2026 20:30", "utc->gmt-3->format");

    const cyberdeck_clock_time_t utc2 = make_time(2024, 3, 1, 0, 0);
    cyberdeck_clock_time_t local2 = {};
    CHECK(cyberdeck_clock_from_utc(&utc2, CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN, &local2));
    verify_format(kBuf, &local2, true, "29/02/2024 21:00", "leap-utc->gmt-3->format");

    const cyberdeck_clock_time_t utc3 = make_time(1, 1, 1, 3, 7);
    cyberdeck_clock_time_t local3 = {};
    CHECK(cyberdeck_clock_from_utc(&utc3, CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN, &local3));
    verify_format(kBuf, &local3, true, "01/01/0001 00:07", "year-1-format");
}

void test_format_cap_semantics()
{
    /* cap == 0: consulta de comprimento, nada escrito (buf pode ser NULL). */
    const cyberdeck_clock_time_t t = make_time(2026, 9, 19, 20, 37);
    CHECK_EQ_SIZE(cyberdeck_format_clock(nullptr, 0, &t), 16);
    verify_format(0, &t, true, "19/09/2026 20:37", "cap-0");

    /* cap == 1: apenas NUL. */
    verify_format(1, &t, true, "19/09/2026 20:37", "cap-1");

    /* cap == 8: "19/09/20" + NUL; retorno permanece 16. */
    verify_format(8, &t, true, "19/09/2026 20:37", "cap-8");

    /* cap == 15: um a menos que o necessario -> "19/09/2026 20:3" + NUL. */
    verify_format(15, &t, true, "19/09/2026 20:37", "cap-15");

    /* cap == 16: "19/09/2026 20:37" + NUL (retorno ainda 16: busca paridade
     * ao usar o literal completo). */
    verify_format(16, &t, true, "19/09/2026 20:37", "cap-16");

    /* cap == 17: cabe exatamente, com NUL. */
    verify_format(17, &t, true, "19/09/2026 20:37", "cap-17");

    /* buf == NULL com cap > 0 (entrada valida): defensivo, retorna 16. */
    CHECK_EQ_SIZE(cyberdeck_format_clock(nullptr, 8, &t), 16);

    /* Truncamento detectavel: retorno > cap - 1. */
    char small[8];
    CHECK(cyberdeck_format_clock(small, sizeof(small), &t) > sizeof(small) - 1);
    char big[24];
    CHECK(cyberdeck_format_clock(big, sizeof(big), &t) <= sizeof(big) - 1);
}

void test_format_invalid_returns_zero_and_writes_nothing()
{
    /* clock == NULL -> 0, nada escrito. */
    CHECK_EQ_SIZE(cyberdeck_format_clock(nullptr, 32, nullptr), 0);
    verify_format(32, nullptr, false, "", "null-clock");
    verify_format(1, nullptr, false, "", "null-clock-cap1");

    /* Componentes invalidos -> 0 e NADA escrito (nem com cap folgado). */
    verify_format(kBuf, addr_of(make_time(0, 6, 15, 12, 0)), false, "", "year-0");
    verify_format(kBuf, addr_of(make_time(10000, 6, 15, 12, 0)), false, "", "year-10000");
    verify_format(kBuf, addr_of(make_time(2026, 0, 15, 12, 0)), false, "", "month-0");
    verify_format(kBuf, addr_of(make_time(2026, 13, 15, 12, 0)), false, "", "month-13");
    verify_format(kBuf, addr_of(make_time(2026, 9, 0, 12, 0)), false, "", "day-0");
    verify_format(kBuf, addr_of(make_time(2026, 9, 32, 12, 0)), false, "", "day-32");
    verify_format(kBuf, addr_of(make_time(2026, 2, 30, 12, 0)), false, "", "feb-30");
    verify_format(kBuf, addr_of(make_time(2025, 2, 29, 12, 0)), false, "", "feb-29-2025");
    verify_format(kBuf, addr_of(make_time(2026, 4, 31, 12, 0)), false, "", "apr-31");
    verify_format(kBuf, addr_of(make_time(2026, 9, 19, 24, 0)), false, "", "hour-24");
    verify_format(kBuf, addr_of(make_time(2026, 9, 19, 12, 60)), false, "", "minute-60");
    /* cap == 0 com entrada invalida: retorna 0 (consulta), nada escrito. */
    CHECK_EQ_SIZE(cyberdeck_format_clock(nullptr, 0, addr_of(make_time(2026, 2, 30, 12, 0))), 0);
}

void test_format_never_overflows_sweep()
{
    /* Varredura: estados validos x caps 0..24 + todos os invalidos com cap
     * folgado. Cada caso valida retorno, prefixo truncado, NUL e canario. */
    const cyberdeck_clock_time_t valid_states[] = {
        make_time(2026, 9, 19, 20, 37),
        make_time(2026, 1, 5, 8, 7),
        make_time(2000, 12, 31, 23, 59),
        make_time(1, 1, 1, 0, 0),
        make_time(2024, 2, 29, 12, 0),
        make_time(9999, 12, 31, 23, 59),
    };
    for (const auto &t : valid_states) {
        const std::string expected = oracle_format(t);
        for (size_t cap = 0; cap <= 24; ++cap) {
            verify_format(cap, &t, true, expected, "sweep-valid");
        }
    }
    const cyberdeck_clock_time_t invalid_states[] = {
        make_time(0, 6, 15, 12, 0),
        make_time(10000, 6, 15, 12, 0),
        make_time(2026, 13, 15, 12, 0),
        make_time(2026, 9, 0, 12, 0),
        make_time(2026, 2, 30, 12, 0),
        make_time(2026, 6, 31, 12, 0),
        make_time(2026, 9, 19, 24, 0),
        make_time(2026, 9, 19, 12, 255),
    };
    for (const auto &t : invalid_states) {
        verify_format(kBuf, &t, false, "", "sweep-invalid");
    }
}

void test_deterministic()
{
    /* Funcao pura: mesma entrada -> mesma saida em chamadas repetidas. */
    const cyberdeck_clock_time_t t = make_time(2026, 9, 19, 20, 37);
    char buf1[kBuf], buf2[kBuf];
    std::memset(buf1, kSentinelCh, sizeof(buf1));
    std::memset(buf2, kSentinelCh, sizeof(buf2));
    const size_t r1 = cyberdeck_format_clock(buf1, kBuf, &t);
    const size_t r2 = cyberdeck_format_clock(buf2, kBuf, &t);
    CHECK(r1 == r2);
    CHECK(std::memcmp(buf1, buf2, kBuf) == 0);

    /* Conversao deterministica. */
    const cyberdeck_clock_time_t utc = make_time(2026, 1, 1, 0, 0);
    cyberdeck_clock_time_t a = {}, b = {};
    CHECK(cyberdeck_clock_from_utc(&utc, CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN, &a));
    CHECK(cyberdeck_clock_from_utc(&utc, CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN, &b));
    CHECK(same_time(a, b));
}

} // namespace

int main()
{
    test_gmt_minus_3_no_rollover();
    test_gmt_minus_3_previous_day();
    test_gmt_minus_3_month_boundaries();
    test_gmt_minus_3_leap_february();
    test_gmt_minus_3_year_boundary();
    test_offset_arithmetic_generic();
    test_representable_range_edges();
    test_invalid_utc_rejected();
    test_round_trip_gmt_minus_3();
    test_format_valid_length_and_padding();
    test_format_from_utc_integration();
    test_format_cap_semantics();
    test_format_invalid_returns_zero_and_writes_nothing();
    test_format_never_overflows_sweep();
    test_deterministic();

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_clock (GMT-3 + formatacao + invalidos; %d checks)\n",
                    s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}