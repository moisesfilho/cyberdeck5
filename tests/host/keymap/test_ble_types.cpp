/*
 * TDD RED host contract for the pure BLE device types, sanitizer and list.
 *
 * Covers REQ-BLE-004 (listing name + type: keyboard / headset / mouse /
 * unknown) and REQ-BLE-010 (bounded, display-safe, secret-free records).
 *
 * The production source is intentionally absent in this tester handoff, so this
 * binary is RED until the coder creates
 * components/cyberdeck/src/features/bluetooth/cyberdeck_ble_types.cpp with the
 * ABI declared in contracts/cyberdeck_ble_types.h.  No ESP-IDF, NimBLE,
 * esp_hosted, LVGL, NVS, simulator or hardware is used here.
 */
#include "cyberdeck_ble_types.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    ++checks; \
    if (!((actual) == (expected))) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #actual); \
    } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    ++checks; \
    const std::string actual_value = (actual); \
    const std::string expected_value = (expected); \
    if (actual_value != expected_value) { \
        ++failures; \
        std::printf("FAIL %s:%d: expected '%s', actual '%s'\n", \
                    __FILE__, __LINE__, expected_value.c_str(), \
                    actual_value.c_str()); \
    } \
} while (0)

using cyberdeck_ble::device;
using cyberdeck_ble::device_kind;
using cyberdeck_ble::device_list;
using cyberdeck_ble::k_max_devices;
using cyberdeck_ble::k_max_name_bytes;
using cyberdeck_ble::k_passkey_digits;
using cyberdeck_ble::k_unnamed_placeholder;

std::string repeated(char value, std::size_t count)
{
    return std::string(count, value);
}

/* Repeats a multi-byte sequence `count` times (used for UTF-8 bounds). */
std::string repeated(const char *unit, std::size_t count)
{
    const std::string piece(unit);
    std::string out;
    out.reserve(piece.size() * count);
    for (std::size_t i = 0; i < count; ++i) out += piece;
    return out;
}

device make_device(const char *address, const char *name, int rssi,
                   device_kind kind, bool paired = false,
                   bool connectable = true,
                   cyberdeck_ble::address_type addr_type =
                       cyberdeck_ble::address_type::public_address)
{
    device item;
    item.address = address;
    item.addr_type = addr_type;
    item.name = name == nullptr ? std::string() : std::string(name);
    item.rssi = rssi;
    item.kind = kind;
    item.paired = paired;
    item.connectable = connectable;
    return item;
}

void test_approved_constants_and_labels()
{
    CHECK_EQ(cyberdeck_ble::k_max_name_bytes, std::size_t(32));
    CHECK_EQ(cyberdeck_ble::k_max_address_bytes, std::size_t(17));
    CHECK_EQ(k_max_devices, std::size_t(32));
    CHECK_EQ(cyberdeck_ble::k_min_rssi, -120);
    CHECK_EQ(cyberdeck_ble::k_max_rssi, 20);
    CHECK_EQ(k_passkey_digits, std::uint32_t(6));
    CHECK_EQ(cyberdeck_ble::k_passkey_modulus, std::uint32_t(1000000));
    CHECK_EQ(device_list::max_devices(), k_max_devices);

    /* REQ-BLE-004: the four approved list types with stable, exact labels. */
    CHECK_STR(cyberdeck_ble::kind_label(device_kind::keyboard), "Keyboard");
    CHECK_STR(cyberdeck_ble::kind_label(device_kind::headset), "Headset");
    CHECK_STR(cyberdeck_ble::kind_label(device_kind::mouse), "Mouse");
    CHECK_STR(cyberdeck_ble::kind_label(device_kind::unknown), "Unknown");

    /* No secret may ever become part of a device record: the type has no key
     * material fields at all, which is a stronger guarantee than a redaction. */
    const device plain = make_device("AA:BB:CC:DD:EE:FF", "Keyboard", -55,
                                    device_kind::keyboard);
    CHECK_STR(plain.address, "AA:BB:CC:DD:EE:FF");
    CHECK(plain.addr_type == cyberdeck_ble::address_type::public_address);
    CHECK_STR(plain.name, "Keyboard");
    CHECK_EQ(plain.rssi, -55);
    CHECK(plain.kind == device_kind::keyboard);
    CHECK(plain.connectable);
    CHECK(!plain.paired);
}

void test_eighteen_items_keep_selection_and_marker_safe()
{
    device_list list;
    for (std::size_t i = 0; i < 18; ++i) {
        char address[18];
        std::snprintf(address, sizeof(address), "AA:BB:CC:DD:EE:%02zX", i);
        const std::string name = "device-" + std::to_string(i);
        CHECK(list.add(make_device(address, name.c_str(), -40,
                                   device_kind::unknown)));
    }
    CHECK_EQ(list.size(), std::size_t(18));
    CHECK_EQ(list.selected_index(), std::size_t(0));
    CHECK(list.selected() != nullptr);
    CHECK_STR(list.selected()->name, "device-0");

    for (int i = 0; i < 17; ++i) list.move_down();
    CHECK_EQ(list.selected_index(), std::size_t(17));
    CHECK(list.selected() != nullptr);
    CHECK_STR(list.selected()->name, "device-17");
    CHECK(list.render().find("> [18] device-17") != std::string::npos);

    list.move_down();
    CHECK_EQ(list.selected_index(), std::size_t(17));
    list.move_up();
    CHECK_EQ(list.selected_index(), std::size_t(16));
    CHECK(list.render().find("> [17] device-16") != std::string::npos);
}

void test_address_type_is_part_of_device_identity()
{
    using cyberdeck_ble::address_type;
    device_list list;
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "Public", -50,
                               device_kind::unknown, false, true,
                               address_type::public_address)));
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "Random", -60,
                               device_kind::unknown, false, true,
                               address_type::random_static)));
    CHECK_EQ(list.size(), std::size_t(2));
    CHECK(list.find("AA:BB:CC:DD:EE:01", address_type::public_address) != nullptr);
    CHECK(list.find("AA:BB:CC:DD:EE:01", address_type::random_static) != nullptr);
    CHECK(list.find("AA:BB:CC:DD:EE:01", address_type::random_resolvable) == nullptr);
}

