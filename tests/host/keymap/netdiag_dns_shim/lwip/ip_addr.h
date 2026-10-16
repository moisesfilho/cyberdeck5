#pragma once
#include <stdint.h>
struct ip4_addr_t {
    uint32_t addr;
};
struct ip_addr_t {
    bool is_v4;
    ip4_addr_t ip4;
};
#define IP_IS_V4(a) ((a)->is_v4)
#define ip_2_ip4(a) (&(a)->ip4)
