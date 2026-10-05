#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t bssid[6];
    uint8_t bssid_set;
    uint8_t channel;
    int8_t rssi;
    int authmode;
} wifi_ap_record_t;