void test_kind_from_appearance_only_proves_what_is_advertised()
{
    using cyberdeck_ble::kind_from_appearance;

    CHECK(kind_from_appearance(cyberdeck_ble::k_appearance_keyboard) ==
          device_kind::keyboard);
    CHECK(kind_from_appearance(cyberdeck_ble::k_appearance_mouse) ==
          device_kind::mouse);

    CHECK(kind_from_appearance(cyberdeck_ble::k_appearance_wearable_headset) ==
          device_kind::headset);
    CHECK(kind_from_appearance(cyberdeck_ble::k_appearance_headset_mic) ==
          device_kind::headset);
    CHECK(kind_from_appearance(cyberdeck_ble::k_appearance_headphones) ==
          device_kind::headset);
    CHECK(kind_from_appearance(cyberdeck_ble::k_appearance_ear_headset) ==
          device_kind::headset);
    CHECK(kind_from_appearance(cyberdeck_ble::k_appearance_handsfree) ==
          device_kind::headset);
    CHECK(kind_from_appearance(cyberdeck_ble::k_appearance_handsfree_mic) ==
          device_kind::headset);

    /* Everything the classifier cannot prove is "Unknown", never a guess. */
    CHECK(kind_from_appearance(cyberdeck_ble::k_appearance_generic_hid) ==
          device_kind::unknown);
    CHECK(kind_from_appearance(0x0000) == device_kind::unknown);
    CHECK(kind_from_appearance(0x03C3) == device_kind::unknown); /* joystick */
    CHECK(kind_from_appearance(0x03C4) == device_kind::unknown); /* gamepad  */
    CHECK(kind_from_appearance(0x0400) == device_kind::unknown); /* generic AV */
    CHECK(kind_from_appearance(0xFFFF) == device_kind::unknown);
    CHECK(kind_from_appearance(0x03C1U | 0x0800U) == device_kind::unknown);

    /* Classification is a pure function of the appearance value. */
    for (std::uint16_t value = 0; value < 0x0500; value = static_cast<std::uint16_t>(value + 1)) {
        const device_kind first = kind_from_appearance(value);
        CHECK(first == kind_from_appearance(value));
        const char *label = cyberdeck_ble::kind_label(first);
        CHECK(label != nullptr && label[0] != '\0');
    }
}

void expect_address(const char *raw, std::size_t len, const char *expected)
{
    std::string out = "unchanged";
    const bool ok = cyberdeck_ble::normalize_address(raw, len, out);
    CHECK(ok);
    if (ok) {
        CHECK_STR(out, expected);
        CHECK(out.size() <= cyberdeck_ble::k_max_address_bytes);
    }
}

void expect_address_rejected(const char *raw, std::size_t len)
{
    std::string out = "unchanged";
    const bool ok = cyberdeck_ble::normalize_address(raw, len, out);
    CHECK(!ok);
    if (!ok) {
        CHECK_STR(out, "unchanged");
    }
}

void test_address_normalization_is_strict_and_normalizing()
{
    expect_address("AA:BB:CC:DD:EE:FF", 17, "AA:BB:CC:DD:EE:FF");
    expect_address("aa:bb:cc:dd:ee:ff", 17, "AA:BB:CC:DD:EE:FF");
    expect_address("Aa:bB:cC:dD:eE:fF", 17, "AA:BB:CC:DD:EE:FF");
    expect_address("AA-BB-CC-DD-EE-FF", 17, "AA:BB:CC:DD:EE:FF");
    expect_address("aa-bb-cc-dd-ee-ff", 17, "AA:BB:CC:DD:EE:FF");
    expect_address("aabbccddeeff", 12, "AA:BB:CC:DD:EE:FF");
    expect_address("AABBCCDDEEFF", 12, "AA:BB:CC:DD:EE:FF");

    expect_address_rejected(nullptr, 0);
    expect_address_rejected("", 0);
    expect_address_rejected("AA:BB:CC:DD:EE", 14);
    expect_address_rejected("AA:BB:CC:DD:EE:F", 16);
    expect_address_rejected("AA:BB:CC:DD:EE:FF:00", 20);
    expect_address_rejected("GG:BB:CC:DD:EE:FF", 17);
    expect_address_rejected("AA:BB:CC:DD:EE:FZ", 17);
    expect_address_rejected("AA:BB:CC:DD:EE:FFX", 18);
    expect_address_rejected("AA:BB:CC:DD:EE:FF\0Z", 18);
    expect_address_rejected("AA BB CC DD EE FF", 17);
    expect_address_rejected("aabbccddeef", 11);
    expect_address_rejected("aabbccddeeffaa", 14);
    expect_address_rejected(":aabbccddeeff", 13);
}

void test_name_sanitizer_is_bounded_utf8_safe_and_control_free()
{
    using cyberdeck_ble::sanitize_name;

    CHECK_STR(sanitize_name("Keyboard", 8), "Keyboard");
    CHECK_STR(sanitize_name("Fone de ouvido", 14), "Fone de ouvido");

    /* Missing, empty and fully stripped names fall back to the placeholder. */
    CHECK_STR(sanitize_name(nullptr, 0), k_unnamed_placeholder);
    CHECK_STR(sanitize_name("", 0), k_unnamed_placeholder);
    CHECK_STR(sanitize_name("   ", 3), k_unnamed_placeholder);
    CHECK_STR(sanitize_name("\t\r\n", 3), k_unnamed_placeholder);
    CHECK_STR(sanitize_name("\x01\x02\x7F", 3), k_unnamed_placeholder);

    /* A name can never inject a line break or an escape into the TUI.
     * The escape sequences are split across string literals on purpose: a
     * following hex digit would otherwise be swallowed by \x parsing. */
    CHECK_STR(sanitize_name("Key\nboard", 9), "Key board");
    CHECK_STR(sanitize_name("Key\tboard", 9), "Key board");
    CHECK_STR(sanitize_name("Key\r\nboard", 10), "Key  board");
    CHECK_STR(sanitize_name("\x1b[31mRed", 8), " [31mRed");
    CHECK_STR(sanitize_name("  padded  ", 10), "padded");
    CHECK_STR(sanitize_name("Fone\xC2\x85" "de", 8), "Fone de");   /* U+0085 C1 */
    CHECK_STR(sanitize_name("A\xE2\x80\xA8" "B", 5), "A B");        /* U+2028   */
    CHECK_STR(sanitize_name("A\xE2\x80\xA9" "B", 5), "A B");        /* U+2029   */
    CHECK_STR(sanitize_name("Fone\xFF" "de", 7), "Fone de");        /* bad byte */

    /* Bounded: 40 ASCII bytes clamp to exactly k_max_name_bytes. */
    const std::string long_ascii = sanitize_name(repeated('a', 40).c_str(), 40);
    CHECK_EQ(long_ascii.size(), k_max_name_bytes);
    CHECK_STR(long_ascii, repeated('a', k_max_name_bytes));

    const std::string exact = sanitize_name(repeated('b', k_max_name_bytes).c_str(),
                                            k_max_name_bytes);
    CHECK_EQ(exact.size(), k_max_name_bytes);
    CHECK_STR(exact, repeated('b', k_max_name_bytes));

    /* Truncation never splits a UTF-8 sequence: 9 x 4-byte emoji (36 bytes)
     * clamp to 8 emoji (32 bytes), and the result stays valid UTF-8. */
    const std::string bounded =
        sanitize_name("\xF0\x9F\x94\xA5", 4);
    CHECK_EQ(bounded.size(), std::size_t(4));
    CHECK_STR(bounded, "\xF0\x9F\x94\xA5");

    std::string emoji;
    for (int i = 0; i < 9; ++i) emoji += "\xF0\x9F\x94\xA5";
    const std::string bounded_emoji = sanitize_name(emoji.c_str(), emoji.size());
    CHECK_EQ(bounded_emoji.size(), k_max_name_bytes);
    CHECK_STR(bounded_emoji, repeated("\xF0\x9F\x94\xA5", 8));

    /* Truncation never splits a UTF-8 sequence: 11 x 3-byte code points (33
     * bytes) clamp to 10 code points (30 bytes); the dangling 0xE2 lead byte of
     * the eleventh is dropped instead of being emitted. */
    std::string three_byte;
    for (int i = 0; i < 11; ++i) three_byte += "\xE2\x96\xA0";
    CHECK_EQ(three_byte.size(), std::size_t(33));
    const std::string bounded_three = sanitize_name(three_byte.c_str(), three_byte.size());
    CHECK_EQ(bounded_three.size(), std::size_t(30));
    CHECK(bounded_three.size() <= k_max_name_bytes);
    CHECK_STR(bounded_three, repeated("\xE2\x96\xA0", 10));

    /* Embedded NUL is a control character, not a terminator. */
    const char with_nul[] = {'a', 'b', '\0', 'c'};
    CHECK_STR(sanitize_name(with_nul, 4), "ab c");
}

