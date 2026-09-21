/*
 * Testes unitarios host-side (ESP-IDF target linux) do getnameinfo dual-stack
 * fornecido pelo componente local components/sock_utils.
 *
 * Contexto do bug corrigido (plano aprovado):
 * - O libssh (david-cermak/libssh, connect.c ssh_connect_host_nonblocking)
 *   resolve o host com getaddrinfo(AF_UNSPEC) e, para CADA addrinfo retornado
 *   (IPv4 ou IPv6), chama getnameinfo(addr, addrlen, host, NI_MAXHOST, serv,
 *   NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) antes do connect()
 *   (libssh-mirror/src/connect.c:286-292).
 * - Se getnameinfo falha, o libssh DESCARTAR aquele addrinfo e tenta o
 *   proximo; se o unico resultado for IPv6 (ou IPv4-mapped), a conexao falha.
 * - O sock_utils oficial (managed_components/espressif__sock_utils,
 *   src/getnameinfo.c) implementa getnameinfo apenas para AF_INET e retorna
 *   EAI_FAMILY para AF_INET6 -- ou seja, qualquer alvo IPv6/IPv4-mapped e
 *   descartado pelo libssh. Este componente local (overlay) corrige o
 *   dual-stack sem alterar managed_components.
 *
 * Contexto do bug de regressao DESTE arquivo (flags NI_* alinhadas):
 * - O port do libssh define (port/idf_compat.h:19):
 *       #define NI_NUMERICHOST AI_NUMERICHOST
 *   e o lwIP define (lwip/src/include/lwip/netdb.h:84-85):
 *       AI_NUMERICHOST = 0x04, AI_NUMERICSERV = 0x08
 *   Logo, as flags que o libssh passa de verdade sao 0x04 | 0x08 == 0x0C.
 * - No device, <net/if.h> (components/newlib/platform_include/net/if.h:26-27)
 *   ja define NI_NUMERICSERV = 0x08 e NI_DGRAM = 0x10, mas NAO define
 *   NI_NUMERICHOST -- entao o fallback de netdb_macros.h (NI_NUMERICHOST=0x1)
 *   passa a valer, divergindo do 0x04 que o libssh envia.
 * - getnameinfo.c:26 valida `flags & ~(NI_NUMERICHOST|NI_NUMERICSERV|NI_DGRAM)`
 *   == 0, ou seja, com a numeracao errada a mascara e ~0x19 em vez de ~0x1C:
 *   as flags reais 0x04/0x0C do libssh sao rejeitadas com EAI_BADFLAGS e o
 *   addrinfo e descartado em conectividade SSH IPv4/IPv6.
 * - Por isso os testes abaixo usam OS VALORES NUMERICOS REAIS (0x04, 0x08,
 *   0x0C, 0x18), fixados por static_assert, e NAO as macros de netdb_macros.h
 *   -- a numeracao desse header e justamente o objeto da regressao. Enquanto
 *   a incompatibilidade existir, os casos com 0x04/0x0C FALHAM (EAI_BADFLAGS
 *   em vez de rc=0), o que e o sinal esperado deste teste.
 *
 * Padrao seguido: managed_components/espressif__sock_utils/test/host/ (Catch2
 * v3 + projeto ESP-IDF de host tests com CONFIG_IDF_TARGET="linux"). Neste
 * ambiente o header do componente renomeia getnameinfo -> esp_getnameinfo
 * (namespace esp_) para nao colidir com a glibc; no device a funcao e o
 * simbolo POSIX getnameinfo que o libssh linka.
 *
 * Formatos fixados (lwIP, consistentes host linux e device):
 *   AF_INET            -> "192.168.1.50"            (inet_ntop AF_INET)
 *   AF_INET6 nativo    -> "2001:DB8::1", "::1"      (inet_ntop AF_INET6,
 *                                                    ip6addr_ntoa_r lwIP)
 *   IPv4-mapped        -> "::FFFF:192.168.1.50"     (lwIP IP4MAPPED_HEADER,
 *                                                    ip6_addr.c:241-258)
 * Codigos de retorno determinados pelo ecossistema ESP-IDF/lwIP/local:
 *   EAI_BADFLAGS = 3   (netdb_macros.h / sys/errno.h do newlib)
 *   EAI_FAMILY  = 204  (lwip/netdb.h)
 *   EOVERFLOW   = 75   (errno.h)
 */
