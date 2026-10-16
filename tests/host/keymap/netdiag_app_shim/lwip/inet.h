#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
typedef struct {
    uint32_t addr;
} esp_ip4_addr_t;
char *esp_ip4addr_ntoa(const esp_ip4_addr_t *, char *, int);