void test_display_name_and_rssi_clamping()
{
    using cyberdeck_ble::clamp_rssi;
    using cyberdeck_ble::display_name;

    CHECK_EQ(clamp_rssi(-127), cyberdeck_ble::k_min_rssi);
    CHECK_EQ(clamp_rssi(0), 0);
    CHECK_EQ(clamp_rssi(-55), -55);
    CHECK_EQ(clamp_rssi(20), 20);
    CHECK_EQ(clamp_rssi(127), cyberdeck_ble::k_max_rssi);
    CHECK_EQ(clamp_rssi(-1000), cyberdeck_ble::k_min_rssi);

    CHECK_STR(display_name(make_device("AA:BB:CC:DD:EE:01", "Fone", -50,
                                      device_kind::headset)),
              "Fone");
    CHECK_STR(display_name(make_device("AA:BB:CC:DD:EE:02", nullptr, -50,
                                      device_kind::unknown)),
              k_unnamed_placeholder);
    CHECK_STR(display_name(make_device("AA:BB:CC:DD:EE:03", "", -50,
                                      device_kind::unknown)),
              k_unnamed_placeholder);
    CHECK_STR(display_name(make_device("AA:BB:CC:DD:EE:04", "Mouse BT", -50,
                                      device_kind::mouse)),
              "Mouse BT");
}

void test_passkey_helpers_are_strict_display_only_and_masked()
{
    using cyberdeck_ble::format_passkey;
    using cyberdeck_ble::mask_passkey;
    using cyberdeck_ble::parse_passkey;

    std::uint32_t value = 777;
    CHECK(parse_passkey("123456", 6, value));
    CHECK_EQ(value, std::uint32_t(123456));

    value = 777;
    CHECK(parse_passkey("000123", 6, value));
    CHECK_EQ(value, std::uint32_t(123));

    value = 777;
    CHECK(parse_passkey("  246813\t", 9, value));
    CHECK_EQ(value, std::uint32_t(246813));

    value = 777;
    CHECK(parse_passkey("000000", 6, value));
    CHECK_EQ(value, std::uint32_t(0));

    const char *rejected[] = {"", "12345", "1234567", "12345a", "+12345",
                              "12 345", "abcdef", "-12345", "12345."};
    for (const char *text : rejected) {
        value = 777;
        CHECK(!parse_passkey(text, std::string(text).size(), value));
        CHECK_EQ(value, std::uint32_t(777));
    }
    value = 777;
    CHECK(!parse_passkey(nullptr, 0, value));
    CHECK_EQ(value, std::uint32_t(777));

    CHECK_STR(format_passkey(0), "000000");
    CHECK_STR(format_passkey(1), "000001");
    CHECK_STR(format_passkey(123), "000123");
    CHECK_STR(format_passkey(999999), "999999");
    CHECK_STR(format_passkey(1000000), "");
    CHECK_STR(format_passkey(4294967295U), "");

    /* The masked projection is constant: it cannot leak by construction. */
    CHECK_STR(mask_passkey(0), "******");
    CHECK_STR(mask_passkey(123456), "******");
    CHECK_STR(mask_passkey(999999), "******");
    CHECK(mask_passkey(246813).find("246813") == std::string::npos);
    CHECK(mask_passkey(246813).find("6") == std::string::npos);
}

void test_list_dedup_is_by_address_and_keeps_the_strongest_signal()
{
    device_list list;
    CHECK_EQ(list.size(), std::size_t(0));
    CHECK_EQ(list.capacity(), k_max_devices);
    CHECK(list.selected() == nullptr);
    CHECK(list.find("AA:BB:CC:DD:EE:FF") == nullptr);

    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "Fone", -80,
                               device_kind::headset)));
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:02", "Fone", -60,
                               device_kind::headset)));
    CHECK_EQ(list.size(), std::size_t(2));

    /* Same address, weaker signal: rssi and kind are not downgraded, and the
     * existing non-empty name is preserved. */
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "", -95,
                               device_kind::unknown)));
    const device *first = list.find("AA:BB:CC:DD:EE:01");
    CHECK(first != nullptr);
    if (first != nullptr) {
        CHECK_EQ(first->rssi, -80);
        CHECK(first->kind == device_kind::headset);
        CHECK_STR(first->name, "Fone");
    }

    /* Same address, stronger signal: the RSSI is promoted, an empty name never
     * overwrites a usable one, and a second different name never replaces the
     * first one (a peripheral may rotate its local name). */
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "Fone Pro", -45,
                               device_kind::headset)));
    first = list.find("AA:BB:CC:DD:EE:01");
    CHECK(first != nullptr);
    if (first != nullptr) {
        CHECK_EQ(first->rssi, -45);
        CHECK_STR(first->name, "Fone");
    }
    CHECK_EQ(list.size(), std::size_t(2));

    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "Fone Pro Plus", -120,
                               device_kind::headset)));
    first = list.find("AA:BB:CC:DD:EE:01");
    CHECK(first != nullptr);
    if (first != nullptr) {
        CHECK_EQ(first->rssi, -45);
        CHECK_STR(first->name, "Fone");
    }

    /* An existing unknown kind is promoted by a proven one, never downgraded
     * back, and connectable is a monotone demotion. */
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:04", "Beacon", -40,
                               device_kind::unknown, false, false)));
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:04", "Beacon", -50,
                               device_kind::keyboard, true, true)));
    const device *beacon = list.find("AA:BB:CC:DD:EE:04");
    CHECK(beacon != nullptr);
    if (beacon != nullptr) {
        CHECK(beacon->kind == device_kind::keyboard);
        CHECK(beacon->paired);
        CHECK(!beacon->connectable);
    }

    /* Unusable addresses are rejected and never inserted. */
    CHECK(!list.add(make_device("", "NoAddr", -50, device_kind::unknown)));
    CHECK(!list.add(make_device("not-a-mac", "Bad", -50, device_kind::unknown)));
    CHECK_EQ(list.size(), std::size_t(3));
}

