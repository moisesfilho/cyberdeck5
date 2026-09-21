#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef __has_include
#if __has_include("esp_err.h")
#include "features/wifi/wifi_storage.h"
#define CYBERDECK_HAS_WIFI_STORAGE 1
#endif
#endif

#ifndef CYBERDECK_HAS_WIFI_STORAGE
#define WIFI_MAX_SAVED_NETWORKS 16
typedef struct {
    char ssid[33];
    char password[65];
} wifi_cfg_t;

typedef struct {
    wifi_cfg_t items[WIFI_MAX_SAVED_NETWORKS];
    int count;
} wifi_saved_list_t;
#endif

#ifdef __has_include
#if __has_include("esp_wifi_types.h")
#include "esp_wifi_types.h"
#define CYBERDECK_HAS_ESP_WIFI_TYPES 1
#endif
#endif

struct cyberdeck_ap_item_t {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
    bool is_open;
};

class cyberdeck_wifi_search_menu {
public:
    cyberdeck_wifi_search_menu() = default;

    void clear();
    void add_ap(const char *ssid, int8_t rssi, uint8_t authmode, bool is_open);
#ifdef CYBERDECK_HAS_ESP_WIFI_TYPES
    void set_aps(const wifi_ap_record_t *aps, int count);
#endif

    size_t count() const { return m_items.size(); }
    size_t selected_index() const { return m_selected_index; }

    void move_up();
    void move_down();

    const cyberdeck_ap_item_t* selected_item() const;
    std::string render() const;

private:
    std::vector<cyberdeck_ap_item_t> m_items;
    size_t m_selected_index = 0;
};

class cyberdeck_wifi_saved_menu {
public:
    cyberdeck_wifi_saved_menu() = default;

    void clear();
    void add_ssid(const char *ssid);
    void set_list(const wifi_saved_list_t &list);

    size_t count() const { return m_ssids.size(); }
    size_t selected_index() const { return m_selected_index; }

    void move_up();
    void move_down();

    std::string selected_ssid() const;
    bool remove_selected();
    std::string render() const;

private:
    std::vector<std::string> m_ssids;
    size_t m_selected_index = 0;
};
