/*
 * Host-only contract for the pure BLE device types, sanitizer and list model.
 *
 * This header is intentionally test-owned.  The production implementation must
 * expose the same ABI from
 * components/cyberdeck/include/features/bluetooth/cyberdeck_ble_types.h
 * and link from cyberdeck_ble_types.cpp without ESP-IDF, NimBLE, Bluedroid,
 * esp_hosted, FreeRTOS, LVGL, NVS, or hardware.
 *
 * Scope guard (REQ-BLE-011): nothing in this contract may grow a key, link key,
 * IRK, LTK, or passkey *storage* field.  A passkey is transient UI input and is
 * only ever formatted for display; see format_passkey/mask_passkey.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if __has_include("features/bluetooth/cyberdeck_ble_types.h")
#include "features/bluetooth/cyberdeck_ble_types.h"
#else
namespace cyberdeck_ble {

/* Bounded sizes.  A terminal row and a bond record must never be able to grow
 * without limit, so every string produced here is clamped before it leaves. */
inline constexpr std::size_t k_max_name_bytes = 32;      /* sanitized, no NUL   */
inline constexpr std::size_t k_max_address_bytes = 17;   /* "AA:BB:CC:DD:EE:FF" */
inline constexpr std::size_t k_max_devices = 32;         /* list capacity       */
inline constexpr int k_min_rssi = -120;
inline constexpr int k_max_rssi = 20;

/* Interactive numeric-comparison pairing uses exactly six decimal digits. */
inline constexpr std::uint32_t k_passkey_digits = 6;
inline constexpr std::uint32_t k_passkey_modulus = 1000000U;

/* Placeholder used when a peripheral advertises no usable name. */
inline constexpr const char *k_unnamed_placeholder = "(unnamed)";

/* Bluetooth SIG appearance values consumed by kind_from_appearance.  These are
 * assigned numbers from the Core Specification Supplement, not SDK APIs. */
inline constexpr std::uint16_t k_appearance_generic_hid = 0x03C0;
inline constexpr std::uint16_t k_appearance_keyboard = 0x03C1;
inline constexpr std::uint16_t k_appearance_mouse = 0x03C2;
inline constexpr std::uint16_t k_appearance_wearable_headset = 0x0401;
inline constexpr std::uint16_t k_appearance_headset_mic = 0x0408;
inline constexpr std::uint16_t k_appearance_headphones = 0x0418;
inline constexpr std::uint16_t k_appearance_ear_headset = 0x0419;
inline constexpr std::uint16_t k_appearance_handsfree = 0x041A;
inline constexpr std::uint16_t k_appearance_handsfree_mic = 0x041B;

/*
 * The four approved list types.  "unknown" is a first-class, always available
 * fallback: the classifier must never guess a type it cannot prove from the
 * advertised appearance.
 */
enum class device_kind { keyboard, headset, mouse, unknown };

/*
 * A discovered/peripheral record.  It deliberately carries no key material so
 * that rendering, snapshots, logs and the bond store cannot leak a secret even
 * by accident.
 */
struct device {
    std::string address;                 /* normalized uppercase MAC         */
    std::string name;                    /* sanitized and bounded; may be "" */
    int rssi = k_min_rssi;
    device_kind kind = device_kind::unknown;
    bool paired = false;
    bool connectable = true;
};

/* Deterministic classification; unknown for every unproven value. */
device_kind kind_from_appearance(std::uint16_t appearance);

/* Stable, bounded, terminal-safe label for each approved type. */
const char *kind_label(device_kind kind);

/*
 * Accepts "AA:BB:CC:DD:EE:FF", "aa-bb-cc-dd-ee-ff" and the bare 12-hex-digit
 * form.  Output is always uppercase and colon separated.  On any failure `out`
 * is left unchanged.
 */
bool normalize_address(const char *raw, std::size_t len, std::string &out);

/*
 * Display-safe bounded name.  Rules, in order:
 *   1. raw == nullptr or len == 0        -> k_unnamed_placeholder
 *   2. C0 (U+0000..U+001F), U+007F, C1 (U+0080..U+009F), U+2028 and U+2029
 *      become a single ASCII space, so a name can never inject a line break or
 *      a control sequence into the TUI;
 *   3. an invalid UTF-8 byte also becomes a single ASCII space;
 *   4. leading/trailing ASCII spaces are removed;
 *   5. the result is clamped to k_max_name_bytes and a truncated trailing
 *      partial UTF-8 sequence is dropped (never a split code point);
 *   6. an empty result becomes k_unnamed_placeholder.
 */
std::string sanitize_name(const char *raw, std::size_t len);

/* The name to show for a record; never empty. */
std::string display_name(const device &item);

/* RSSI is clamped into [k_min_rssi, k_max_rssi]. */
int clamp_rssi(int raw);

/*
 * Strict passkey parser: after trimming spaces/tabs, exactly k_passkey_digits
 * ASCII decimal digits.  Leading zeros are accepted.  On failure `out` is left
 * unchanged.
 */
bool parse_passkey(const char *text, std::size_t len, std::uint32_t &out);

/* "000123" for 0..999999; empty string for any out-of-range value. */
std::string format_passkey(std::uint32_t passkey);

/* Always "******".  This is the only projection allowed into logs. */
std::string mask_passkey(std::uint32_t passkey);

/*
 * Bounded, ordered, de-duplicated device list with clamped selection.
 * De-duplication is by address, never by name: two peripherals may legitimately
 * advertise the same name, and the address is the only stable identity.
 */
class device_list {
public:
    void clear();

    /*
     * Upsert, keyed by address only.  On a duplicate address:
     *   rssi        -> max(existing, incoming)   (strongest signal wins)
     *   name        -> first non-empty wins; an empty name never overwrites
     *                   a usable one and a second different name never
     *                   replaces the first (a peripheral may rotate its
     *                   local name)
     *   kind        -> an existing proven kind is never downgraded to unknown
     *   paired      -> monotone promotion (true wins)
     *   connectable -> monotone demotion (false wins), so a single
     *                   non-connectable advertisement is enough to refuse
     *                   pairing
     * Returns false when the address is unusable or the bounded capacity is
     * exhausted; the list is left unchanged in that case.
     */
    bool add(const device &item);

    std::size_t size() const;
    std::size_t capacity() const;
    const device *at(std::size_t index) const;
    const device *find(const std::string &address) const;
    bool remove(const std::string &address);

    std::size_t selected_index() const;
    void select(std::size_t index); /* ignored when out of range */
    void move_up();                /* clamps at 0            */
    void move_down();              /* clamps at size() - 1   */
    const device *selected() const;

    std::vector<device> snapshot() const;

    /* Deterministic listing: "Name (Kind, -55 dBm)" with a "> " marker. */
    std::string render() const;

    static std::size_t max_devices();
};

} // namespace cyberdeck_ble
#endif
