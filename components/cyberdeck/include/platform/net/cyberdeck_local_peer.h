#pragma once

/*
 * Checagem de origem para as superficies HTTP da LAN.
 *
 * O filtro por endereco de origem NAO autentica o usuario e nao substitui o
 * HMAC do canal de comandos: ele apenas impede que a rota fique exposta a
 * peers fora das faixas privadas/link-local. Mantido header-only para que os
 * adaptadores HTTP de dispositivo compartilhem a mesma regra sem depender de
 * ESP-IDF no link host.
 */
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>

namespace cyberdeck_net {

inline bool ipv4_local(std::uint32_t host)
{
    return (host >> 24) == 127 ||                                  /* 127.0.0.0/8 */
           (host >> 24) == 10 ||                                   /* 10.0.0.0/8 */
           (host & 0xfff00000u) == 0xac100000u ||                  /* 172.16.0.0/12 */
           (host >> 16) == ((192 << 8) | 168) ||                   /* 192.168.0.0/16 */
           (host >> 16) == ((169 << 8) | 254);                     /* 169.254.0.0/16 */
}

inline bool socket_is_local_peer(int socket)
{
    if (socket < 0) return false;
    sockaddr_storage address = {};
    socklen_t length = sizeof(address);
    if (getpeername(socket, reinterpret_cast<sockaddr *>(&address), &length) != 0) return false;

    if (address.ss_family == AF_INET)
        return ipv4_local(ntohl(reinterpret_cast<const sockaddr_in *>(&address)->sin_addr.s_addr));

    if (address.ss_family == AF_INET6) {
        const std::uint8_t *bytes = reinterpret_cast<const sockaddr_in6 *>(&address)->sin6_addr.s6_addr;
        static const std::uint8_t kIpv4MappedPrefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        if (std::memcmp(bytes, kIpv4MappedPrefix, sizeof(kIpv4MappedPrefix)) == 0) {
            std::uint32_t ipv4 = 0;
            std::memcpy(&ipv4, bytes + 12, sizeof(ipv4));
            return ipv4_local(ntohl(ipv4));
        }
        static const std::uint8_t kLoopback6[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        if (std::memcmp(bytes, kLoopback6, sizeof(kLoopback6)) == 0) return true;
        /* fc00::/7 (ULA) e fe80::/10 (link-local). */
        return (bytes[0] & 0xfe) == 0xfc || (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80);
    }
    return false;
}

} // namespace cyberdeck_net
