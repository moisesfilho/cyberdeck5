#pragma once
#include <cstdint>
struct ip4_addr_t { std::uint8_t bytes[4]; };
struct ip6_addr_t { std::uint8_t addr[16]; };
struct ip_addr_t { int type; ip4_addr_t v4; ip6_addr_t v6; };
#define IPADDR_TYPE_V4 4
#define IPADDR_TYPE_V6 6
#define IP_IS_V4(a) ((a)->type == IPADDR_TYPE_V4)
#define IP_IS_V6(a) ((a)->type == IPADDR_TYPE_V6)
#define ip_2_ip4(a) (&(a)->v4)
#define ip_2_ip6(a) (&(a)->v6)
#define ip4_addr1(a) ((a)->bytes[0])
#define ip4_addr2(a) ((a)->bytes[1])
#define ip4_addr3(a) ((a)->bytes[2])
#define ip4_addr4(a) ((a)->bytes[3])