void test_list_capacity_is_bounded_and_selection_clamps()
{
    device_list list;
    for (std::size_t i = 0; i < k_max_devices; ++i) {
        char address[18];
        std::snprintf(address, sizeof(address), "AA:BB:CC:DD:%02X:%02X",
                      static_cast<unsigned>(i >> 8), static_cast<unsigned>(i & 0xFF));
        CHECK(list.add(make_device(address, "Device", -40,
                                   device_kind::keyboard)));
    }
    CHECK_EQ(list.size(), k_max_devices);

    char overflow_address[18];
    std::snprintf(overflow_address, sizeof(overflow_address), "AA:BB:CC:DD:FF:FF");
    CHECK(!list.add(make_device(overflow_address, "Overflow", -40,
                                device_kind::mouse)));
    CHECK_EQ(list.size(), k_max_devices);
    CHECK(list.find("AA:BB:CC:DD:FF:FF") == nullptr);

    CHECK_EQ(list.selected_index(), std::size_t(0));
    list.move_up();
    CHECK_EQ(list.selected_index(), std::size_t(0));
    for (std::size_t i = 0; i < k_max_devices + 5; ++i) list.move_down();
    CHECK_EQ(list.selected_index(), k_max_devices - 1);
    list.move_down();
    CHECK_EQ(list.selected_index(), k_max_devices - 1);

    list.select(7);
    CHECK_EQ(list.selected_index(), std::size_t(7));
    CHECK(list.selected() != nullptr);
    list.select(k_max_devices);
    CHECK_EQ(list.selected_index(), std::size_t(7));
    list.select(9999);
    CHECK_EQ(list.selected_index(), std::size_t(7));

    CHECK(list.at(0) != nullptr);
    CHECK(list.at(k_max_devices) == nullptr);
    CHECK(list.at(9999) == nullptr);

    const std::vector<device> snapshot = list.snapshot();
    CHECK_EQ(snapshot.size(), k_max_devices);
    if (!snapshot.empty()) {
        CHECK_STR(snapshot[0].address, "AA:BB:CC:DD:00:00");
    }
}

void test_list_removal_keeps_selection_inside_bounds()
{
    device_list list;
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "A", -40, device_kind::keyboard)));
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:02", "B", -41, device_kind::mouse)));
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:03", "C", -42, device_kind::headset)));

    list.move_down();
    list.move_down();
    CHECK_EQ(list.selected_index(), std::size_t(2));

    CHECK(list.remove("AA:BB:CC:DD:EE:03"));
    CHECK_EQ(list.size(), std::size_t(2));
    CHECK_EQ(list.selected_index(), std::size_t(1));
    CHECK(list.selected() != nullptr);
    if (list.selected() != nullptr) {
        CHECK_STR(list.selected()->address, "AA:BB:CC:DD:EE:02");
    }

    CHECK(!list.remove("AA:BB:CC:DD:EE:FF"));
    CHECK(!list.remove(""));

    CHECK(list.remove("AA:BB:CC:DD:EE:02"));
    CHECK_EQ(list.size(), std::size_t(1));
    CHECK_EQ(list.selected_index(), std::size_t(0));
    CHECK(list.selected() != nullptr);
    if (list.selected() != nullptr) {
        CHECK_STR(list.selected()->address, "AA:BB:CC:DD:EE:01");
    }

    list.clear();
    CHECK_EQ(list.size(), std::size_t(0));
    CHECK_EQ(list.selected_index(), std::size_t(0));
    CHECK(list.selected() == nullptr);
}

void test_list_render_is_deterministic_and_shows_name_plus_type()
{
    device_list empty;
    CHECK_STR(empty.render(), "No Bluetooth devices found.\n");

    device_list list;
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "Fone", -55,
                               device_kind::headset)));
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:02", nullptr, -70,
                               device_kind::unknown)));
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:03", "Mouse", -60,
                               device_kind::mouse, true)));

    const std::string first_pass = list.render();
    CHECK_STR(first_pass,
              "Found 3 Bluetooth devices (UP/DOWN navigate, ENTER pair, ESC cancel):\n"
              "> [1] Fone (Headset, -55 dBm)\n"
              "  [2] (unnamed) (Unknown, -70 dBm)\n"
              "  [3] Mouse (Mouse, -60 dBm)\n");

    /* Rendering is a pure projection: it never reorders or mutates the list. */
    CHECK_STR(list.render(), first_pass);
    CHECK_STR(list.snapshot().at(0).name, "Fone");

    list.move_down();
    CHECK_STR(list.render(),
              "Found 3 Bluetooth devices (UP/DOWN navigate, ENTER pair, ESC cancel):\n"
              "  [1] Fone (Headset, -55 dBm)\n"
              "> [2] (unnamed) (Unknown, -70 dBm)\n"
              "  [3] Mouse (Mouse, -60 dBm)\n");

    /* A device that disappears from the list cannot stay selected. */
    CHECK(list.remove("AA:BB:CC:DD:EE:02"));
    CHECK_EQ(list.selected_index(), std::size_t(1));
    CHECK(list.selected() != nullptr);
    if (list.selected() != nullptr) {
        CHECK_STR(list.selected()->address, "AA:BB:CC:DD:EE:03");
    }
    CHECK(list.render().find("(unnamed)") == std::string::npos);
}

void test_duplicate_names_are_distinguished_only_by_address()
{
    device_list list;
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "Fone", -55,
                               device_kind::headset)));
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:02", "Fone", -56,
                               device_kind::headset)));
    CHECK_EQ(list.size(), std::size_t(2));

    const std::string rendered = list.render();
    const std::size_t first = rendered.find("Fone (Headset, -55 dBm)");
    const std::size_t second = rendered.find("Fone (Headset, -56 dBm)");
    CHECK(first != std::string::npos);
    CHECK(second != std::string::npos);
    CHECK(first < second);

    /* Names are never an identity key. */
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:03", "Fone", -57,
                               device_kind::headset)));
    CHECK_EQ(list.size(), std::size_t(3));
    CHECK(list.remove("AA:BB:CC:DD:EE:01"));
    CHECK_EQ(list.size(), std::size_t(2));
    CHECK(list.find("AA:BB:CC:DD:EE:02") != nullptr);
}

void test_non_connectable_devices_are_kept_but_marked()
{
    device_list list;
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "Beacon", -40,
                               device_kind::unknown, false, false)));
    CHECK_EQ(list.size(), std::size_t(1));
    const device *item = list.find("AA:BB:CC:DD:EE:01");
    CHECK(item != nullptr);
    if (item != nullptr) {
        CHECK(!item->connectable);
    }
    /* A non-connectable advertisement is still listed; only the state machine
     * is allowed to refuse to pair it. */
    CHECK(list.render().find("Beacon") != std::string::npos);
}

