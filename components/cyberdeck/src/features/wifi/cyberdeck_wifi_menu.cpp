#include "features/wifi/cyberdeck_wifi_menu.h"
#include <algorithm>
#include <cstdio>

static const char* authmode_to_str(uint8_t authmode, bool is_open) {
    if (is_open || authmode == 0) return "OPEN";
    switch (authmode) {
    case 1: return "WEP";
    case 2: return "WPA";
    case 3: return "WPA2";
    case 4: return "WPA/WPA2";
    case 5: return "ENTERPRISE";
    case 6: return "WPA3";
    case 7: return "WPA2/WPA3";
    default: return "SECURE";
    }
}

void cyberdeck_wifi_search_menu::clear() {
    m_items.clear();
    m_selected_index = 0;
}

void cyberdeck_wifi_search_menu::add_ap(const char *ssid, int8_t rssi, uint8_t authmode, bool is_open) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return;
    }

    for (auto &item : m_items) {
        if (strcmp(item.ssid, ssid) == 0) {
            if (rssi > item.rssi) {
                item.rssi = rssi;
                item.authmode = authmode;
                item.is_open = is_open;
            }
            return;
        }
    }

    cyberdeck_ap_item_t ap = {};
    snprintf(ap.ssid, sizeof(ap.ssid), "%s", ssid);
    ap.rssi = rssi;
    ap.authmode = authmode;
    ap.is_open = is_open;
    m_items.push_back(ap);

    std::sort(m_items.begin(), m_items.end(), [](const cyberdeck_ap_item_t &a, const cyberdeck_ap_item_t &b) {
        return a.rssi > b.rssi;
    });
}

#ifdef CYBERDECK_HAS_ESP_WIFI_TYPES
void cyberdeck_wifi_search_menu::set_aps(const wifi_ap_record_t *aps, int count) {
    clear();
    if (aps == nullptr || count <= 0) {
        return;
    }
    for (int i = 0; i < count; i++) {
        const char *ssid = reinterpret_cast<const char*>(aps[i].ssid);
        const bool is_open = (aps[i].authmode == WIFI_AUTH_OPEN);
        add_ap(ssid, aps[i].rssi, static_cast<uint8_t>(aps[i].authmode), is_open);
    }
}
#endif

void cyberdeck_wifi_search_menu::move_up() {
    if (m_selected_index > 0) {
        m_selected_index--;
    }
}

void cyberdeck_wifi_search_menu::move_down() {
    if (!m_items.empty() && m_selected_index + 1 < m_items.size()) {
        m_selected_index++;
    }
}

const cyberdeck_ap_item_t* cyberdeck_wifi_search_menu::selected_item() const {
    if (m_items.empty() || m_selected_index >= m_items.size()) {
        return nullptr;
    }
    return &m_items[m_selected_index];
}

std::string cyberdeck_wifi_search_menu::render() const {
    if (m_items.empty()) {
        return "No Wi-Fi networks found.\n";
    }

    char buf[256];
    std::string out;
    snprintf(buf, sizeof(buf), "Found %zu networks (UP/DOWN navigate, ENTER select, ESC cancel):\n", m_items.size());
    out += buf;

    for (size_t i = 0; i < m_items.size(); i++) {
        const char *marker = (i == m_selected_index) ? "> " : "  ";
        const char *auth = authmode_to_str(m_items[i].authmode, m_items[i].is_open);
        snprintf(buf, sizeof(buf), "%s[%zu] %s (%d dBm, %s)\n", marker, i + 1, m_items[i].ssid, m_items[i].rssi, auth);
        out += buf;
    }
    return out;
}

void cyberdeck_wifi_saved_menu::clear() {
    m_ssids.clear();
    m_selected_index = 0;
}

void cyberdeck_wifi_saved_menu::add_ssid(const char *ssid) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return;
    }
    m_ssids.push_back(ssid);
}

void cyberdeck_wifi_saved_menu::set_list(const wifi_saved_list_t &list) {
    clear();
    for (int i = 0; i < list.count; i++) {
        add_ssid(list.items[i].ssid);
    }
}

void cyberdeck_wifi_saved_menu::move_up() {
    if (m_selected_index > 0) {
        m_selected_index--;
    }
}

void cyberdeck_wifi_saved_menu::move_down() {
    if (!m_ssids.empty() && m_selected_index + 1 < m_ssids.size()) {
        m_selected_index++;
    }
}

std::string cyberdeck_wifi_saved_menu::selected_ssid() const {
    if (m_ssids.empty() || m_selected_index >= m_ssids.size()) {
        return "";
    }
    return m_ssids[m_selected_index];
}

bool cyberdeck_wifi_saved_menu::remove_selected() {
    if (m_ssids.empty() || m_selected_index >= m_ssids.size()) {
        return false;
    }
    m_ssids.erase(m_ssids.begin() + m_selected_index);
    if (m_selected_index >= m_ssids.size() && m_selected_index > 0) {
        m_selected_index--;
    }
    return true;
}

std::string cyberdeck_wifi_saved_menu::render() const {
    if (m_ssids.empty()) {
        return "No saved Wi-Fi networks.\n";
    }

    char buf[256];
    std::string out;
    snprintf(buf, sizeof(buf), "Saved networks (UP/DOWN navigate, ENTER forget, ESC exit):\n");
    out += buf;

    for (size_t i = 0; i < m_ssids.size(); i++) {
        const char *marker = (i == m_selected_index) ? "> " : "  ";
        snprintf(buf, sizeof(buf), "%s[%zu] %s\n", marker, i + 1, m_ssids[i].c_str());
        out += buf;
    }
    return out;
}