#include "getnameinfo.h"
#include "lwip/sockets.h"

#include "esp_check.h"   // ESP_ERROR_CHECK
#include "esp_netif.h"   // esp_netif_init() (mesmo cenario do teste oficial)
#include "esp_event.h"   // esp_event_loop_create_default()

#include <netdb.h>   // getaddrinfo/freeaddrinfo (lwip no host linux ESP-IDF)
#include <cerrno>    // EOVERFLOW (mesmo valor 75 em glibc e newlib)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ios>

// NI_MAXHOST/NI_MAXSERV nao sao definidos pelo lwip/netdb.h do ESP-IDF
// (no device vem de net/if.h; no host linux nem isso). O libssh usa os
// mesmos valores (connect.c: addrname[NI_MAXHOST], portname[NI_MAXSERV]).
#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif

#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_session.hpp"

namespace {

// ---------------------------------------------------------------------------
// Valores REAIS das flags NI_* no ecossistema ESP-IDF/lwIP/libssh.
//
// O libssh passa NI_NUMERICHOST | NI_NUMERICSERV em connect.c:292, mas a
// numeracao que ele compila NAO e a do netdb_macros.h local:
//   - libssh (port/idf_compat.h:19): NI_NUMERICHOST == AI_NUMERICHOST
//     == 0x04 (lwip/src/include/lwip/netdb.h:84);
//   - NI_NUMERICSERV == AI_NUMERICSERV == 0x08 (lwip/netdb.h:85; idem
//     <net/if.h> do device, newlib platform_include/net/if.h:26);
//   - NI_DGRAM == 0x10 (<net/if.h> do device, newlib platform_include,
//     net/if.h:27).
// Estes valores sao fixados abaixo como literais numericos + static_assert
// para que o teste regrida o CONTRATO REAL (o que o libssh envia), nao a
// numeracao interna possivelmente divergente do componente. Nenhuma macro de
// netdb_macros.h e usada nos casos deste arquivo.
// ---------------------------------------------------------------------------
constexpr int kNiNumerichostReal = 0x04;               // == AI_NUMERICHOST (lwip)
constexpr int kNiNumericservReal = 0x08;               // == AI_NUMERICSERV (lwip / net/if.h device)
constexpr int kNiDgramReal       = 0x10;               // == NI_DGRAM (net/if.h device)
constexpr int kNiHostAndServReal = kNiNumerichostReal | kNiNumericservReal; // 0x0C (flags do libssh connect.c:292)
constexpr int kNiServAndDgramReal = kNiNumericservReal | kNiDgramReal;      // 0x18

static_assert(kNiNumerichostReal == 0x04, "NI_NUMERICHOST real (libssh/lwIP) deve ser 0x04");
static_assert(kNiNumericservReal == 0x08, "NI_NUMERICSERV real (lwIP/ESP-IDF) deve ser 0x08");
static_assert(kNiDgramReal       == 0x10, "NI_DGRAM real (ESP-IDF net/if.h) deve ser 0x10");
static_assert(kNiHostAndServReal == 0x0C, "flags do libssh (NI_NUMERICHOST|NI_NUMERICSERV) devem ser 0x0C");

// ---------------------------------------------------------------------------
// Fixture de dados: casos de endereco de entrada -> saida esperada.
// Associo aqui o contrato dual-stack em forma de tabela (deterministico).
// Os bytes dos enderecos estao em network byte order (ordem de transmissao).
// ---------------------------------------------------------------------------

struct gni_case {
    const char *name;        // descricao do caso (report de falha)
    int family;              // AF_INET ou AF_INET6
    const uint8_t bytes[16]; // endereco em network byte order
    uint16_t port;           // porta em host byte order
    int flags;               // flags NI_* REAIS (0x04/0x08/0x0C/0x18; libssh)
    int expected_rc;         // codigo de retorno esperado (0 = sucesso)
    const char *expected_host; // saida esperada do host ("" = nao solicitado)
    const char *expected_serv; // saida esperada do serv ("" = nao solicitado)
};

const gni_case k_ipv4_cases[] = {
    // AF_INET numerico com as flags REAIS do libssh (regressao NI_*):
    // 0x04 = NI_NUMERICHOST, 0x08 = NI_NUMERICSERV, 0x0C = libssh connect.c:292.
    // (A numeracao antiga de netdb_macros.h usava 0x1 para NI_NUMERICHOST:
    // as linhas 0x04/0x0C abaixo falham com EAI_BADFLAGS enquanto a
    // incompatibilidade existir -- sinal esperado desta regressao.)
    {"ipv4 192.168.1.50 host only flags=0x04", AF_INET, {192, 168, 1, 50}, 0,
     kNiNumerichostReal, 0, "192.168.1.50", ""},
    {"ipv4 192.168.1.50 host+serv flags=0x0C (libssh)", AF_INET, {192, 168, 1, 50}, 2222,
     kNiHostAndServReal, 0, "192.168.1.50", "2222"},
    {"ipv4 10.0.0.1 host+serv flags=0x0C (libssh)", AF_INET, {10, 0, 0, 1}, 22,
     kNiHostAndServReal, 0, "10.0.0.1", "22"},
    {"ipv4 10.0.0.1 serv only flags=0x08", AF_INET, {10, 0, 0, 1}, 22,
     kNiNumericservReal, 0, "", "22"},
    {"ipv4 0.0.0.0 (unspecified) flags=0x04", AF_INET, {0, 0, 0, 0}, 0,
     kNiNumerichostReal, 0, "0.0.0.0", ""},
    {"ipv4 255.255.255.255 (broadcast) flags=0x04", AF_INET, {255, 255, 255, 255}, 0,
     kNiNumerichostReal, 0, "255.255.255.255", ""},
    {"ipv4 serv only flags=0x18 (NI_NUMERICSERV|NI_DGRAM)", AF_INET, {192, 168, 1, 50}, 443,
     kNiServAndDgramReal, 0, "", "443"},
};

const gni_case k_ipv6_native_cases[] = {
    // AF_INET6 nativo: antes do fix dual-stack retornava EAI_FAMILY (bug);
    // agora o foco e a regressao das flags NI_* reais (0x04/0x08/0x0C).
    {"ipv6 2001:db8::1 host only flags=0x04", AF_INET6,
     {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 0,
     kNiNumerichostReal, 0, "2001:DB8::1", ""},
    {"ipv6 2001:db8::1 host+serv flags=0x0C (libssh)", AF_INET6,
     {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 22,
     kNiHostAndServReal, 0, "2001:DB8::1", "22"},
    {"ipv6 ::1 loopback flags=0x04", AF_INET6,
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 0,
     kNiNumerichostReal, 0, "::1", ""},
    {"ipv6 ::1 serv only flags=0x08", AF_INET6,
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 22,
     kNiNumericservReal, 0, "", "22"},
    {"ipv6 :: any flags=0x04", AF_INET6,
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0,
     kNiNumerichostReal, 0, "::", ""},
    {"ipv6 fe80::1 link-local flags=0x04", AF_INET6,
     {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 0,
     kNiNumerichostReal, 0, "FE80::1", ""},
};

const gni_case k_ipv4_mapped_cases[] = {
    // IPv4-mapped (dual-stack): o lwIP apresenta como "::FFFF:a.b.c.d"
    // (ip6addr_ntoa_r, ip6_addr.c IP4MAPPED_HEADER). O addrinfo retornado
    // pelo getaddrinfo(lwIP) para esses alvos e AF_INET6 mapped; sem o fix
    // dual-stack o sock_utils oficial retornava EAI_FAMILY e o libssh
    // descartava o addrinfo (connect.c:293-300). Aqui o foco e a regressao
    // das flags NI_* reais (0x04/0x08/0x0C) para este formato.
    {"mapped ::ffff:192.168.1.50 host only flags=0x04", AF_INET6,
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 192, 168, 1, 50}, 0,
     kNiNumerichostReal, 0, "::FFFF:192.168.1.50", ""},
    {"mapped ::ffff:192.168.1.50 host+serv flags=0x0C (libssh)", AF_INET6,
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 192, 168, 1, 50}, 2222,
     kNiHostAndServReal, 0, "::FFFF:192.168.1.50", "2222"},
    {"mapped ::ffff:10.0.0.1 host+serv flags=0x0C (libssh)", AF_INET6,
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 10, 0, 0, 1}, 22,
     kNiHostAndServReal, 0, "::FFFF:10.0.0.1", "22"},
    {"mapped ::ffff:10.0.0.1 serv only flags=0x08", AF_INET6,
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 10, 0, 0, 1}, 22,
     kNiNumericservReal, 0, "", "22"},
};