/* REQ-COV-01/TEST-COV-TYPES: deterministic edge coverage for the real missed
 * gcov branches (address separators/trimming, UTF-8 lead/continuation paths,
 * dedup promotion and capacity guards). */
void test_coverage_addresses_separators_and_case_forms()
{
    expect_address("AA:BB:CC:DD:EE:00", 17, "AA:BB:CC:DD:EE:00");
    expect_address("aa-bb-cc-dd-ee-ff", 17, "AA:BB:CC:DD:EE:FF");
    expect_address("aAbBcCdDeEfF", 12, "AA:BB:CC:DD:EE:FF");
    /* Leading/trailing/double separators and embedded blanks are rejected. */
    expect_address_rejected(":AA:BB:CC:DD:EE:FF", 18);
    expect_address_rejected("AA:BB:CC:DD:EE:FF:", 18);
    expect_address_rejected("AA::BB:CC:DD:EE:FF", 18);
    expect_address_rejected("AA--BB-CC-DD-EE-FF", 18);
    expect_address_rejected("AA:BB:CC:DD:EE:FF ", 18);
    expect_address_rejected("AA:BB:CC:DD:EE:FF\t", 18);
    expect_address_rejected(" AA:BB:CC:DD:EE:FF", 18);
    expect_address_rejected("AA:BB:CC:DD:EE:FG", 17);
    expect_address_rejected("AA:BB:CC:DD:EE:F", 16);
}

void test_coverage_sanitize_utf8_boundaries_and_control_set()
{
    using cyberdeck_ble::sanitize_name;
    /* Two-byte sequence split at the end and invalid continuations. */
    {
        const char truncated[] = {'A', 'B', static_cast<char>(0xC3)};
        CHECK_STR(sanitize_name(truncated, sizeof(truncated)), "AB");
    }
    {
        const char bad_cont[] = {'A', static_cast<char>(0xC3), 'X', 'B'};
        CHECK_STR(sanitize_name(bad_cont, sizeof(bad_cont)), "A XB");
    }
    {
        /* Overlong "/" (0xC0 0xAF) after content becomes one space. */
        const char overlong[] = {'A', static_cast<char>(0xC0), static_cast<char>(0xAF)};
        CHECK_STR(sanitize_name(overlong, sizeof(overlong)), "A");
    }
    /* Three-byte truncated/invalid/surrogate paths. */
    {
        const char truncated[] = {'A', static_cast<char>(0xE2), static_cast<char>(0x96)};
        CHECK_STR(sanitize_name(truncated, sizeof(truncated)), "A");
    }
    {
        const char bad[] = {'A', static_cast<char>(0xE2), 'X',
                            static_cast<char>(0xA0), 'B'};
        CHECK_STR(sanitize_name(bad, sizeof(bad)), "A X B");
    }
    {
        /* U+D800 surrogate encoded in three bytes is rejected. */
        const char surrogate[] = {'A', static_cast<char>(0xED), static_cast<char>(0xA0),
                                  static_cast<char>(0x80), 'B'};
        CHECK_STR(sanitize_name(surrogate, sizeof(surrogate)), "A B");
    }
    /* Four-byte truncated/invalid/range paths. */
    {
        const char truncated[] = {'A', static_cast<char>(0xF0), static_cast<char>(0x9F),
                                  static_cast<char>(0x94)};
        CHECK_STR(sanitize_name(truncated, sizeof(truncated)), "A");
    }
    {
        const char bad[] = {'A', static_cast<char>(0xF0), 'X',
                            static_cast<char>(0x9F), static_cast<char>(0x94),
                            static_cast<char>(0xA5), 'B'};
        CHECK_STR(sanitize_name(bad, sizeof(bad)), "A X   B");
    }
    {
        /* Code point beyond U+10FFFF (0xF4 0x90 ...) is rejected. */
        const char out_of_range[] = {'A', static_cast<char>(0xF4), static_cast<char>(0x90),
                                     static_cast<char>(0x80), static_cast<char>(0x80), 'B'};
        CHECK_STR(sanitize_name(out_of_range, sizeof(out_of_range)), "A B");
    }
    /* Bare continuation byte mid-string and control saturation at the cap. */
    {
        const char stray[] = {'A', static_cast<char>(0x80), 'B'};
        CHECK_STR(sanitize_name(stray, sizeof(stray)), "A B");
    }
    CHECK_STR(sanitize_name("\x7F\x80\x9F\xC2\x85", 5), k_unnamed_placeholder);
    CHECK_STR(sanitize_name("ab\x01\x02\x03", 5), "ab");
}

void test_coverage_passkey_and_list_promotion_guards()
{
    using cyberdeck_ble::parse_passkey;
    std::uint32_t value = 777;
    /* Interior tab is not valid; max representable value passes; the modulus
     * itself and longer trimmed input are rejected without clobbering out. */
    CHECK(!parse_passkey("123\t56", 6, value));
    CHECK_EQ(value, std::uint32_t(777));
    CHECK(parse_passkey("999999", 6, value));
    CHECK_EQ(value, std::uint32_t(999999));
    CHECK(!parse_passkey("1000000", 7, value));
    CHECK_EQ(value, std::uint32_t(999999));
    CHECK(!parse_passkey("\t 12 34 \t", 9, value));
    CHECK_EQ(value, std::uint32_t(999999));
    CHECK(parse_passkey("\t 123456 \t", 10, value));
    CHECK_EQ(value, std::uint32_t(123456));

    /* Dedup: connectable monotone demotion stays false and typed find misses. */
    device_list list;
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "", -50,
                               device_kind::unknown)));
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:01", "Named", -40,
                               device_kind::keyboard)));
    const device *merged = list.find("AA:BB:CC:DD:EE:01");
    CHECK(merged != nullptr);
    if (merged != nullptr) {
        CHECK_STR(merged->name, "Named");
        CHECK(merged->kind == device_kind::keyboard);
        CHECK_EQ(merged->rssi, -40);
    }
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:02", "Beacon", -40,
                               device_kind::unknown, false, false)));
    /* A stronger RSSI on the same non-connectable peer is promoted but the
     * monotone demotion keeps connectable false. */
    CHECK(list.add(make_device("AA:BB:CC:DD:EE:02", "Beacon", -30,
                               device_kind::unknown, true, true)));
    const device *beacon = list.find("AA:BB:CC:DD:EE:02");
    CHECK(beacon != nullptr);
    if (beacon != nullptr) CHECK(!beacon->connectable);
    CHECK(list.find("AA:BB:CC:DD:EE:01",
                    cyberdeck_ble::address_type::random_static) == nullptr);
    /* Overlong stored address and typed miss on empty list. */
    CHECK(!list.add(make_device("AA:BB:CC:DD:EE:FF:00", "Long", -40,
                                device_kind::mouse)));
    device_list empty;
    CHECK(empty.find("AA:BB:CC:DD:EE:01",
                     cyberdeck_ble::address_type::public_address) == nullptr);
}

/* REQ-COV-02/TEST-COV-TYPES-02: punctuation, case, invalid and overlong
 * address forms through the public normalize_address API. */
