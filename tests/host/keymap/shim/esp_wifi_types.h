#pragma once
/* Host shim for the Wi-Fi driver types referenced by wifi_mgr.h and the
 * Wi-Fi menu.  The shell session never reads these fields itself, but the
 * compiled menu module does, so the layout-relevant members are declared
 * with the real ESP-IDF names. */

#include <stdint.h>

typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WEP = 1,
    WIFI_AUTH_WPA_PSK = 2,
    WIFI_AUTH_WPA2_PSK = 3,
    WIFI_AUTH_WPA_WPA2_PSK = 4,
    WIFI_AUTH_WPA3_PSK = 6,
} wifi_auth_mode_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t bssid[6];
    uint8_t bssid_set;
    uint8_t channel;
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
} wifi_sta_config_t;
