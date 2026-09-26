/*
 * Host-only contract for the pure BLE search/pair state machine.
 *
 * Test-owned.  Production must expose the same ABI from
 * components/cyberdeck/include/features/bluetooth/cyberdeck_ble_state_machine.h
 * and link from cyberdeck_ble_state_machine.cpp without ESP-IDF, NimBLE,
 * esp_hosted, FreeRTOS, LVGL, NVS, timers or hardware.
 *
 * The machine is the UI-facing model: it owns screens, bounded deadlines,
 * monotonic tokens and a derived action list.  It never performs I/O and never
 * calls a BLE stack API, so nothing here can block the LVGL task.  Every
 * asynchronous completion is matched against the token the machine handed out,
 * so a late callback from a superseded generation can never mutate the visible
 * state.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if __has_include("features/bluetooth/cyberdeck_ble_state_machine.h")
#include "features/bluetooth/cyberdeck_ble_state_machine.h"
#else
#include "cyberdeck_ble_types.h"

namespace cyberdeck_ble {

/* Bounded deadlines, all in milliseconds. */
inline constexpr std::uint32_t k_scan_timeout_ms = 10000;
inline constexpr std::uint32_t k_pair_timeout_ms = 30000;
inline constexpr std::uint32_t k_auth_timeout_ms = 30000;
inline constexpr std::uint32_t k_connect_timeout_ms = 20000;

/* Automatic reconnection gives up after this many consecutive failures. */
inline constexpr std::uint32_t k_max_reconnect_attempts = 3;

enum class screen {
    idle,
    searching,
    results,
    paired,
    pairing,
    auth,
    connecting,
    connected,
};

/* The four approved interactive keys.  No other input reaches this model. */
enum class key { up, down, enter, escape };

/* The four approved terminal message classes (REQ-BLE-009) plus the "nothing
 * to say" sentinel.  Each non-none value maps to exactly one notice text for
 * its current screen; notice_text() is the only message the UI logs. */
enum class notice { none, empty, failed, timed_out, cancelled };

/* How the peer asked the user to authenticate interactively. */
enum class auth_request_kind { passkey, numeric_compare, confirm };

/* Terminal result of a pairing attempt. */
enum class pair_outcome { bonded, rejected, cancelled, timed_out, failed };

enum class action_kind {
    start_scan,
    cancel_scan,
    pair,
    submit_auth,
    cancel_pair,
    cancel_connect,
    connect,
    disconnect,
    reconnect,
};

/*
 * The single source of truth for the user-visible strings.  They match the
 * English wording used by the rest of the firmware terminal, and none of them
 * can carry a secret: notice_text() is a loggable surface, so a passkey may
 * only ever appear in status_line() on the auth screen, where the user needs
 * to read it.
 */
inline constexpr const char *k_msg_scan_empty = "No Bluetooth devices found.";
inline constexpr const char *k_msg_scan_failed = "Bluetooth scan failed.";
inline constexpr const char *k_msg_scan_timeout = "Bluetooth search timed out.";
inline constexpr const char *k_msg_search_cancelled = "Bluetooth search cancelled.";
inline constexpr const char *k_msg_pair_rejected = "Bluetooth pairing rejected.";
inline constexpr const char *k_msg_pair_failed = "Bluetooth pairing failed.";
inline constexpr const char *k_msg_pair_timeout = "Bluetooth pairing timed out.";
inline constexpr const char *k_msg_pair_cancelled = "Bluetooth pairing cancelled.";
inline constexpr const char *k_msg_connect_failed = "Bluetooth connection failed.";
inline constexpr const char *k_msg_connect_timeout = "Bluetooth connection timed out.";
inline constexpr const char *k_msg_connect_cancelled = "Bluetooth connection cancelled.";
inline constexpr const char *k_msg_reconnect_gave_up = "Bluetooth reconnection failed.";
inline constexpr const char *k_status_scanning = "Scanning Bluetooth devices...";
inline constexpr const char *k_status_pairing_prefix = "Pairing with ";
inline constexpr const char *k_status_enter_passkey = "Enter the passkey shown on your device: ";
inline constexpr const char *k_status_confirm =
    "Confirm the pairing request on your device (ENTER to accept, ESC to cancel).";
inline constexpr const char *k_status_numeric_compare =
    "Compare the number shown on your device and confirm (ENTER to accept, ESC to cancel).";
inline constexpr const char *k_status_connecting_prefix = "Connecting to ";
inline constexpr const char *k_status_connected_prefix = "Connected to ";
inline constexpr const char *k_status_connected_suffix = ".";

/*
 * A derived, side-effect free instruction for the adapter.  `passkey` is
 * non-zero only for submit_auth of a passkey challenge, which is the single
 * legitimate crossing of the value across this boundary.
 */