void test_coverage_address_punctuation_case_and_overlong()
{
    expect_address("AA-BB-CC-DD-EE-FF", 17, "AA:BB:CC:DD:EE:FF");
    expect_address("aA-bB-cC-dD-eE-fF", 17, "AA:BB:CC:DD:EE:FF");
    expect_address("aabbccDDEEFF", 12, "AA:BB:CC:DD:EE:FF");
    expect_address("00:11:22:33:44:55", 17, "00:11:22:33:44:55");
    /* Dots, slashes, underscores and blanks are never separators. */
    expect_address_rejected("AA.BB.CC.DD.EE.FF", 17);
    expect_address_rejected("AA/BB/CC/DD/EE/FF", 17);
    expect_address_rejected("AA_BB_CC_DD_EE_FF", 17);
    expect_address_rejected("AA:BB:CC:DD:EE:FF\n", 18);
    expect_address_rejected("AA:BB:CC:DD:EE:FF\r", 18);
    expect_address_rejected("AA:BB:CC:DD:EE:0G", 17);
    expect_address_rejected("AA:BB:CC:DD:EE:ZZ", 17);
    /* Overlong bare and colon forms fail without touching out. */
    expect_address_rejected("AABBCCDDEEFF0", 13);
    expect_address_rejected("AABBCCDDEEFF00", 14);
    expect_address_rejected("00:11:22:33:44:55:66", 20);
}

void test_coverage_sanitize_leading_overlong_surrogate_trim_truncate()
{
    using cyberdeck_ble::sanitize_name;
    /* Leading invalid/overlong/surrogate bytes are skipped, not blanked. */
    {
        const char leading_bad[] = {static_cast<char>(0xFF), 'H', 'e', 'l', 'l', 'o'};
        CHECK_STR(sanitize_name(leading_bad, sizeof(leading_bad)), "Hello");
    }
    {
        const char leading_overlong[] = {static_cast<char>(0xC0), static_cast<char>(0xAF),
                                         'H', 'i'};
        CHECK_STR(sanitize_name(leading_overlong, sizeof(leading_overlong)), "Hi");
    }
    {
        const char leading_surrogate[] = {static_cast<char>(0xED), static_cast<char>(0xA0),
                                          static_cast<char>(0x80), 'H', 'i'};
        CHECK_STR(sanitize_name(leading_surrogate, sizeof(leading_surrogate)), "Hi");
    }
    {
        const char leading_stray[] = {static_cast<char>(0x80), 'H', 'i'};
        CHECK_STR(sanitize_name(leading_stray, sizeof(leading_stray)), "Hi");
    }
    /* Leading truncated sequences collapse to the placeholder. */
    {
        const char truncated_two[] = {static_cast<char>(0xC3)};
        CHECK_STR(sanitize_name(truncated_two, sizeof(truncated_two)), k_unnamed_placeholder);
    }
    {
        const char truncated_three[] = {static_cast<char>(0xE2), static_cast<char>(0x96)};
        CHECK_STR(sanitize_name(truncated_three, sizeof(truncated_three)),
                  k_unnamed_placeholder);
    }
    {
        const char truncated_four[] = {static_cast<char>(0xF0), static_cast<char>(0x9F),
                                       static_cast<char>(0x94)};
        CHECK_STR(sanitize_name(truncated_four, sizeof(truncated_four)),
                  k_unnamed_placeholder);
    }
    /* Trailing controls are saturated into the trim, not the output. */
    CHECK_STR(sanitize_name("hi\x7F", 3), "hi");
    CHECK_STR(sanitize_name("hi\xC2\x85", 4), "hi");
    CHECK_STR(sanitize_name("hi  ", 4), "hi");
    CHECK_STR(sanitize_name("  hi", 4), "hi");
    /* Saturation at the cap: extra controls and blanks never grow output. */
    {
        std::string full = repeated('a', k_max_name_bytes);
        std::string with_control = full + "\x01";
        CHECK_STR(sanitize_name(with_control.c_str(), with_control.size()), full);
        std::string with_blanks = full + "   ";
        CHECK_STR(sanitize_name(with_blanks.c_str(), with_blanks.size()), full);
    }
    /* Truncation keeps whole code points: 17 x 2-byte units clamp to 16. */
    CHECK_STR(sanitize_name(repeated("\xC3\xA9", 16).c_str(), 32),
              repeated("\xC3\xA9", 16));
    CHECK_STR(sanitize_name(repeated("\xC3\xA9", 17).c_str(), 34),
              repeated("\xC3\xA9", 16));
    /* A 4-byte unit that no longer fits stops the copy, keeping ASCII prefix. */
    {
        const std::string ascii31 = repeated('a', 31);
        const std::string mixed = ascii31 + "\xF0\x9F\x94\xA5";
        CHECK_STR(sanitize_name(mixed.c_str(), mixed.size()), ascii31);
    }
}

void test_coverage_passkey_trims_malformed_max()
{
    using cyberdeck_ble::parse_passkey;
    std::uint32_t value = 777;
    CHECK(parse_passkey(" 123456 ", 8, value));
    CHECK_EQ(value, std::uint32_t(123456));
    value = 777;
    CHECK(parse_passkey(" \t123456\t ", 10, value));
    CHECK_EQ(value, std::uint32_t(123456));
    value = 777;
    CHECK(parse_passkey(" 000000 ", 8, value));
    CHECK_EQ(value, std::uint32_t(0));
    /* Whitespace-only trims to empty and is rejected without clobbering. */
    value = 777;
    CHECK(!parse_passkey("      ", 6, value));
    CHECK_EQ(value, std::uint32_t(777));
    /* Newlines are content, never trimming, and any interior blank fails. */
    value = 777;
    CHECK(!parse_passkey("12\n456", 6, value));
    CHECK_EQ(value, std::uint32_t(777));
    value = 777;
    CHECK(!parse_passkey("\n123456", 7, value));
    CHECK_EQ(value, std::uint32_t(777));
    value = 777;
    CHECK(!parse_passkey("123456\n", 7, value));
    CHECK_EQ(value, std::uint32_t(777));
    value = 777;
    CHECK(!parse_passkey("123 56", 6, value));
    CHECK_EQ(value, std::uint32_t(777));
    value = 777;
    CHECK(!parse_passkey("12:456", 6, value));
    CHECK_EQ(value, std::uint32_t(777));
    value = 777;
    CHECK(!parse_passkey("12345\x7F", 6, value));
    CHECK_EQ(value, std::uint32_t(777));
    /* Overlong trimmed input is rejected even when the prefix is numeric. */
    value = 777;
    CHECK(!parse_passkey(" 1234567 ", 9, value));
    CHECK_EQ(value, std::uint32_t(777));
}

