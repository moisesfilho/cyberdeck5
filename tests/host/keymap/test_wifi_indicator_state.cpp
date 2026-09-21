/*
 * Testes unitarios host-side para a funcao pura
 * cyberdeck_wifi_indicator_is_lit().
 *
 * Plano aprovado (recorte sob teste):
 *   - Indicador Wi-Fi eh um UNICO icono no header;
 *   - Indicador LIT somente quando enabled && connected && has_ip;
 *   - Indicador APAGADO em todos os outros estados;
 *   - NÃO depende de SSID: o contrato nao recebe parametro ssid;
 *   - 8 combinacoes de (enabled, connected, has_ip), apenas uma
 *     produz o estado LIT.
 *
 * Contrato sob teste:
 *   - bool cyberdeck_wifi_indicator_is_lit(bool enabled,
 *     bool connected, bool has_ip);
 *   - Retorna true somente quando os tres parametros sao true;
 *   - Funcao pura e deterministica;
 *   - Sem dependencia de SSID ou qualquer fator externo.
 *
 * Build: make test_wifi_indicator_state (veja Makefile;
 * sem dependencias alem de g++).
 */

#include "cyberdeck_wifi_indicator.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int s_failures = 0;
int s_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++s_checks;                                                          \
        if (!(cond)) {                                                       \
            ++s_failures;                                                    \
            std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected)                                              \
    do {                                                                        \
        ++s_checks;                                                             \
        if ((actual) != (expected)) {                                           \
            ++s_failures;                                                       \
            std::printf("FAIL %s:%d  CHECK_EQ(%s): expected %s, actual %s\n", \
                        __FILE__, __LINE__, #actual, #expected,               \
                        (actual) ? "true" : "false");                         \
        }                                                                       \
    } while (0)

/* As 8 combinacoes de (enabled, connected, has_ip).
 * Apenas (true, true, true) produz true (LIT); as outras 7 produzem false (APAGADO). */

void test_all_8_combinations()
{
    /* Caso 1: F,F,F -> APAGADO */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(false, false, false), false);
    /* Caso 2: F,F,T -> APAGADO */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(false, false, true), false);
    /* Caso 3: F,T,F -> APAGADO */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(false, true, false), false);
    /* Caso 4: F,T,T -> APAGADO */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(false, true, true), false);
    /* Caso 5: T,F,F -> APAGADO */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(true, false, false), false);
    /* Caso 6: T,F,T -> APAGADO */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(true, false, true), false);
    /* Caso 7: T,T,F -> APAGADO */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(true, true, false), false);
    /* Caso 8: T,T,T -> LIT (unico estado verdadeiro) */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(true, true, true), true);
}

void test_disabled_dominates_all_states()
{
    /* Desabilitado domina qualquer combinacao de connected e has_ip. */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(false, false, false), false);
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(false, true, true), false);
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(false, true, false), false);
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(false, false, true), false);
}

void test_enabled_without_full_connection_is_off()
{
    /* Habilitado mas sem conexao completa -> APAGADO. */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(true, false, false), false);
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(true, true, false), false);
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(true, false, true), false);
}

void test_only_connected_with_ip_is_lit()
{
    /* Apenas (true, true, true) produz true. */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(true, true, true), true);
}

void test_deterministic()
{
    /* Funcao pura: mesma entrada -> mesma saida em chamadas repetidas. */
    const bool r1 = cyberdeck_wifi_indicator_is_lit(true, true, true);
    const bool r2 = cyberdeck_wifi_indicator_is_lit(true, true, true);
    CHECK_EQ(r1, r2);

    const bool r3 = cyberdeck_wifi_indicator_is_lit(false, true, false);
    const bool r4 = cyberdeck_wifi_indicator_is_lit(false, true, false);
    CHECK_EQ(r3, r4);

    const bool r5 = cyberdeck_wifi_indicator_is_lit(true, false, true);
    const bool r6 = cyberdeck_wifi_indicator_is_lit(true, false, true);
    CHECK_EQ(r5, r6);
}

void test_no_ssid_dependency()
{
    /* O contrato nao recebe SSID: a funcao tem apenas 3 parametros
     * booleanos (enabled, connected, has_ip). O indicador e um unico
     * icone que nao depende do nome da rede.
     * Verifica que (true, true, true) sempre produz true
     * independentemente de qualquer fator externo. */
    CHECK_EQ(cyberdeck_wifi_indicator_is_lit(true, true, true), true);
}

}  // namespace anonymous

/* ---------------------------------------------------------- main */

int main()
{
    test_all_8_combinations();
    test_disabled_dominates_all_states();
    test_enabled_without_full_connection_is_off();
    test_only_connected_with_ip_is_lit();
    test_deterministic();
    test_no_ssid_dependency();

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_wifi_indicator_is_lit (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
