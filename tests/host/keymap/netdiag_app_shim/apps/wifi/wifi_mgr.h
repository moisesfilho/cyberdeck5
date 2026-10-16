#pragma once
#define ESP_OK 0
typedef struct {
    bool has_ip;
    bool connected;
    char ip[16];
} wifi_status_t;
int wifi_mgr_get_status(wifi_status_t *);
