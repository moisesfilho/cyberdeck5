/*
 * Testes unitarios host-side para a logica pura de menus Wi-Fi.
 * Cobre components/cyberdeck/src/core/cyberdeck_wifi_menu.cpp.
 *
 * Estruturado segundo o padrao AAA (Arrange, Act, Assert) e principios F.I.R.S.T.
 */
#include "cyberdeck_wifi_menu.h"

#include <cstdio>
#include <string>

namespace {

int s_failures = 0;
int s_checks = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++s_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);          \
        }                                                                              \
    } while (0)

#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        ++s_checks;                                                                    \
        if (std::string(actual) != std::string(expected)) {                            \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  expected: '%s' actual: '%s'\n",                   \
                        __FILE__, __LINE__, std::string(expected).c_str(),             \
                        std::string(actual).c_str());                                  \
        }                                                                              \
    } while (0)

void test_search_menu_empty()
{
    cyberdeck_wifi_search_menu menu;
    CHECK(menu.count() == 0);
    CHECK(menu.selected_index() == 0);
    CHECK(menu.selected_item() == nullptr);
    CHECK_EQ(menu.render(), "No Wi-Fi networks found.\n");
}

void test_search_menu_dedup_and_sorting()
{
    cyberdeck_wifi_search_menu menu;

    // Add weak AP
    menu.add_ap("Home_WiFi", -80, 3, false);
    // Add stronger AP with same SSID (should replace weak one)
    menu.add_ap("Home_WiFi", -50, 3, false);
    // Add another AP with stronger signal
    menu.add_ap("Office_5G", -40, 4, false);
    // Add an open AP with weaker signal
    menu.add_ap("Guest_Open", -65, 0, true);

    CHECK(menu.count() == 3);

    // Order should be sorted by RSSI descending: Office_5G (-40), Home_WiFi (-50), Guest_Open (-65)
    menu.move_up(); // Should stay at 0
    CHECK(menu.selected_index() == 0);
    const auto *item0 = menu.selected_item();
    CHECK(item0 != nullptr);
    CHECK_EQ(item0->ssid, "Office_5G");
    CHECK(item0->rssi == -40);

    menu.move_down();
    CHECK(menu.selected_index() == 1);
    const auto *item1 = menu.selected_item();
    CHECK(item1 != nullptr);
    CHECK_EQ(item1->ssid, "Home_WiFi");
    CHECK(item1->rssi == -50);

    menu.move_down();
    CHECK(menu.selected_index() == 2);
    const auto *item2 = menu.selected_item();
    CHECK(item2 != nullptr);
    CHECK_EQ(item2->ssid, "Guest_Open");
    CHECK(item2->is_open == true);

    menu.move_down(); // Should stay at 2 (bounds limit)
    CHECK(menu.selected_index() == 2);
}

void test_search_menu_render_output()
{
    cyberdeck_wifi_search_menu menu;
    menu.add_ap("Net_A", -55, 3, false); // WPA2
    menu.add_ap("Net_B", -70, 0, true);  // OPEN

    std::string expected =
        "Found 2 networks (UP/DOWN navigate, ENTER select, ESC cancel):\n"
        "> [1] Net_A (-55 dBm, WPA2)\n"
        "  [2] Net_B (-70 dBm, OPEN)\n";

    CHECK_EQ(menu.render(), expected);

    menu.move_down();
    expected =
        "Found 2 networks (UP/DOWN navigate, ENTER select, ESC cancel):\n"
        "  [1] Net_A (-55 dBm, WPA2)\n"
        "> [2] Net_B (-70 dBm, OPEN)\n";

    CHECK_EQ(menu.render(), expected);
}

void test_saved_menu_operations()
{
    cyberdeck_wifi_saved_menu menu;
    CHECK(menu.count() == 0);
    CHECK_EQ(menu.render(), "No saved Wi-Fi networks.\n");

    wifi_saved_list_t list = {};
    snprintf(list.items[0].ssid, sizeof(list.items[0].ssid), "Home");
    snprintf(list.items[1].ssid, sizeof(list.items[1].ssid), "Work");
    list.count = 2;

    menu.set_list(list);
    CHECK(menu.count() == 2);
    CHECK_EQ(menu.selected_ssid(), "Home");

    std::string expected =
        "Saved networks (UP/DOWN navigate, ENTER forget, ESC exit):\n"
        "> [1] Home\n"
        "  [2] Work\n";
    CHECK_EQ(menu.render(), expected);

    menu.move_down();
    CHECK_EQ(menu.selected_ssid(), "Work");

    // Remove Work
    CHECK(menu.remove_selected() == true);
    CHECK(menu.count() == 1);
    CHECK_EQ(menu.selected_ssid(), "Home");

    // Remove Home
    CHECK(menu.remove_selected() == true);
    CHECK(menu.count() == 0);
    CHECK_EQ(menu.selected_ssid(), "");
    CHECK(menu.remove_selected() == false);
}

} // namespace

int main()
{
    test_search_menu_empty();
    test_search_menu_dedup_and_sorting();
    test_search_menu_render_output();
    test_saved_menu_operations();

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_wifi_menu (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