void test_coverage_device_list_merge_remove_render_clamp()
{
    /* Lowercase input is normalized on add and found via the stored form. */
    device_list normalized;
    CHECK(normalized.add(make_device("aa:bb:cc:dd:ee:0a", "Lower", -50,
                                     device_kind::mouse)));
    CHECK_EQ(normalized.size(), std::size_t(1));
    const device *stored = normalized.find("AA:BB:CC:DD:EE:0A");
    CHECK(stored != nullptr);
    if (stored != nullptr) {
        CHECK_STR(stored->address, "AA:BB:CC:DD:EE:0A");
        CHECK_STR(stored->name, "Lower");
    }
    CHECK(normalized.find("aa:bb:cc:dd:ee:0a") == nullptr);
    CHECK(normalized.remove("AA:BB:CC:DD:EE:0A"));
    CHECK_EQ(normalized.size(), std::size_t(0));

    /* Empty + empty keeps an empty name; display and render use placeholder. */
    device_list unnamed;
    CHECK(unnamed.add(make_device("AA:BB:CC:DD:EE:10", "", -50,
                                  device_kind::unknown)));
    CHECK(unnamed.add(make_device("AA:BB:CC:DD:EE:10", "", -40,
                                  device_kind::unknown)));
    CHECK_EQ(unnamed.size(), std::size_t(1));
    const device *kept = unnamed.find("AA:BB:CC:DD:EE:10");
    CHECK(kept != nullptr);
    if (kept != nullptr) {
        CHECK_STR(kept->name, "");
        CHECK_EQ(kept->rssi, -40);
        CHECK_STR(cyberdeck_ble::display_name(*kept), k_unnamed_placeholder);
    }
    CHECK(unnamed.render().find("(unnamed)") != std::string::npos);

    /* Paired promotes monotonically and survives a later unpaired merge. */
    device_list paired;
    CHECK(paired.add(make_device("AA:BB:CC:DD:EE:11", "Tag", -50,
                                 device_kind::unknown)));
    CHECK(paired.add(make_device("AA:BB:CC:DD:EE:11", "Tag", -50,
                                 device_kind::mouse, true, true)));
    CHECK(paired.add(make_device("AA:BB:CC:DD:EE:11", "Other", -30,
                                 device_kind::keyboard, false, true)));
    const device *tag = paired.find("AA:BB:CC:DD:EE:11");
    CHECK(tag != nullptr);
    if (tag != nullptr) {
        CHECK(tag->paired);
        CHECK(tag->kind == device_kind::mouse);
        CHECK_STR(tag->name, "Tag");
        CHECK_EQ(tag->rssi, -30);
        CHECK(tag->connectable);
    }

    /* A weaker non-connectable duplicate still demotes without downgrading. */
    device_list demote;
    CHECK(demote.add(make_device("AA:BB:CC:DD:EE:12", "Beacon", -40,
                                 device_kind::keyboard, false, true)));
    CHECK(demote.add(make_device("AA:BB:CC:DD:EE:12", "Beacon", -90,
                                 device_kind::unknown, false, false)));
    const device *beacon = demote.find("AA:BB:CC:DD:EE:12");
    CHECK(beacon != nullptr);
    if (beacon != nullptr) {
        CHECK_EQ(beacon->rssi, -40);
        CHECK(beacon->kind == device_kind::keyboard);
        CHECK(!beacon->connectable);
    }

    /* A duplicate on a full list still merges instead of failing capacity. */
    device_list full;
    for (std::size_t i = 0; i < k_max_devices; ++i) {
        char address[18];
        std::snprintf(address, sizeof(address), "AA:BB:CC:DD:%02X:%02X",
                      static_cast<unsigned>(i >> 8), static_cast<unsigned>(i & 0xFF));
        CHECK(full.add(make_device(address, "Device", -60,
                                   device_kind::unknown)));
    }
    CHECK_EQ(full.size(), k_max_devices);
    CHECK(full.add(make_device("AA:BB:CC:DD:00:00", "Device", -30,
                               device_kind::keyboard)));
    CHECK_EQ(full.size(), k_max_devices);
    const device *promoted = full.find("AA:BB:CC:DD:00:00");
    CHECK(promoted != nullptr);
    if (promoted != nullptr) {
        CHECK_EQ(promoted->rssi, -30);
        CHECK(promoted->kind == device_kind::keyboard);
    }

    /* Removal clamps the selection and render keeps the marker deterministic. */
    device_list ordered;
    CHECK(ordered.add(make_device("AA:BB:CC:DD:EE:21", "A", -40, device_kind::keyboard)));
    CHECK(ordered.add(make_device("AA:BB:CC:DD:EE:22", "B", -41, device_kind::mouse)));
    CHECK(ordered.add(make_device("AA:BB:CC:DD:EE:23", "C", -42, device_kind::headset)));
    CHECK(ordered.remove("AA:BB:CC:DD:EE:21"));
    CHECK_EQ(ordered.size(), std::size_t(2));
    CHECK_EQ(ordered.selected_index(), std::size_t(0));
    CHECK_STR(ordered.selected()->name, "B");
    CHECK(ordered.render().find("> [1] B (Mouse, -41 dBm)") != std::string::npos);
    ordered.select(1);
    CHECK(ordered.remove("AA:BB:CC:DD:EE:22"));
    CHECK_EQ(ordered.selected_index(), std::size_t(0));
    CHECK(ordered.selected() != nullptr);
    if (ordered.selected() != nullptr) {
        CHECK_STR(ordered.selected()->name, "C");
    }
    /* Out-of-range removal leaves selection and content unchanged. */
    CHECK(!ordered.remove("AA:BB:CC:DD:EE:FF"));
    CHECK_EQ(ordered.size(), std::size_t(1));
    CHECK_EQ(ordered.selected_index(), std::size_t(0));
    ordered.clear();
    CHECK_STR(ordered.render(), "No Bluetooth devices found.\n");

    /* Single render is exact and RSSI is clamped in the projection. */
    device_list single;
    CHECK(single.add(make_device("AA:BB:CC:DD:EE:31", "Solo", -55,
                                 device_kind::keyboard)));
    CHECK_STR(single.render(),
              "Found 1 Bluetooth devices (UP/DOWN navigate, ENTER pair, ESC cancel):\n"
              "> [1] Solo (Keyboard, -55 dBm)\n");
    device_list extremes;
    CHECK(extremes.add(make_device("AA:BB:CC:DD:EE:32", "Weak", -127,
                                   device_kind::unknown)));
    CHECK(extremes.add(make_device("AA:BB:CC:DD:EE:33", "Hot", 127,
                                   device_kind::unknown)));
    CHECK(extremes.render().find("-120 dBm") != std::string::npos);
    CHECK(extremes.render().find("20 dBm") != std::string::npos);
}

/* REQ-COV-01 cycle 2: leading-invalid UTF-8 branches where content already
 * exists (else arm of !seen_non_space), saturated control at the name cap,
 * empty-trim passkeys, unreachable-yet selection guard, and the merge path
 * where address matches but addr_type differs (entry kept, capacity kept). */
