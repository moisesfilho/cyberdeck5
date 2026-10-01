#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esp_err.h"
#include "apps/bluetooth/ble_mgr.h"
#include "apps/bluetooth/cyberdeck_ble_state_machine.h"
#include "apps/bluetooth/cyberdeck_ble_types.h"
#include "apps/shell/cyberdeck_edit_line.h"
#include "apps/shell/cyberdeck_history.h"
#include "apps/shell/cyberdeck_local_shell.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/shell/cyberdeck_ssh_line_composer.h"
#include "apps/wifi/cyberdeck_wifi_menu.h"
#include "apps/wifi/cyberdeck_wifi_state_machine.h"
#include "apps/wifi/wifi_mgr.h"
#include "apps/wifi/wifi_storage.h"
#include "platform/sensors/battery_protection.h"

namespace cyberdeck_shell_session {

/* Wi-Fi UI flow state shared with the LVGL pump and the renderer.  Owned by
 * the UI composition; the session only reads and transitions it. */
enum class wifi_ui_state_t {
    IDLE,
    SCANNING,
    SEARCH_SELECT,
    SEARCH_PASSWORD,
    CONNECTING,
    SAVED_SELECT,
    SAVED_CONFIRM
};

/* Physical/virtual navigation and editing keys, translated from LVGL key
 * codes by the UI.  The session never includes LVGL headers. */
enum class key {
    unknown,
    up,
    down,
    left,
    right,
    home,
    end,
    next_tab,
    enter,
    backspace,
    del,
    esc,
};

/* UI-owned services and shared flow state used by the session controller.
 * The session owns the editor, history, current line and cursor; everything
 * else is borrowed from the UI composition for the duration of a call.
 *
 * Every side effect the session needs is declared here, so the session stays
 * free of platform and LVGL calls and can be linked on the host against a
 * fake host.  The UI composition implements them by delegating to the real
 * service. */
struct host {
    virtual ~host() = default;

    /* Bounded terminal output owned by the UI. */
    virtual void append_output_line(const std::string &line) = 0;
    virtual void write_output(const char *data, std::size_t length) = 0;
    /* Unbounded staging append without an intermediate repaint (menu renders
     * are committed by the trailing render call, as before). */
    virtual void append_output_text(const std::string &text) = 0;
    virtual void clear_output() = 0;
    virtual void render() = 0;

    /* Bluetooth model and pump owned by the UI. */
    virtual cyberdeck_ble::state_machine &ble_model() = 0;
    virtual cyberdeck_ble::device_list &ble_scan_devices() = 0;
    virtual void clear_ble_notice() = 0;
    virtual void mark_ble_transient_uncommitted() = 0;
    virtual void submit_ble_actions() = 0;
    virtual void sync_ble_transient() = 0;
    /* Bounded read-only snapshot of the persisted bonds, owned by the host. */
    virtual std::size_t copy_ble_bonds(ble_bond_snapshot_t *out, std::size_t capacity) = 0;

    /* Wi-Fi flow owned by the UI (pump, menus and renderer share it). */
    virtual wifi_ui_state_t &wifi_state() = 0;
    virtual cyberdeck_wifi::state_machine &wifi_model() = 0;
    virtual cyberdeck_wifi_search_menu &wifi_search_menu() = 0;
    virtual cyberdeck_wifi_saved_menu &wifi_saved_menu() = 0;
    virtual std::uint64_t &wifi_scan_generation() = 0;
    virtual bool wifi_begin_scan(std::uint64_t generation) = 0;
    virtual void wifi_cancel_scan() = 0;
    virtual std::string build_wifi_audit_save_path() = 0;
    /* Wi-Fi audit port.  The session never owns the controller, so the module
     * stays linkable on the host without its worker and ESP-IDF dependencies. */
    virtual void wifi_audit_begin() = 0;
    virtual bool wifi_audit_save(const std::string &path) = 0;

    /* Wi-Fi service port.  Tokens are owned by the session; the manager's
     * current token is read back through the host. */
    virtual esp_err_t wifi_connect(const char *ssid, const char *password) = 0;
    virtual esp_err_t wifi_cancel_connection() = 0;
    virtual void wifi_forget(const char *ssid) = 0;
    virtual bool wifi_enabled() const = 0;
    virtual std::uint64_t wifi_current_token() const = 0;
    virtual bool wifi_status(wifi_status_t *out) = 0;
    /* Saved-network storage.  Password buffers are owned by the caller and
     * must be wiped by it. */
    virtual bool wifi_storage_ready() = 0;
    virtual bool wifi_storage_load_all(wifi_saved_list_t *list) = 0;
    virtual bool wifi_storage_find(const char *ssid, char *out_password, std::size_t max_len) = 0;

