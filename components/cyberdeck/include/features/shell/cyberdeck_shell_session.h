#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esp_err.h"
#include "features/bluetooth/cyberdeck_ble_state_machine.h"
#include "features/bluetooth/cyberdeck_ble_types.h"
#include "features/shell/cyberdeck_edit_line.h"
#include "features/shell/cyberdeck_history.h"
#include "features/shell/cyberdeck_local_shell.h"
#include "features/shell/cyberdeck_ssh_line_composer.h"
#include "features/wifi/cyberdeck_wifi_audit.h"
#include "features/wifi/cyberdeck_wifi_menu.h"
#include "features/wifi/cyberdeck_wifi_state_machine.h"

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
 * else is borrowed from the UI composition for the duration of a call. */
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
    virtual std::string &ble_auth_input() = 0;
    virtual void clear_ble_notice() = 0;
    virtual void mark_ble_transient_uncommitted() = 0;
    virtual void submit_ble_actions() = 0;
    virtual void sync_ble_transient() = 0;

    /* Wi-Fi flow owned by the UI (pump, menus and renderer share it). */
    virtual wifi_ui_state_t &wifi_state() = 0;
    virtual cyberdeck_wifi::state_machine &wifi_model() = 0;
    virtual cyberdeck_wifi_search_menu &wifi_search_menu() = 0;
    virtual cyberdeck_wifi_saved_menu &wifi_saved_menu() = 0;
    virtual std::uint64_t &wifi_scan_generation() = 0;
    virtual std::uint64_t &wifi_connection_token() = 0;
    virtual std::uint64_t &wifi_model_connection_token() = 0;
    virtual bool wifi_begin_scan(std::uint64_t generation) = 0;
    virtual void wifi_cancel_scan() = 0;
    virtual std::string build_wifi_audit_save_path() = 0;
    virtual cyberdeck_wifi_audit::audit_controller &wifi_audit() = 0;

    /* SSH chrome owned by the UI. */
    virtual void set_ssh_visible(bool visible) = 0;
    virtual void reset_ssh_filter() = 0;
    virtual void discard_ssh_composer() = 0;
    virtual cyberdeck_ssh_line_composer &ssh_composer() = 0;
    virtual esp_err_t ssh_connect(const char *user, const char *host, int port) = 0;

    /* Async cat worker owned by the UI lifecycle. */
    virtual bool cat_enqueue(const char *cwd, const char *line) = 0;

    /* Local shell owned by the UI composition. */
    virtual cyberdeck_local_shell &local_shell() = 0;
};

/* Stateful local/remote line dispatcher.  Owns the editor, the history, the
 * current line, the cursor and the pending Wi-Fi password target; every
 * feature side effect goes through host or through standalone feature
 * services.  All methods run on the LVGL task. */
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

    const std::string &line() const { return line_; }
    std::size_t cursor() const { return cursor_; }
    cyberdeck_edit_line &editor() { return editor_; }
    const cyberdeck_edit_line &editor() const { return editor_; }

private:
    void move_history(int direction);
    bool begin_wifi_connection(const char *ssid, const char *password);

    host &host_;
    cyberdeck_edit_line editor_;
    cyberdeck_history history_;
    std::string line_;
    std::size_t cursor_ = 0;
    std::string selected_ap_ssid_;
};

} // namespace cyberdeck_shell_session