void test_coverage_types_cycle2_sanitize_passkey_merge()
{
    using cyberdeck_ble::parse_passkey;
    using cyberdeck_ble::sanitize_name;

    /* Non-leading invalid bytes after content each become one blank: second
     * byte of a truncated 2-byte lead, bad continuation, overlong tail,
     * truncated 3-byte lead, bad 3-byte continuation, surrogate tail,
     * truncated 4-byte lead, bad 4-byte continuation, out-of-range tail,
     * and a bare continuation byte. The leading variants are already pinned
     * by the previous cycle; here seen_non_space is true so the else arm
     * (push ' ') executes instead of the skip. */
    {
        const char t2[] = {'A', static_cast<char>(0xC3)};
        CHECK_STR(sanitize_name(t2, sizeof(t2)), "A");
    }
    {
        const char bad2[] = {'A', static_cast<char>(0xC3), 'X'};
        CHECK_STR(sanitize_name(bad2, sizeof(bad2)), "A X");
    }
    {
        const char over[] = {'A', static_cast<char>(0xC0), static_cast<char>(0xAF)};
        CHECK_STR(sanitize_name(over, sizeof(over)), "A");
    }
    {
        const char t3[] = {'A', static_cast<char>(0xE2), static_cast<char>(0x96)};
        CHECK_STR(sanitize_name(t3, sizeof(t3)), "A");
    }
    {
        const char bad3[] = {'A', static_cast<char>(0xE2), 'X',
                             static_cast<char>(0xA0)};
        CHECK_STR(sanitize_name(bad3, sizeof(bad3)), "A X");
    }
    {
        const char sur[] = {'A', static_cast<char>(0xED), static_cast<char>(0xA0),
                            static_cast<char>(0x80)};
        CHECK_STR(sanitize_name(sur, sizeof(sur)), "A");
    }
    {
        const char t4[] = {'A', static_cast<char>(0xF0), static_cast<char>(0x9F),
                           static_cast<char>(0x94)};
        CHECK_STR(sanitize_name(t4, sizeof(t4)), "A");
    }
    {
        const char bad4[] = {'A', static_cast<char>(0xF0), 'X',
                             static_cast<char>(0x9F), static_cast<char>(0x94),
                             static_cast<char>(0xA5)};
        CHECK_STR(sanitize_name(bad4, sizeof(bad4)), "A X");
    }
    {
        const char oor[] = {'A', static_cast<char>(0xF4), static_cast<char>(0x90),
                            static_cast<char>(0x80), static_cast<char>(0x80)};
        CHECK_STR(sanitize_name(oor, sizeof(oor)), "A");
    }
    {
        const char stray[] = {'A', static_cast<char>(0x80)};
        CHECK_STR(sanitize_name(stray, sizeof(stray)), "A");
    }

    /* Saturated control at the cap: the (out.size() < cap) miss. A full
     * name followed by a control char keeps exactly the cap bytes. */
    {
        const std::string full = repeated('z', k_max_name_bytes);
        const std::string with_control = full + "\x01";
        CHECK_STR(sanitize_name(with_control.c_str(), with_control.size()), full);
    }

    /* Address loop tail: a late non-hex digit returns false without touching
     * the caller's buffer (33->35 miss at types.cpp:91). */
    expect_address_rejected("AA:BB:CC:DD:EE:0Z", 17);

    /* Passkey trim loops: empty-after-trim falls through both loops without
     * entering them (start==end on entry), then rejects on digit count. */
    {
        std::uint32_t value = 777;
        CHECK(!parse_passkey("", 0, value));
        CHECK_EQ(value, std::uint32_t(777));
        value = 777;
        CHECK(!parse_passkey("   ", 3, value));
        CHECK_EQ(value, std::uint32_t(777));
    }
    /* Modulus boundary via the public API: exactly 1000000 is rejected. */
    {
        std::uint32_t value = 777;
        CHECK(!parse_passkey("1000000", 7, value));
        CHECK_EQ(value, std::uint32_t(777));
    }

    /* Merge path where address matches but addr_type differs: no merge, a
     * second entry is stored (covers the 30->32 / 40->41 miss region). */
    {
        device_list list;
        CHECK(list.add(make_device("AA:BB:CC:DD:EE:50", "Pub", -50,
                                   device_kind::unknown, false, true,
                                   cyberdeck_ble::address_type::public_address)));
        CHECK(list.add(make_device("AA:BB:CC:DD:EE:50", "Rnd", -55,
                                   device_kind::keyboard, false, true,
                                   cyberdeck_ble::address_type::random_static)));
        CHECK_EQ(list.size(), std::size_t(2));
        const device *pub = list.find("AA:BB:CC:DD:EE:50",
                                      cyberdeck_ble::address_type::public_address);
        CHECK(pub != nullptr);
        if (pub != nullptr) CHECK_STR(pub->name, "Pub");
    }

    /* Degenerate selection guard: selection past the end with a non-empty
     * list returns nullptr (selected() 6->7 miss). Reachable by removing the
     * tail while the selection sat on it and then querying selected(). */
    {
        device_list list;
        CHECK(list.add(make_device("AA:BB:CC:DD:EE:60", "A", -40,
                                   device_kind::keyboard)));
        CHECK(list.add(make_device("AA:BB:CC:DD:EE:61", "B", -41,
                                   device_kind::mouse)));
        list.select(1);
        CHECK(list.remove("AA:BB:CC:DD:EE:61"));
        /* clamp_selection pulls 1 -> 0, so selected() is valid again. */
        CHECK(list.selected() != nullptr);
        CHECK_EQ(list.selected_index(), std::size_t(0));
        /* Empty list still yields nullptr without touching the guard. */
        list.clear();
        CHECK(list.selected() == nullptr);
    }
}

} // namespace

int main()
{
    test_approved_constants_and_labels();
    test_eighteen_items_keep_selection_and_marker_safe();
    test_address_type_is_part_of_device_identity();
    test_kind_from_appearance_only_proves_what_is_advertised();
    test_address_normalization_is_strict_and_normalizing();
    test_name_sanitizer_is_bounded_utf8_safe_and_control_free();
    test_display_name_and_rssi_clamping();
    test_passkey_helpers_are_strict_display_only_and_masked();
    test_list_dedup_is_by_address_and_keeps_the_strongest_signal();
    test_list_capacity_is_bounded_and_selection_clamps();
    test_list_removal_keeps_selection_inside_bounds();
    test_list_render_is_deterministic_and_shows_name_plus_type();
    test_duplicate_names_are_distinguished_only_by_address();
    test_non_connectable_devices_are_kept_but_marked();
    test_coverage_addresses_separators_and_case_forms();
    test_coverage_sanitize_utf8_boundaries_and_control_set();
    test_coverage_passkey_and_list_promotion_guards();
    test_coverage_address_punctuation_case_and_overlong();
    test_coverage_sanitize_leading_overlong_surrogate_trim_truncate();
    test_coverage_passkey_trims_malformed_max();
    test_coverage_device_list_merge_remove_render_clamp();
    test_coverage_types_cycle2_sanitize_passkey_merge();

    std::printf("ble types/list contract: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