struct action {
    action_kind kind = action_kind::start_scan;
    std::string address;
    std::uint32_t passkey = 0;
    std::uint64_t token = 0;
};

class state_machine {
public:
    state_machine();
    ~state_machine();
    state_machine(const state_machine &) = delete;
    state_machine &operator=(const state_machine &) = delete;

    /* ---- commands derived from the shell --------------------------------- */
    void begin_search();
    void begin_paired();
    void set_paired_devices(const std::vector<device> &paired);

    /*
     * ---- asynchronous completions, all token matched ----------------------
     * A completion is applied only when the token equals the active token of
     * its generation AND the machine is still on the screen that generation
     * opened.  Anything else is dropped, leaving the visible state untouched.
     *
     * Exact transitions:
     *   searching --scan_finished--> results   (empty -> notice::empty)
     *   searching --scan_failed----> results   (notice::failed)
     *   searching --scan_timed_out-> results   (notice::timed_out)
     *   searching --escape---------> idle      (notice::cancelled, cancel_scan)
     *   searching --deadline-------> results   (notice::timed_out, cancel_scan)
     *   results ---press(enter)----> pairing   (action pair, if selectable)
     *   results ---escape----------> idle      (notice::none, no action)
     *   pairing ---auth_requested--> auth
     *   auth ------submit_auth-----> pairing   (bond is now awaited)
     *   pairing ---bonded----------> connecting (action connect)
     *   pairing ---other outcome---> results   (notice per outcome)
     *   pairing/auth/connecting --escape------> results (action cancel_*)
     *   connecting --connected-----> connected
     *   connecting --failed--------> results   (notice::failed)
     *   connected --escape---------> idle      (action disconnect)
     * A new scan_finished resets the selection to 0 and the visible list, so a
     * device that stopped advertising can never be paired by a stale index.
     */
    void scan_finished(std::uint64_t token, const std::vector<device> &devices);
    void scan_failed(std::uint64_t token);
    void scan_timed_out(std::uint64_t token);

    /* Applied only while on `results`/`paired` with the token the machine
     * itself emitted in the `pair` action. */
    void pairing_started(std::uint64_t token, const std::string &address);

    /* Applied only on the `pairing` screen of the matching pair generation.
     * A passkey challenge outside 0..k_passkey_modulus is refused (the bond
     * deadline keeps running) and no fabricated value is ever displayed. */
    void auth_requested(std::uint64_t token, auth_request_kind kind,
                        std::uint32_t passkey);
    void pairing_finished(std::uint64_t token, pair_outcome outcome);

    void connection_finished(std::uint64_t token, bool connected);

    /*
     * Automatic reconnection for a bonded device.  Emits a `reconnect` action
     * while the consecutive-failure budget is below
     * k_max_reconnect_attempts.  At the cap it emits nothing, moves to
     * `paired` and reports k_msg_reconnect_gave_up, so the feature can never
     * spin.  A manual `connect` (Enter on `results`/`paired`) re-arms the
     * budget; a manual connect success resets the consecutive-failure count.
     */
    void schedule_reconnect(const device &item);

    /* Bounded time source; only the deadlines above are affected. */
    void advance_time(std::uint32_t elapsed_ms);

    /* ---- input ----------------------------------------------------------- */
    void press(key pressed);

    /*
     * Accepts the interactive authentication the peer asked for.  For a
     * passkey challenge only the exact displayed value is forwarded; a wrong
     * value forwards nothing and keeps the auth screen.  For a confirm or
     * numeric comparison challenge the user accepted, so 0 is forwarded.
     *
     * On acceptance the machine returns to `pairing` (the bond is awaited),
     * clears the displayed passkey so it can no longer leak, and re-arms the
     * pair deadline.  press(key::enter) is exactly submit_auth with the
     * displayed value.
     */
    void submit_auth(std::uint32_t passkey);

    /* ---- observation ----------------------------------------------------- */
    screen current_screen() const;
    notice current_notice() const;

    /* Exact single-line user message for the current notice, "" for none. */
    std::string notice_text() const;

    /* Transient progress line for the current screen, "" when there is none. */
    std::string status_line() const;

    std::size_t selected_index() const;
    const device *selected() const;  /* nullptr when the selection vanished */
    device_list devices() const;

    /* Displayed passkey for the auth screen, 0 when none is displayed. */
    std::uint32_t displayed_passkey() const;
    auth_request_kind pending_auth_kind() const;

    /* Address of the bonded device the machine is currently working on, or
     * "" when idle. */
    std::string active_address() const;

    std::uint64_t active_scan_token() const;
    std::uint64_t active_pair_token() const;
    std::uint64_t active_connection_token() const;

    /* Drains and clears the derived action queue. */
    std::vector<action> take_actions();
};

} // namespace cyberdeck_ble
#endif