// ---------------------------------------------------------------------------
// Helpers para montar sockaddr em network byte order sem depender de hton*/endian
// ---------------------------------------------------------------------------

struct sockaddr_in make_in4(const uint8_t b[4], uint16_t port)
{
    struct sockaddr_in sin;
    std::memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    std::memcpy(&sin.sin_addr.s_addr, b, 4);
    // Porta em network byte order manual (big endian), deterministica em
    // qualquer host (todos os targets suportados pelo ESP-IDF sao LE).
    sin.sin_port = static_cast<in_port_t>((port >> 8) | (port << 8));
    return sin;
}

struct sockaddr_in6 make_in6(const uint8_t b[16], uint16_t port)
{
    struct sockaddr_in6 sin6;
    std::memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    std::memcpy(&sin6.sin6_addr.s6_addr, b, 16);
    sin6.sin6_port = static_cast<in_port_t>((port >> 8) | (port << 8));
    return sin6;
}

// Enderecos fixos usados pelos testes de borda/robustez abaixo. Arrays
// nomeados em vez de braced-init-list direto na chamada: em C++ um
// braced-init-list nao converte para o parametro const uint8_t * de
// make_in4/make_in6 (erro de compilacao). Os bytes estao em network byte
// order, identicos aos usados nas tabelas k_*_cases.
const uint8_t k_addr4[4] = {192, 168, 1, 50}; // 192.168.1.50
const uint8_t k_addr6[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 1};          // 2001:DB8::1

