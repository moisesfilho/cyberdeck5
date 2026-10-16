#pragma once
#include <stdint.h>
typedef struct {
    uint32_t addr;
} ip4_addr_t;
typedef struct {
    ip4_addr_t ip4;
} ip_addr_t;
#define IP_IS_V4(a) ((a) != 0)
#define ip_2_ip4(a) (&(a)->ip4)