    /* SSH service port.  The session never queries the global SSH client. */
    virtual cyberdeck_session_state ssh_phase() const = 0;
    virtual esp_err_t ssh_send_data(const char *data, std::size_t length) = 0;
    virtual esp_err_t ssh_send_password(const char *password) = 0;
    virtual void ssh_accept_host_key() = 0;

    /* SSH chrome owned by the UI. */
    virtual void set_ssh_visible(bool visible) = 0;
    virtual void reset_ssh_filter() = 0;
    virtual void discard_ssh_composer() = 0;
    virtual cyberdeck_ssh_line_composer &ssh_composer() = 0;
    virtual esp_err_t ssh_connect(const char *user, const char *host, int port) = 0;

    /* Screen protection port. */
    virtual void screen_turn_on() = 0;
    virtual void screen_turn_off() = 0;
    virtual esp_err_t screen_set_timeout_minutes(std::uint16_t minutes) = 0;

    /* Battery protection port. */
    virtual bool battery_protection_set_enabled(bool enabled) = 0;
    virtual bool battery_protection_started() const = 0;
    virtual bool battery_protection_snapshot(cyberdeck_battery_protection::snapshot *out) const = 0;

    /* Event log port.  Bounded, oldest first. */
    virtual void log_event(char level, const char *tag, const char *message) = 0;
    virtual std::string recent_events(std::size_t count) = 0;

    /* Async cat worker owned by the UI lifecycle. */
    virtual bool cat_enqueue(const char *cwd, const char *line) = 0;

    /* Local shell owned by the UI composition. */
    virtual cyberdeck_local_shell &local_shell() = 0;

    /* Compiled-in application runtime owned by the UI composition. */
    virtual cyberdeck_apps::runtime &app_runtime() = 0;
};

/* Stateful local/remote line dispatcher.  Owns the editor, the history, the
 * current line, the cursor, the pending Wi-Fi password target, the BLE passkey
 * buffer and the Wi-Fi connection tokens; every feature side effect goes
 * through host.  All methods run on the LVGL task. */
class session {
public:
    explicit session(host &host);
    session(const session &) = delete;
    session &operator=(const session &) = delete;

    void execute_line(bool line_already_sent = false);
    void handle_key(key pressed);
    void sync_editor();
    void sync_line();
    void clear_editor();

    /* Physical text injection.  Returns true when the editor accepted the
     * text; the UI must not repaint on false. */
    bool insert_physical_text(const char *text, std::size_t length);
    /* A modified physical key is an SSH escape sequence rather than text.
     * Returns true when the sequence was accepted by the session. */
    bool insert_modified_key(char character, std::uint8_t modifier);
    /* Virtual-keyboard insertion.  Editing keys are actions, not text, and
     * a chunk may carry several newlines; each line executes exactly once.
     * *virtual_enter_handled reports that the chunk ended right after a
     * newline, so the UI can suppress the textarea's duplicate Enter. */
    bool insert_virtual_text(const char *text, bool *virtual_enter_handled);

    const std::string &line() const { return line_; }
    std::size_t cursor() const { return cursor_; }
    cyberdeck_edit_line &editor() { return editor_; }
    const cyberdeck_edit_line &editor() const { return editor_; }

    /* Transient BLE passkey buffer, owned by the session.  Wiped on every
     * lifecycle path that ends passkey ownership. */
    const std::string &ble_auth_input() const { return ble_auth_input_; }
    void clear_ble_auth_input();
    void append_ble_auth_digit(char digit);

    /* Wi-Fi connection tokens owned by the session and published to the UI
     * pump, which matches manager callbacks against them. */
    std::uint64_t wifi_connection_token() const { return wifi_connection_token_; }
    std::uint64_t wifi_model_connection_token() const { return wifi_model_connection_token_; }
    void invalidate_wifi_connection();

private:
    void move_history(int direction);
    bool begin_wifi_connection(const char *ssid, const char *password);

    host &host_;
    cyberdeck_edit_line editor_;
    cyberdeck_history history_;
    std::string line_;
    std::size_t cursor_ = 0;
    std::string selected_ap_ssid_;
    std::string ble_auth_input_;
    std::uint64_t wifi_connection_token_ = 0;
    std::uint64_t wifi_model_connection_token_ = 0;
    std::size_t log_lines_;
};

} // namespace cyberdeck_shell_session