void run_case_table(const gni_case *cases, size_t n)
{
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];

    for (size_t i = 0; i < n; ++i) {
        const gni_case &c = cases[i];
        std::memset(host, 0x55, sizeof(host));
        std::memset(serv, 0x55, sizeof(serv));

        if (c.family == AF_INET) {
            struct sockaddr_in sin = make_in4(c.bytes, c.port);
            int rc = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin),
                                     host, sizeof(host), serv, sizeof(serv), c.flags);
            INFO("case: " << c.name << " rc=" << rc);
            REQUIRE(rc == c.expected_rc);
            if (c.expected_host[0] != '\0') {
                REQUIRE(std::strcmp(host, c.expected_host) == 0);
            }
            if (c.expected_serv[0] != '\0') {
                REQUIRE(std::strcmp(serv, c.expected_serv) == 0);
            }
        } else {
            struct sockaddr_in6 sin6 = make_in6(c.bytes, c.port);
            int rc = esp_getnameinfo((const struct sockaddr *)&sin6, sizeof(sin6),
                                     host, sizeof(host), serv, sizeof(serv), c.flags);
            INFO("case: " << c.name << " rc=" << rc);
            REQUIRE(rc == c.expected_rc);
            if (c.expected_host[0] != '\0') {
                REQUIRE(std::strcmp(host, c.expected_host) == 0);
            }
            if (c.expected_serv[0] != '\0') {
                REQUIRE(std::strcmp(serv, c.expected_serv) == 0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Testes
// ---------------------------------------------------------------------------

TEST_CASE("esp_getnameinfo() accepts real libssh NI_* flags on IPv4 (regression)",
          "[sock_utils][getnameinfo][ipv4][flags]")
{
    // Regressao NI_*: estas chamadas usam as flags REAIS (0x04/0x08/0x0C) que
    // o libssh envia (connect.c:292 com idf_compat.h:19). Se o componente
    // ainda valida com a numeracao antiga (NI_NUMERICHOST=0x1), rc = 3
    // (EAI_BADFLAGS) em vez de 0 -> FALHA esperada enquanto a
    // incompatibilidade existir.
    run_case_table(k_ipv4_cases, sizeof(k_ipv4_cases) / sizeof(k_ipv4_cases[0]));
}

TEST_CASE("esp_getnameinfo() accepts real libssh NI_* flags on native AF_INET6",
          "[sock_utils][getnameinfo][ipv6][flags]")
{
    run_case_table(k_ipv6_native_cases, sizeof(k_ipv6_native_cases) / sizeof(k_ipv6_native_cases[0]));
}

TEST_CASE("esp_getnameinfo() accepts real libssh NI_* flags on IPv4-mapped (dual-stack)",
          "[sock_utils][getnameinfo][ipv4mapped][flags]")
{
    run_case_table(k_ipv4_mapped_cases,
                   sizeof(k_ipv4_mapped_cases) / sizeof(k_ipv4_mapped_cases[0]));
}

TEST_CASE("esp_getnameinfo() rejects genuinely invalid flags with EAI_BADFLAGS",
          "[sock_utils][getnameinfo][flags]")
{
    // A mascara aceita apenas as flags NI_* REAIS do ecossistema:
    //   NI_NUMERICHOST(0x04) | NI_NUMERICSERV(0x08) | NI_DGRAM(0x10) == 0x1C.
    // Bits fora dessa mascara sao GENUINAMENTE invalidos e devem retornar
    // EAI_BADFLAGS de forma deterministica nos dois ambientes (host linux e
    // device), misturados ou nao com bits validos:
    //   - 0x01: NI_NOFQDN (numeracao POSIX). O componente e numerico-only
    //           (nao ha DNS reverso/lookup no lwIP) e nao suporta o bit.
    //           ATENCAO (regressao): com a mascara antiga ~0x19 o bit 0x01
    //           era aceito por colisao com o fallback errado NI_NUMERICHOST
    //           == 0x1 -> este caso FALHA enquanto a incompatibilidade existir.
    //   - 0x02: bit indefinido no ecossistema ESP-IDF (a glibc usa 0x02 para
    //           NI_NUMERICSERV, mas o componente nao e a glibc: e lwIP/ESP-IDF).
    //   - 0x20: bit acima de NI_DGRAM(0x10) na numeracao ESP-IDF.
    //   - 0x2C: 0x04|0x08|0x20 -- invalido mesmo quando misturado com bits validos.
    //   - 0x1000: bit alto fora da mascara.
    //   - -1 (0xFFFFFFFF): bits indefinidos.
    // (0x04 NAO e mais um caso invalido: e o NI_NUMERICHOST real do libssh,
    // coberto pelas tabelas k_*_cases acima.)
    struct sockaddr_in sin = make_in4(k_addr4, 22);
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    const socklen_t hostlen = static_cast<socklen_t>(sizeof(host));
    const socklen_t servlen = static_cast<socklen_t>(sizeof(serv));

    const int kEaiBadFlags = 3; // netdb_macros.h / sys/errno.h do newlib (ambos ambientes)
    const int invalid_flags[] = {0x01, 0x02, 0x20, 0x2C, 0x1000, -1};

    for (int flags : invalid_flags) {
        std::memset(host, 0x55, sizeof(host));
        std::memset(serv, 0x55, sizeof(serv));
        int rc = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin),
                                 host, hostlen, serv, servlen, flags);
        INFO("flags=0x" << std::hex << static_cast<unsigned>(flags) << std::dec
                        << " rc=" << rc);
        REQUIRE(rc == kEaiBadFlags);
        // Falha de flags nao pode corromper os buffers de saida.
        REQUIRE(host[0] == static_cast<char>(0x55));
        REQUIRE(serv[0] == static_cast<char>(0x55));
    }
}

TEST_CASE("esp_getnameinfo() buffer edge cases and error codes",
          "[sock_utils][getnameinfo][buffers]")
{
    struct sockaddr_in sin = make_in4(k_addr4, 2222);
    struct sockaddr_in6 sin6 = make_in6(k_addr6, 22);
    const int kEOverflow = EOVERFLOW; // 75: padrao oficial do sock_utils (errno)

    // Todas as chamadas abaixo usam as flags REAIS do libssh (0x04/0x08/0x0C).
    // Enquanto a mascara antiga (~0x19) existir, os casos com 0x04/0x0C
    // retornam EAI_BADFLAGS antes da checagem de buffer -> FALHA esperada.

    // 1) Buffer exatamente do tamanho necessario (13 bytes: "192.168.1.50" + NUL).
    {
        char exact[13];
        int rc = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin),
                                 exact, sizeof(exact), NULL, 0, kNiNumerichostReal);
        REQUIRE(rc == 0);
        REQUIRE(std::strcmp(exact, "192.168.1.50") == 0);
    }

    // 2) Buffer menor que o necessario -> EOVERFLOW, sem truncagem silenciosa.
    {
        char small[12];
        int rc = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin),
                                 small, sizeof(small), NULL, 0, kNiNumerichostReal);
        REQUIRE(rc == kEOverflow);
    }

    // 3) Buffer de serviço insuficiente (bug do oficial: snprintf truncava e
    //    retornava 0). O fix deve reportar EOVERFLOW. (0x08 ja e aceito pela
    //    mascara atual e pela alinhada: caso estavel.)
    {
        char serv_one[1];
        int rc = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin),
                                 NULL, 0, serv_one, sizeof(serv_one), kNiNumericservReal);
        REQUIRE(rc == kEOverflow);
    }

    // 4) Host/serv nao solicitados (NULL + 0) -> sucesso, sem escrita
    //    (comportamento POSIX e do oficial: NULL == "nao quero este campo").
    {
        int rc = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin),
                                 NULL, 0, NULL, 0, kNiHostAndServReal);
        REQUIRE(rc == 0);
    }

    // 5) Tamanho exato para IPv6 nativo ("2001:DB8::1" = 11 bytes + NUL = 12).
    {
        char exact6[12];
        int rc = esp_getnameinfo((const struct sockaddr *)&sin6, sizeof(sin6),
                                 exact6, sizeof(exact6), NULL, 0, kNiNumerichostReal);
        REQUIRE(rc == 0);
        REQUIRE(std::strcmp(exact6, "2001:DB8::1") == 0);
    }

    // 6) Buffer IPv6 insuficiente -> EOVERFLOW.
    {
        char small6[11];
        int rc = esp_getnameinfo((const struct sockaddr *)&sin6, sizeof(sin6),
                                 small6, sizeof(small6), NULL, 0, kNiNumerichostReal);
        REQUIRE(rc == kEOverflow);
    }
}

TEST_CASE("esp_getnameinfo() unsupported families return EAI_FAMILY",
          "[sock_utils][getnameinfo][families]")
{
    const int kEaiFamily = 204; // lwip/netdb.h (host linux e device)
    char host[NI_MAXHOST];

    // flags = 0 de proposito: a mascara de flags e o objeto de OUTRA regressao
    // (k_*_cases). Aqui isolamos a checagem de familia para que o EAI_FAMILY
    // (checado depois da mascara, no switch de sa_family) seja observavel de
    // forma ortogonal aos valores de NI_*, tanto no estado atual quanto apos
    // o alinhamento das flags.
    // AF_UNIX / familia desconhecida (0 == AF_UNSPEC).
    for (int family : {AF_UNIX, 0, 0x7F}) {
        struct sockaddr sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_family = static_cast<sa_family_t>(family);
        int rc = esp_getnameinfo(&sa, sizeof(sa), host, sizeof(host), NULL, 0, 0);
        INFO("family=" << family << " rc=" << rc);
        REQUIRE(rc == kEaiFamily);
    }
}

TEST_CASE("esp_getnameinfo() is safe on NULL addr and short addrlen",
          "[sock_utils][getnameinfo][safety]")
{
    char host[NI_MAXHOST];

    // flags = 0 pelo mesmo motivo do teste de familias: isolar a seguranca de
    // entrada (addr NULL / addrlen curto) da regressao de mascara de flags.
    // addr NULL: deve retornar erro de forma segura (o oficial tinha UB).
    {
        int rc = esp_getnameinfo(NULL, sizeof(struct sockaddr_in),
                                 host, sizeof(host), NULL, 0, 0);
        REQUIRE(rc != 0);
    }

    // addrlen menor que o minimo do formato: erro seguro, sem ler fora.
    {
        struct sockaddr_in sin = make_in4(k_addr4, 22);
        int rc = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin) - 1,
                                 host, sizeof(host), NULL, 0, 0);
        REQUIRE(rc != 0);
        rc = esp_getnameinfo((const struct sockaddr *)&sin, 1,
                             host, sizeof(host), NULL, 0, 0);
        REQUIRE(rc != 0);
    }
}

TEST_CASE("esp_getnameinfo() is deterministic and side-effect-free",
          "[sock_utils][getnameinfo][purity]")
{
    struct sockaddr_in sin = make_in4(k_addr4, 22);
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];

    // Com apenas NI_NUMERICHOST(0x04), o buffer de serv nao pode ser tocado.
    // (Enquanto a mascara antiga existir, 0x04 -> EAI_BADFLAGS: FALHA
    // esperada desta regressao; apos o alinhamento, rc=0 com serv intacto.)
    {
        std::memset(serv, 0xA5, sizeof(serv));
        int rc = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin),
                                 host, sizeof(host), serv, sizeof(serv), kNiNumerichostReal);
        REQUIRE(rc == 0);
        for (size_t i = 0; i < sizeof(serv); ++i) {
            REQUIRE(serv[i] == static_cast<char>(0xA5));
        }
    }

    // Com apenas NI_NUMERICSERV(0x08), o buffer de host nao pode ser tocado.
    // (0x08 e aceito pela mascara atual e pela alinhada: caso estavel.)
    {
        std::memset(host, 0x5A, sizeof(host));
        int rc = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin),
                                 host, sizeof(host), serv, sizeof(serv), kNiNumericservReal);
        REQUIRE(rc == 0);
        for (size_t i = 0; i < sizeof(host); ++i) {
            REQUIRE(host[i] == static_cast<char>(0x5A));
        }
    }

    // flags == 0: sem DNS reverso (limitacao herdada do componente oficial;
    // apenas o numero e formatado quando solicitado) -> sucesso, buffers intactos.
    {
        std::memset(host, 0x3C, sizeof(host));
        std::memset(serv, 0xC3, sizeof(serv));
        int rc = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin),
                                 host, sizeof(host), serv, sizeof(serv), 0);
        REQUIRE(rc == 0);
        REQUIRE(host[0] == static_cast<char>(0x3C));
        REQUIRE(serv[0] == static_cast<char>(0xC3));
    }

    // Determinismo: mesma entrada, mesmo resultado, em chamadas repetidas.
    // (Usa as flags REAIS do libssh; falha enquanto 0x04 for rejeitado.)
    {
        char first[NI_MAXHOST];
        char second[NI_MAXHOST];
        int rc1 = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin),
                                  first, sizeof(first), NULL, 0, kNiNumerichostReal);
        int rc2 = esp_getnameinfo((const struct sockaddr *)&sin, sizeof(sin),
                                  second, sizeof(second), NULL, 0, kNiNumerichostReal);
        REQUIRE(rc1 == 0);
        REQUIRE(rc2 == 0);
        REQUIRE(std::strcmp(first, second) == 0);
        REQUIRE(std::strcmp(first, "192.168.1.50") == 0);
    }
}

TEST_CASE("esp_getnameinfo() getaddrinfo contract used by libssh connect",
          "[sock_utils][getnameinfo][libssh][contract]")
{
    // Espelha getai()/ssh_connect_host_nonblocking do libssh
    // (libssh-mirror/src/connect.c:286-300): cada addrinfo retornado por
    // getaddrinfo(AF_UNSPEC) com AI_NUMERICHOST|AI_NUMERICSERV precisa passar
    // por getnameinfo(..., NI_NUMERICHOST|NI_NUMERICSERV) sem erro -- caso
    // contrario o libssh descarta o addrinfo e a conexao pode falhar mesmo
    // com endereco valido.
    //
    // As flags passadas sao as REAIS do libssh (0x0C): port/idf_compat.h:19
    // mapeia NI_NUMERICHOST -> AI_NUMERICHOST (0x04, lwip/netdb.h:84) e
    // NI_NUMERICSERV -> AI_NUMERICSERV (0x08, lwip/netdb.h:85). Enquanto a
    // mascara antiga de getnameinfo.c (que usa o fallback 0x1 de
    // netdb_macros.h) existir, TODOS os addrinfo retornam EAI_BADFLAGS e o
    // libssh descartaria a conexao -- FALHA esperada (regressao de flags).
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    hints.ai_family = AF_UNSPEC;   // SSH_ADDRESS_FAMILY_ANY (PF_UNSPEC)
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const char *targets[][2] = {
        {"192.168.1.50", "2222"}, // IPv4 puro (regressao)
        {"10.0.0.1", "22"},       // IPv4 puro
        {"2001:db8::1", "22"},    // IPv6 nativo (descartado antes do fix dual-stack)
        {"::ffff:10.0.0.1", "22"},// IPv4-mapped (descartado antes do fix dual-stack)
    };

    for (const auto &target : targets) {
        struct addrinfo *ai = NULL;
        const char *hostname = target[0];
        const char *service = target[1];
        int rc = getaddrinfo(hostname, service, &hints, &ai);
        INFO("getaddrinfo(" << hostname << "," << service << ") rc=" << rc);
        REQUIRE(rc == 0);
        REQUIRE(ai != NULL);

        // Cada addrinfo retornado precisa ser formatavel (contrato libssh).
        bool saw_any = false;
        for (struct addrinfo *itr = ai; itr != NULL; itr = itr->ai_next) {
            saw_any = true;
            char addrname[NI_MAXHOST];
            char portname[NI_MAXSERV];
            rc = esp_getnameinfo(itr->ai_addr, itr->ai_addrlen,
                                 addrname, sizeof(addrname),
                                 portname, sizeof(portname),
                                 kNiHostAndServReal);
            INFO("esp_getnameinfo(" << hostname << " family=" << itr->ai_family
                                    << ") rc=" << rc);
            REQUIRE(rc == 0);
            REQUIRE(addrname[0] != '\0');
            REQUIRE(std::strcmp(portname, service) == 0);
        }
        REQUIRE(saw_any);
        freeaddrinfo(ai);
    }
}

} // namespace

extern "C" void app_main(void)
{
    // getnameinfo usa apenas inet_ntop do lwIP in-memory, mas o ambiente do
    // lwIP e inicializado da mesma forma que o host test oficial do sock_utils
    // para manter o cenario identico ao CI da Espressif.
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    Catch::Session session;

    int failures = session.run();
    if (failures > 0) {
        std::printf("TEST FAILED! number of failures=%d\n", failures);
        std::exit(1);
    }
    std::printf("Test passed!\n");
    std::exit(0);
}