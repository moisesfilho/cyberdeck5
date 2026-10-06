#include "platform/display/cyberdeck_ui.h"
#include "apps/shell/cyberdeck_history.h"
#include "apps/shell/cyberdeck_shell_utils.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "apps/system/cyberdeck_service_ports.h"
#include "platform/input/tab5_keyboard.h"
#include "platform/input/cyberdeck_keyboard_dispatch.h"
#include "apps/shell/cyberdeck_shell_session.h"
#include "apps/shell/cyberdeck_shell_app.h"
#include "platform/display/cyberdeck_wifi_indicator.h"
#include "platform/display/cyberdeck_wifi_icon.h"
#include "platform/display/cyberdeck_clock.h"
#include "platform/display/cyberdeck_battery_view.h"
#include "platform/display/cyberdeck_header_view.h"
#include "platform/display/cyberdeck_terminal_view.h"
#include "platform/display/cyberdeck_terminal_scrollback.h"
#include "platform/sensors/battery_protection.h"
#include "apps/shell/cyberdeck_terminal_filter.h"
#include "apps/shell/cyberdeck_ssh_line_composer.h"
#include "apps/wifi/cyberdeck_wifi_menu.h"
#include "apps/wifi/cyberdeck_wifi_state_machine.h"
#include "apps/wifi/cyberdeck_wifi_audit.h"
#include "apps/bluetooth/cyberdeck_ble_background.h"
#include "apps/bluetooth/cyberdeck_ble_types.h"
#include "apps/bluetooth/cyberdeck_ble_state_machine.h"
#include "apps/shell/cyberdeck_edit_line.h"
#include "apps/shell/cyberdeck_local_shell.h"
#include "apps/shell/cyberdeck_cat_worker.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "platform/display/cyberdeck_screen_protection.h"
#include "platform/display/screen_off.h"
#include "platform/display/cyberdeck_window_manager_adapter.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <ctime>
#include <cstdint>
#include <vector>
#include <new>
#include <atomic>
#include <limits>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

extern const lv_font_t cyberdeck_font;

namespace {
/* Wi-Fi flow state lives in cyberdeck_shell_session::wifi_ui_state_t so the
 * session controller, the LVGL pump and the renderer share one definition. */

/* The scrollback budget must stay identical to the shell console budget: the
 * textarea renders scrollback plus prompt and line in one buffer, so any drift
 * here would overflow the terminal bound the console enforces. */
constexpr size_t TERMINAL_LIMIT = cyberdeck_shell_console::k_terminal_limit;
constexpr size_t viewport_bytes = 4096;
static_assert(TERMINAL_LIMIT == cyberdeck_edit_line::limit,
              "the scrollback budget must match the console line budget");
constexpr UBaseType_t CAT_WORK_QUEUE_CAPACITY = 8;
static_assert(CAT_WORK_QUEUE_CAPACITY == 8, "cat handoff capacity is bounded");
const lv_color_t BLACK = lv_color_hex(0x000000);
const lv_color_t SURFACE = lv_color_hex(0x0A0A0A);
const lv_color_t BORDER = lv_color_hex(0x2A2A2A);
const lv_color_t WHITE = lv_color_hex(0xF2F2F2);
const lv_color_t MUTED = lv_color_hex(0x8A8A8A);
lv_obj_t *s_screen = nullptr;
lv_obj_t *s_menu = nullptr;
lv_obj_t *s_terminal = nullptr;
lv_obj_t *s_keyboard = nullptr;
lv_timer_t *s_clock_timer = nullptr;
lv_timer_t *s_wifi_state_timer = nullptr;
lv_timer_t *s_wifi_scan_timer = nullptr;
lv_timer_t *s_ble_timer = nullptr;
lv_timer_t *s_ssh_timer = nullptr;
lv_timer_t *s_wifi_audit_timer = nullptr;
lv_timer_t *s_battery_timer = nullptr;
lv_timer_t *s_terminal_output_timer = nullptr;
cyberdeck_terminal_scrollback::model s_scrollback;

cyberdeck_terminal_filter s_ssh_output_filter;
cyberdeck_ssh_line_composer s_ssh_line_composer;
bool s_rendering = false;
bool s_terminal_output_dirty = false;
bool s_virtual_enter_handled = false;
std::string s_last_clock_text;
cyberdeck_local_shell s_local_shell("/sdcard", "/");
cyberdeck_header_view::view s_header_view;
cyberdeck_terminal_view::view s_terminal_view;
cyberdeck_window_manager::view_context s_shell_view_context;
bool s_cat_worker_ready = false;
bool s_ui_ready = false;
cyberdeck_shell_session::wifi_ui_state_t s_wifi_ui_state = cyberdeck_shell_session::wifi_ui_state_t::IDLE;
cyberdeck_wifi_search_menu s_wifi_search_menu;
cyberdeck_wifi_saved_menu s_wifi_saved_menu;
cyberdeck_wifi::state_machine s_wifi_model;
 cyberdeck_wifi_audit::audit_controller s_wifi_audit;
  std::uint64_t s_wifi_audit_reported_token = 0;
  cyberdeck_wifi_audit::state s_wifi_audit_reported_state = cyberdeck_wifi_audit::state::unavailable;
struct wifi_state_update {
    wifi_status_t status;
    bool enabled;
};
wifi_status_t s_latest_wifi_status{};
QueueHandle_t s_wifi_state_queue = nullptr;
struct wifi_scan_result {
    std::uint64_t generation;
    int count;
    wifi_ap_record_t aps[WIFI_SCAN_MAX_APS];
};
QueueHandle_t s_wifi_scan_queue = nullptr;
SemaphoreHandle_t s_wifi_scan_context_mutex = nullptr;
cyberdeck_keyboard_dispatch::dispatcher s_keyboard_dispatch;
QueueHandle_t s_ble_event_queue = nullptr;
ble_mgr_observer_handle_t s_ble_observer = nullptr;
constexpr std::size_t k_ssh_event_queue_capacity = 8;
/* State messages are short and fully described by the enum plus a small tag.
 * The data slot matches the largest chunk delivered by ssh_client: its 1024
 * byte receive buffer leaves at most 1023 bytes for one callback. The queue
 * element is reclaimed into s_ssh_event_slot, so the LVGL timer does not copy
 * it by value onto its stack. */
constexpr std::size_t k_ssh_event_state_message_limit = 64;
constexpr std::size_t k_ssh_event_data_limit = 1023;
constexpr uint32_t k_ssh_disconnect_timeout_ms = 1000;
enum class ssh_ui_event_kind : uint8_t { data, state };
struct ssh_ui_event {
    ssh_client_generation_t generation = 0;
    uint32_t discard_epoch = 0;
    ssh_ui_event_kind kind = ssh_ui_event_kind::data;
    ssh_client_state_t state = SSH_CLIENT_DISCONNECTED;
    char message[k_ssh_event_state_message_limit]{};
    /* The producer rejects chunks above the transport contract before copying. */
    char data[k_ssh_event_data_limit]{};
    uint16_t length = 0;
};
/* One reusable reclaim buffer keeps the LVGL timer from copying a large element
 * on the stack.  It is only touched from the LVGL task. */
ssh_ui_event s_ssh_event_slot;
ssh_ui_event s_ssh_data_event;
ssh_ui_event s_ssh_state_event;
ssh_ui_event s_ssh_discarded_event;
QueueHandle_t s_ssh_event_queue = nullptr;
ssh_client_generation_t s_ssh_expected_generation = 0;
std::atomic<uint32_t> s_ssh_data_queue_drop_count{0};
std::atomic<uint32_t> s_ssh_state_queue_drop_count{0};
std::atomic<uint32_t> s_ssh_discard_epoch{0};
uint32_t s_ssh_applied_discard_epoch = 0;
cyberdeck_ble::state_machine s_ble_model;
cyberdeck_ble::device_list s_ble_scan_devices;
std::string s_ble_last_notice;
bool s_ble_transient_active = false;
bool s_ble_transient_committed = false;
/* Background discovery observer: restores NVS bonds once at boot, then
 * reconnects only known bonds after spontaneous loss.  Scheduling state
 * lives in cyberdeck_ble_background::scheduler; the UI only routes events
 * and renders. */
cyberdeck_ble_background::scheduler s_ble_background;

/* Shell session controller: owns the editor, history, current line, cursor
 * and pending Wi-Fi password target.  The host below borrows UI-owned output,
 * models, menus and async handles; every method runs on the LVGL task. */
struct shell_session_host final : public cyberdeck_shell_session::host {
    void append_output_line(const std::string &line) override;
    void write_output(const char *data, std::size_t length) override;
    void append_output_text(const std::string &text) override;
    void clear_output() override;
    void render() override;
    cyberdeck_ble::state_machine &ble_model() override;
    cyberdeck_ble::device_list &ble_scan_devices() override;
    void clear_ble_notice() override;
    void mark_ble_transient_uncommitted() override;
    void submit_ble_actions() override;
    void sync_ble_transient() override;
    std::size_t copy_ble_bonds(ble_bond_snapshot_t *out, std::size_t capacity) override;
    cyberdeck_shell_session::wifi_ui_state_t &wifi_state() override;
    cyberdeck_wifi::state_machine &wifi_model() override;
    cyberdeck_wifi_search_menu &wifi_search_menu() override;
    cyberdeck_wifi_saved_menu &wifi_saved_menu() override;
    std::uint64_t &wifi_scan_generation() override;
    bool wifi_begin_scan(std::uint64_t generation) override;
    void wifi_cancel_scan() override;
    std::string build_wifi_audit_save_path() override;
    void wifi_audit_begin() override;
    bool wifi_audit_save(const std::string &path) override;
    esp_err_t wifi_connect(const char *ssid, const char *password) override;
    esp_err_t wifi_cancel_connection() override;
    void wifi_forget(const char *ssid) override;
    bool wifi_enabled() const override;
    std::uint64_t wifi_current_token() const override;
    bool wifi_status(wifi_status_t *out) override;
    bool wifi_storage_ready() override;
    bool wifi_storage_load_all(wifi_saved_list_t *list) override;
    bool wifi_storage_find(const char *ssid, char *out_password, std::size_t max_len) override;
    cyberdeck_session_state ssh_phase() const override;
    esp_err_t ssh_send_data(const char *data, std::size_t length) override;
    esp_err_t ssh_send_password(const char *password) override;
    void ssh_accept_host_key() override;
    void set_ssh_visible(bool visible) override;
    void reset_ssh_filter() override;
    void discard_ssh_composer() override;
    cyberdeck_ssh_line_composer &ssh_composer() override;
    esp_err_t ssh_connect(const char *user, const char *host, int port) override;
    void screen_turn_on() override;
    void screen_turn_off() override;
    esp_err_t screen_set_timeout_minutes(std::uint16_t minutes) override;
    bool battery_protection_set_enabled(bool enabled) override;
    bool battery_protection_started() const override;
    bool battery_protection_snapshot(cyberdeck_battery_protection::snapshot *out) const override;
    void log_event(char level, const char *tag, const char *message) override;
    std::string recent_events(std::size_t count) override;
    bool cat_enqueue(const char *cwd, const char *line) override;
    cyberdeck_local_shell &local_shell() override;
    cyberdeck_apps::runtime &app_runtime() override;
};

shell_session_host s_shell_session_host{};
/* The shell application owns the console (history, editing, prompt and the SSH
 * session mode).  The UI is only the view: it lends its session host and
 * applies whatever the application composes. */
cyberdeck_shell_app::application &s_shell_app = cyberdeck_shell_app::global_application();

void append_line(const std::string &line);
void append_output(const char *data, size_t len, bool repaint = true);
void render_terminal();
void terminal_geometry_changed(lv_event_t *) { render_terminal(); }
void process_terminal_output(lv_timer_t *timer);
void zero_string(std::string &s);
void refresh_ble_status();

bool ble_list_is_visible()
{
    const cyberdeck_ble::screen current = s_ble_model.current_screen();
    return s_ble_model.owns_input() &&
           (current == cyberdeck_ble::screen::results ||
            current == cyberdeck_ble::screen::paired);
}

void sync_ble_transient_block()
{
    if (ble_list_is_visible()) {
        s_ble_transient_active = true;
        return;
    }
    if (!s_ble_transient_active) return;

    /* Keep the interactive list out of terminal history while it owns the
     * navigation keys. Commit its last model-backed rendering once ownership
     * ends, so later repaints cannot duplicate it. */
    s_ble_transient_active = false;
    if (s_ble_transient_committed) return;
    s_ble_transient_committed = true;
    const std::string rendered = s_ble_model.devices().render();
    if (!rendered.empty()) append_output(rendered.data(), rendered.size());
}

bool ble_address_bytes(const std::string &address, uint8_t out[6])
{
    unsigned int bytes[6]{};
    if (std::sscanf(address.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X",
                    &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6)
        return false;
    for (int i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(bytes[i]);
    return true;
}

void ble_submit_actions()
{
    for (cyberdeck_ble::action &action : s_ble_model.take_actions()) {
        if ((action.kind == cyberdeck_ble::action_kind::start_scan ||
             action.kind == cyberdeck_ble::action_kind::pair ||
             action.kind == cyberdeck_ble::action_kind::connect) &&
            s_ble_background.scan_active()) {
            ble_mgr_cmd_t cancel{};
            cancel.kind = BLE_MGR_CMD_SCAN_CANCEL;
            cancel.token = s_ble_background.scan_token();
            if (cyberdeck_apps::service_ports::ble_enqueue(&cancel, 0) == ESP_OK) {
                s_ble_background.preempt_for_manual();
            }
        }
        ble_mgr_cmd_t cmd{};
        cmd.token = action.token;
        switch (action.kind) {
        case cyberdeck_ble::action_kind::start_scan: cmd.kind = BLE_MGR_CMD_SCAN_START; break;
        case cyberdeck_ble::action_kind::cancel_scan: cmd.kind = BLE_MGR_CMD_SCAN_CANCEL; break;
        case cyberdeck_ble::action_kind::pair: cmd.kind = BLE_MGR_CMD_PAIR; break;
        case cyberdeck_ble::action_kind::submit_auth: cmd.kind = BLE_MGR_CMD_PASSKEY_REPLY; break;
        case cyberdeck_ble::action_kind::cancel_pair: cmd.kind = BLE_MGR_CMD_PAIR_CANCEL; break;
        case cyberdeck_ble::action_kind::connect: cmd.kind = BLE_MGR_CMD_CONNECT; break;
        case cyberdeck_ble::action_kind::disconnect: cmd.kind = BLE_MGR_CMD_DISCONNECT; break;
        case cyberdeck_ble::action_kind::cancel_connect: cmd.kind = BLE_MGR_CMD_DISCONNECT; break;
        case cyberdeck_ble::action_kind::reconnect: cmd.kind = BLE_MGR_CMD_RECONNECT; break;
        }
        uint8_t address[6] = {};
        if (!action.address.empty() && !ble_address_bytes(action.address, address)) continue;
        if (!action.address.empty() &&
            s_ble_scan_devices.find(action.address, action.addr_type) == nullptr &&
            s_ble_model.devices().find(action.address, action.addr_type) == nullptr &&
            (action.kind == cyberdeck_ble::action_kind::pair ||
             action.kind == cyberdeck_ble::action_kind::connect ||
             action.kind == cyberdeck_ble::action_kind::reconnect)) continue;
        const uint8_t addr_type = static_cast<uint8_t>(action.addr_type);
        if (action.kind == cyberdeck_ble::action_kind::submit_auth) {
            std::memcpy(cmd.passkey.addr, address, sizeof(cmd.passkey.addr));
            cmd.passkey.addr_type = addr_type;
            cmd.passkey.passkey = action.passkey;
            cmd.passkey.numcmp = action.numcmp;
            cmd.passkey.numcmp_accept = action.numcmp_accept;
            switch (action.auth_action) {
            case cyberdeck_ble::auth_io_action::display: cmd.passkey.io_action = BLE_MGR_AUTH_IO_DISP; break;
            case cyberdeck_ble::auth_io_action::input: cmd.passkey.io_action = BLE_MGR_AUTH_IO_INPUT; break;
            case cyberdeck_ble::auth_io_action::numeric_compare: cmd.passkey.io_action = BLE_MGR_AUTH_IO_NUMCMP; break;
            }
        }
        if (action.kind == cyberdeck_ble::action_kind::pair ||
            action.kind == cyberdeck_ble::action_kind::cancel_pair) {
            std::memcpy(cmd.pair.addr, address, sizeof(cmd.pair.addr));
            cmd.pair.addr_type = addr_type;
        }
        if (action.kind == cyberdeck_ble::action_kind::connect ||
            action.kind == cyberdeck_ble::action_kind::reconnect) {
            std::memcpy(cmd.connect.addr, address, sizeof(cmd.connect.addr));
            cmd.connect.addr_type = addr_type;
            cmd.connect.automatic = action.kind == cyberdeck_ble::action_kind::reconnect;
        }
        (void)cyberdeck_apps::service_ports::ble_enqueue(&cmd, 0);
        /* The command queue owns the transient transport copy; do not retain
         * the passkey in the model action or UI-side vector after submission. */
        action.passkey = 0;
    }
}

void on_ble_event(const ble_mgr_event_t *event, void *)
{
    if (event == nullptr || s_ble_event_queue == nullptr) return;
    if (xQueueSend(s_ble_event_queue, event, 0) == pdTRUE) return;
    /* A bounded scan may produce more reports than the UI can consume.  The
     * terminal outcome is never optional: evict one queued scan report to
     * reserve its slot, while leaving auth/connection completions intact. */
    if (event->kind == BLE_MGR_EVT_SCAN_FINISHED) {
        ble_mgr_event_t retained[9]{};
        std::size_t count = 0;
        bool evicted = false;
        ble_mgr_event_t queued{};
        while (count < 9 && xQueueReceive(s_ble_event_queue, &queued, 0) == pdTRUE) {
            if (!evicted && queued.kind == BLE_MGR_EVT_SCAN_RESULT) { evicted = true; continue; }
            retained[count++] = queued;
        }
        for (std::size_t i = 0; i < count; ++i) (void)xQueueSend(s_ble_event_queue, &retained[i], 0);
        if (evicted) (void)xQueueSend(s_ble_event_queue, event, 0);
    }
}

void process_ble_events(lv_timer_t *)
{
    if (s_ble_observer == nullptr) {
        s_ble_observer = cyberdeck_apps::service_ports::ble_register_observer(on_ble_event, nullptr);
    }
    /* Restore NVS bonds once at boot before any deadline or queue work. */
    s_ble_background.restore_bonds_once(s_ble_model);
    /* Snapshot ownership only for rendering.  Deadline advancement is a
     * model concern and must run on every LVGL tick: shell-triggered scans
     * must not depend on owns_input(), an observer callback, or the event
     * queue being available. */
    bool changed = s_ble_model.owns_input();
    s_ble_model.advance_time(100);
    s_ble_background.tick(100);
    ble_submit_actions();

    /* A missing terminal GAP callback is still bounded by the pure model
     * deadline.  Keep processing the deadline even if queue setup failed. */
    ble_mgr_event_t event{};
    while (s_ble_event_queue != nullptr &&
           xQueueReceive(s_ble_event_queue, &event, 0) == pdTRUE) {
        changed = true;
        if (event.token != 0 && event.token == s_ble_background.abandoned_token() &&
            event.kind != BLE_MGR_EVT_SCAN_FINISHED) {
            continue;
        }
        const bool background_event = s_ble_background.scan_active() &&
                                      event.token == s_ble_background.scan_token();
        switch (event.kind) {
        case BLE_MGR_EVT_SCAN_RESULT: {
            cyberdeck_ble::device item;
            item.address = event.scan_result.address;
            item.addr_type = static_cast<cyberdeck_ble::address_type>(event.scan_result.addr_type);
            item.name = event.scan_result.name;
            item.rssi = event.scan_result.rssi;
            item.kind = static_cast<cyberdeck_ble::device_kind>(event.scan_result.kind);
            item.connectable = event.scan_result.connectable;
            item.paired = event.scan_result.paired;
            if (!background_event) (void)s_ble_scan_devices.add(item);
            /* Background scan reports never enter the interactive list: they
             * only feed the spontaneous-loss reconnect scheduler. */
            s_ble_background.on_scan_result(event, s_ble_model);
            break;
        }
        case BLE_MGR_EVT_SCAN_FINISHED: {
            if (background_event) {
                s_ble_background.on_scan_finished(event, s_ble_model);
                s_ble_scan_devices.clear();
                break;
            }
            const cyberdeck_ble::notice outcome =
                static_cast<cyberdeck_ble::notice>(event.scan_finished.outcome);
            if (outcome == cyberdeck_ble::notice::failed) {
                s_ble_model.scan_failed(event.token);
            } else if (outcome == cyberdeck_ble::notice::timed_out) {
                s_ble_model.scan_timed_out(event.token);
            } else {
                s_ble_model.scan_finished(event.token, s_ble_scan_devices.snapshot());
            }
            s_ble_scan_devices.clear();
            /* Successful scans have no notice text when they contain devices.
             * Render the terminal result independently from notice deduplication. */
            break;
        }
        case BLE_MGR_EVT_AUTH_REQUEST:
            s_ble_model.auth_requested(event.token,
                static_cast<cyberdeck_ble::auth_request_kind>(event.auth_request.kind),
                event.auth_request.passkey,
                static_cast<cyberdeck_ble::auth_io_action>(event.auth_request.io_action));
            s_shell_app.clear_ble_auth_input();
            break;
        case BLE_MGR_EVT_PAIR_FINISHED:
            s_ble_model.pairing_finished(event.token,
                static_cast<cyberdeck_ble::pair_outcome>(event.pair_finished.outcome));
            break;
        case BLE_MGR_EVT_CONNECTED:
            s_ble_model.connection_finished(event.token, true);
            break;
        case BLE_MGR_EVT_DISCONNECTED:
            s_ble_model.connection_finished(event.token, false);
            if (event.connection.automatic) s_ble_background.advance_target(s_ble_model);
            break;
        default: break;
        }
    }
    /* When idle with an armed cycle (first boot or spontaneous loss with no
     * pending event), keep the background observer progressing without
     * touching the visible screen or the terminal. */
    s_ble_background.maybe_reconnect(s_ble_model);
    if (s_ble_model.current_screen() != cyberdeck_ble::screen::auth) s_shell_app.clear_ble_auth_input();
    refresh_ble_status();
    if (changed) {
        ble_submit_actions();
        sync_ble_transient_block();
        const std::string notice = s_ble_model.notice_text();
        if (!notice.empty() && notice != s_ble_last_notice) {
            append_line(notice + "\n");
        }
        s_ble_last_notice = notice;
        render_terminal();
    }
}

void destroy_ui_resource_handles()
{
    /* Quiesce producers before deleting their LVGL callback targets. */
    s_keyboard_dispatch.stop();
    cyberdeck_cat_worker_teardown();
    cyberdeck_apps::service_ports::wifi_set_state_callback(nullptr, nullptr);
    if (s_clock_timer != nullptr) { lv_timer_del(s_clock_timer); s_clock_timer = nullptr; }
    if (s_wifi_state_timer != nullptr) { lv_timer_del(s_wifi_state_timer); s_wifi_state_timer = nullptr; }
    if (s_wifi_scan_timer != nullptr) { lv_timer_del(s_wifi_scan_timer); s_wifi_scan_timer = nullptr; }
    if (s_ble_timer != nullptr) { lv_timer_del(s_ble_timer); s_ble_timer = nullptr; }
    if (s_ssh_timer != nullptr) { lv_timer_del(s_ssh_timer); s_ssh_timer = nullptr; }
    if (s_wifi_audit_timer != nullptr) { lv_timer_del(s_wifi_audit_timer); s_wifi_audit_timer = nullptr; }
    if (s_battery_timer != nullptr) { lv_timer_del(s_battery_timer); s_battery_timer = nullptr; }
    if (s_terminal_output_timer != nullptr) {
        lv_timer_del(s_terminal_output_timer);
        s_terminal_output_timer = nullptr;
    }
    /* A deinit during BLE authentication must not leave the passkey resident.
     * Detaching also drops the console the shell application owns. */
    s_shell_app.detach_console();
    auto &window_manager = cyberdeck_window_manager_adapter::global();
    if (!s_shell_view_context.empty()) {
        (void)window_manager.policy().begin_teardown(s_shell_view_context);
        (void)window_manager.policy().remove(s_shell_view_context);
        s_shell_view_context = {};
    }
    window_manager.deinit();
    s_header_view = {};
    s_terminal_view = {};
    s_terminal = nullptr;
    s_keyboard = nullptr;
    s_wifi_audit.teardown();
    if (s_ble_observer != nullptr) {
        cyberdeck_apps::service_ports::ble_unregister_observer(s_ble_observer);
        s_ble_observer = nullptr;
    }
    if (s_ble_event_queue != nullptr) {
        vQueueDelete(s_ble_event_queue);
        s_ble_event_queue = nullptr;
    }
    s_ssh_expected_generation = 0;
    const bool ssh_stopped = cyberdeck_apps::service_ports::ssh_disconnect_and_wait(k_ssh_disconnect_timeout_ms);
    if (s_ssh_event_queue != nullptr) {
        if (ssh_stopped) {
            vQueueDelete(s_ssh_event_queue);
            s_ssh_event_queue = nullptr;
        } else {
            ESP_LOGE("cyberdeck_ui", "Timeout ao encerrar task SSH; fila mantida para evitar use-after-delete");
        }
    }
    if (s_wifi_scan_context_mutex != nullptr) {
        vSemaphoreDelete(s_wifi_scan_context_mutex);
        s_wifi_scan_context_mutex = nullptr;
    }
    if (s_wifi_scan_queue != nullptr) {
        vQueueDelete(s_wifi_scan_queue);
        s_wifi_scan_queue = nullptr;
    }
    if (s_wifi_state_queue != nullptr) {
        vQueueDelete(s_wifi_state_queue);
        s_wifi_state_queue = nullptr;
    }
    s_ui_ready = false;
}

void hidden(lv_obj_t *obj, bool value);
void local_key(uint32_t key);

void on_cat_result(const char *output, size_t output_length, bool accepted, void *)
{
    auto sanitize_for_lvgl = [](const char *input, size_t input_length) {
        std::string clean;
        if (input == nullptr) return clean;
        constexpr size_t limit = TERMINAL_LIMIT;
        const unsigned char *bytes = reinterpret_cast<const unsigned char *>(input);
        const auto replacement = [&clean]() {
            if (clean.size() + 3 <= TERMINAL_LIMIT) clean.append("\xEF\xBF\xBD");
        };
        for (size_t i = 0; i < input_length && clean.size() < limit;) {
            const unsigned char first = bytes[i];
            if (first < 0x80) {
                if (first == '\n' || first == '\r' || first == '\t' || (first >= 0x20 && first != 0x7F))
                    clean.push_back(static_cast<char>(first));
                else replacement();
                ++i;
                continue;
            }
            size_t length = first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC2 ? 2 : 0;
            uint32_t codepoint = first & (length == 4 ? 0x07 : length == 3 ? 0x0F : 0x1F);
            bool valid = length != 0;
            for (size_t j = 1; valid && j < length; ++j) {
                if (i + j >= input_length || (bytes[i + j] & 0xC0) != 0x80) valid = false;
                else codepoint = (codepoint << 6) | (bytes[i + j] & 0x3F);
            }
            if (valid && ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) ||
                          (length == 4 && (codepoint < 0x10000 || codepoint > 0x10FFFF)) ||
                          (codepoint >= 0xD800 && codepoint <= 0xDFFF))) valid = false;
            if (!valid) { replacement(); ++i; continue; }
            if (clean.size() + length > limit) break;
            clean.append(reinterpret_cast<const char *>(bytes + i), length);
            i += length;
        }
        return clean;
    };
    const std::string safe_output = sanitize_for_lvgl(output, output_length);
    if (accepted) append_line(safe_output);
    else append_line(output != nullptr && output_length != 0 ? safe_output : "cat: operation rejected\n");
    render_terminal();
}



void on_keyboard_event(const char *text, size_t length, uint8_t modifier,
                       uint32_t special_key, void *)
{
    /* lv_async_call invokes this on the LVGL task.  In particular, do not
     * acquire bsp_display_lock here: this callback is already in that
     * context, and some of the paths below can synchronously render. */
    lv_display_trigger_activity(lv_disp_get_default());
    if (s_keyboard) hidden(s_keyboard, true);
    if (text != nullptr && text[0] != '\0' && length != 0) {
        /* A modified key is an SSH escape sequence, not text.  The session
         * owns that decision; the UI only reports the modifier. */
        if (modifier & 0x01U && length == 1) {
            (void)s_shell_app.insert_modified_key(text[0], modifier);
        } else {
            (void)s_shell_app.insert_physical_text(text, length);
        }
    } else if (special_key) {
        local_key(special_key);
    }
}


struct wifi_scan_context {
    std::uint64_t generation;
};
std::uint64_t s_wifi_scan_generation = 0;
wifi_scan_context *s_wifi_scan_context = nullptr;




struct RenderGuard {
    RenderGuard() { s_rendering = true; }
    ~RenderGuard() { s_rendering = false; }
};

void zero_string(std::string &s) {
    if (!s.empty()) {
        memset(&s[0], 0, s.size());
        s.clear();
    }
}

void wipe_wifi_actions(std::vector<cyberdeck_wifi::action> &actions) {
    for (auto &action : actions) zero_string(action.password);
    actions.clear();
}


/* Prompt, line fitting and the UTF-8 helpers belong to the shell application
 * (cyberdeck_shell_console); the view only measures the scrollback it renders
 * around them. */
using cyberdeck_shell_console::truncate_left_utf8;
using cyberdeck_shell_console::utf8_char_count;
using cyberdeck_shell_console::utf8_valid_start_offset;

void disable_scrolling(lv_obj_t *obj) {
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_set_scroll_chain(obj, false);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

void style_base(lv_obj_t *obj, lv_color_t bg, lv_color_t text) {
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_text_color(obj, text, 0);
    lv_obj_set_style_text_font(obj, &cyberdeck_font, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}
void hidden(lv_obj_t *obj, bool value) { if (obj) lv_obj_set_hidden(obj, value); }

void refresh_ble_status()
{
    s_header_view.update_ble(s_ble_model.is_connected());
}

void refresh_battery_status()
{
    cyberdeck_battery_protection::snapshot value{};
    if (!battery_protection_started() ||
        !battery_protection_get_policy_snapshot(&value)) {
        s_header_view.update_battery({});
        return;
    }

    /* The pure view owns the state -> visibility/percentage/glyph mapping; the
     * LVGL layer only applies the result to the three labels. */
    const cyberdeck_battery_view::presentation shown =
        cyberdeck_battery_view::resolve(cyberdeck_battery_view::from_snapshot(value));
    s_header_view.update_battery(shown);
}

void process_battery_protection(lv_timer_t *)
{
    refresh_battery_status();
}

void update_clock(lv_timer_t *) {
    refresh_battery_status();
    std::string text;
    const time_t now = time(nullptr);
    /* An unset RTC commonly reports an early Unix epoch.  Do not expose that
     * value as if it were a usable wall clock. */
    if (now >= static_cast<time_t>(1577836800)) {
        struct tm utc = {};
        if (gmtime_r(&now, &utc) != nullptr) {
            const cyberdeck_clock_time_t utc_time = {
                static_cast<int16_t>(utc.tm_year + 1900),
                static_cast<uint8_t>(utc.tm_mon + 1),
                static_cast<uint8_t>(utc.tm_mday),
                static_cast<uint8_t>(utc.tm_hour),
                static_cast<uint8_t>(utc.tm_min)};
            cyberdeck_clock_time_t local = {};
            char formatted[sizeof("DD/MM/YYYY HH:MM")];
            if (cyberdeck_clock_from_utc(&utc_time,
                                         CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                                         &local) != false &&
                cyberdeck_format_clock(formatted, sizeof(formatted), &local) != 0) {
                text = formatted;
            }
        }
    }
    if (text == s_last_clock_text) return;
    s_last_clock_text = text;
    s_header_view.update_clock(text.c_str());
}

void process_wifi_state(lv_timer_t *) {
    /* Listener callbacks (including the HTTP screenshot server) are dispatched
     * only here, in the display task; Wi-Fi event tasks handle snapshots only. */
    cyberdeck_apps::service_ports::wifi_process_state_callbacks();
    if (s_wifi_state_queue == nullptr) return;
    wifi_state_update update = {};
    if (xQueueReceive(s_wifi_state_queue, &update, 0) != pdTRUE) return;

    const wifi_status_t *status = &update.status;
    const bool enabled = update.enabled;
    s_latest_wifi_status = *status;
    if (s_wifi_ui_state == cyberdeck_shell_session::wifi_ui_state_t::CONNECTING &&
        s_wifi_model.active_connection_token() == s_shell_app.wifi_model_connection_token() &&
        status->connection_token == s_shell_app.wifi_connection_token()) {
        if (status->connected && status->has_ip) {
            s_wifi_model.connection_callback(s_shell_app.wifi_model_connection_token(),
                                             cyberdeck_wifi::connection_event::connected);
            s_wifi_model.connection_callback(s_shell_app.wifi_model_connection_token(),
                                             cyberdeck_wifi::connection_event::has_ip);
            auto completed_actions = s_wifi_model.take_actions();
            wipe_wifi_actions(completed_actions);
            s_shell_app.invalidate_wifi_connection();
            append_line("Wi-Fi connected.\n");
            s_wifi_ui_state = cyberdeck_shell_session::wifi_ui_state_t::IDLE;
            s_shell_app.clear_editor();
            render_terminal();
        } else if (!status->connected) {
            s_wifi_model.connection_callback(s_shell_app.wifi_model_connection_token(),
                                             cyberdeck_wifi::connection_event::failed);
            auto failed_actions = s_wifi_model.take_actions();
            wipe_wifi_actions(failed_actions);
            s_shell_app.invalidate_wifi_connection();
            append_line("Wi-Fi connection failed or timed out.\n");
            s_wifi_ui_state = cyberdeck_shell_session::wifi_ui_state_t::IDLE;
            s_shell_app.clear_editor();
            render_terminal();
        }
    }
    const bool lit = cyberdeck_wifi_indicator_is_lit(enabled, status->connected,
                                                      status->has_ip);
    /* This callback is run by LVGL's timer handler, i.e. the display/UI
     * context.  Do not take bsp_display_lock() here: the display task already
     * owns it, and taking it again would deadlock. */
    s_header_view.update_wifi(lit);
}

void process_wifi_scan(lv_timer_t *) {
    if (s_wifi_scan_queue == nullptr) return;

    wifi_scan_result *result = nullptr;
    if (xQueueReceive(s_wifi_scan_queue, &result, 0) != pdTRUE || result == nullptr) return;

    const int count = result->count < 0 ? 0 :
                      (result->count > WIFI_SCAN_MAX_APS ? WIFI_SCAN_MAX_APS : result->count);
    if (result->generation == s_wifi_scan_generation &&
        s_wifi_ui_state == cyberdeck_shell_session::wifi_ui_state_t::SCANNING &&
        s_wifi_model.active_scan_token() == result->generation &&
        s_wifi_model.current_screen() == cyberdeck_wifi::screen::search) {
        std::vector<cyberdeck_wifi::access_point> model_aps;
        for (int i = 0; i < count; ++i) {
            model_aps.push_back({reinterpret_cast<const char *>(result->aps[i].ssid),
                                 result->aps[i].rssi,
                                 result->aps[i].authmode == WIFI_AUTH_OPEN, false});
        }
        s_wifi_model.scan_complete(result->generation, model_aps);
        if (count == 0) {
            append_line("No Wi-Fi networks found.\n");
            s_wifi_ui_state = cyberdeck_shell_session::wifi_ui_state_t::IDLE;
        } else {
            s_wifi_search_menu.set_aps(result->aps, count);
            if (s_wifi_search_menu.count() == 0) {
                append_line("No Wi-Fi networks found.\n");
                s_wifi_ui_state = cyberdeck_shell_session::wifi_ui_state_t::IDLE;
            } else {
                s_wifi_ui_state = cyberdeck_shell_session::wifi_ui_state_t::SEARCH_SELECT;
            }
        }
        render_terminal();
    }
    delete result;
}

std::string wifi_audit_save_path()
{
    constexpr char kTimeFormat[] = "%H%M%S";
    const time_t now = time(nullptr);
    if (now < static_cast<time_t>(1577836800)) return {};

    struct tm utc = {};
    if (gmtime_r(&now, &utc) == nullptr) return {};
    const cyberdeck_clock_time_t utc_time = {
        static_cast<int16_t>(utc.tm_year + 1900),
        static_cast<uint8_t>(utc.tm_mon + 1),
        static_cast<uint8_t>(utc.tm_mday),
        static_cast<uint8_t>(utc.tm_hour),
        static_cast<uint8_t>(utc.tm_min)};
    cyberdeck_clock_time_t local = {};
    if (!cyberdeck_clock_from_utc(&utc_time,
                                   CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                                   &local)) {
        return {};
    }

    struct tm local_tm = {};
    local_tm.tm_year = local.year - 1900;
    local_tm.tm_mon = local.month - 1;
    local_tm.tm_mday = local.day;
    local_tm.tm_hour = local.hour;
    local_tm.tm_min = local.minute;
    local_tm.tm_sec = utc.tm_sec;
    local_tm.tm_isdst = 0;
    char hhmmss[sizeof("HHMMSS")] = {};
    if (strftime(hhmmss, sizeof(hhmmss), kTimeFormat, &local_tm) !=
        sizeof(hhmmss) - 1) {
        return {};
    }

    char filename[64] = {};
    const int written = snprintf(filename, sizeof(filename),
                                 "wifi-audit-%04d%02u%02u-%s.txt",
                                 static_cast<int>(local.year),
                                 static_cast<unsigned>(local.month),
                                 static_cast<unsigned>(local.day),
                                 hhmmss);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(filename)) {
        return {};
    }
    return std::string("/sdcard/wifi-audit/") + filename;
}

void process_wifi_audit(lv_timer_t *) {
    const auto value = s_wifi_audit.snapshot_view();
    if (value.token != 0 &&
        (value.status == cyberdeck_wifi_audit::state::ready ||
         value.status == cyberdeck_wifi_audit::state::error) &&
        (value.token != s_wifi_audit_reported_token ||
         value.status != s_wifi_audit_reported_state)) {
        s_wifi_audit_reported_token = value.token;
        s_wifi_audit_reported_state = value.status;
        append_line(cyberdeck_wifi_audit::render_ui(value));
        render_terminal();
    }

    const auto result = s_wifi_audit.drain_save();
    if (result.ok) {
        std::string line = "wifi audit saved: ";
        line.append(result.path);
        line += '\n';
        append_line(line);
        render_terminal();
    } else if (result.path[0] != '\0') {
        append_line("wifi audit save failed\n");
        render_terminal();
    }
}

void on_wifi_state(const wifi_status_t *status, bool enabled, void *) {
    if (!status || s_wifi_state_queue == nullptr) return;
    wifi_state_update update = {*status, enabled};
    /* The Wi-Fi event task may call us directly.  Queue only a value snapshot;
     * all LVGL access and UI state transitions are deferred to the LVGL timer
     * context above. */
    (void)xQueueOverwrite(s_wifi_state_queue, &update);
}

void on_wifi_scan_done(const wifi_ap_record_t *aps, int count, void *ctx) {
    wifi_scan_context *scan = static_cast<wifi_scan_context *>(ctx);
    if (scan == nullptr) return;

    /* The manager transfers the callback context to this callback before it
     * can be cancelled.  Serialize that ownership handoff with the UI-side
     * cancellation path, then copy both the generation and AP records into an
     * independently owned queue item.  This callback must not inspect UI or
     * model state and must not touch LVGL. */
    if (s_wifi_scan_context_mutex == nullptr ||
        xSemaphoreTake(s_wifi_scan_context_mutex, portMAX_DELAY) != pdTRUE) {
        delete scan;
        return;
    }
    const std::uint64_t generation = scan->generation;
    xSemaphoreGive(s_wifi_scan_context_mutex);

    wifi_scan_result *result = new (std::nothrow) wifi_scan_result{};
    if (result != nullptr) {
        result->generation = generation;
        result->count = (aps == nullptr || count <= 0) ? 0 :
                        (count > WIFI_SCAN_MAX_APS ? WIFI_SCAN_MAX_APS : count);
        if (result->count > 0) {
            memcpy(result->aps, aps, static_cast<size_t>(result->count) * sizeof(result->aps[0]));
        }
        if (s_wifi_scan_queue == nullptr ||
            xQueueSend(s_wifi_scan_queue, &result, 0) != pdTRUE) {
            delete result;
        }
    }
    /* Remove the shared handle before releasing the callback-owned context.
     * A concurrent canceler can then distinguish an already completed callback
     * from an in-progress scan and will never delete this object twice. */
    if (s_wifi_scan_context_mutex != nullptr &&
        xSemaphoreTake(s_wifi_scan_context_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_wifi_scan_context == scan) s_wifi_scan_context = nullptr;
        xSemaphoreGive(s_wifi_scan_context_mutex);
    }
    delete scan;
}

cyberdeck_shell_console::line_view compose_console_line() {
    /* The prompt and the command line belong to the shell application.  The UI
     * only resolves which surface owns the input right now and hands that
     * state over; the application returns the marker, the fitted line and the
     * cursor already clamped to the visible window. */
    const ssh_client_state_t state = cyberdeck_apps::service_ports::ssh_state();
    cyberdeck_shell_console::surface_state surface;
    surface.ssh_connected = state == SSH_CLIENT_CONNECTED;
    surface.password_pending =
        (state == SSH_CLIENT_NEED_PASSWORD) ||
        (s_wifi_ui_state == cyberdeck_shell_session::wifi_ui_state_t::SEARCH_PASSWORD);
    surface.input_owned_elsewhere =
        s_ble_model.owns_input() ||
        s_wifi_ui_state == cyberdeck_shell_session::wifi_ui_state_t::SEARCH_SELECT ||
        s_wifi_ui_state == cyberdeck_shell_session::wifi_ui_state_t::SAVED_SELECT ||
        s_wifi_ui_state == cyberdeck_shell_session::wifi_ui_state_t::SAVED_CONFIRM;
    surface.cwd = s_local_shell.cwd();
    return s_shell_app.compose_line(surface);
}

std::string get_rendered_output(const cyberdeck_shell_console::line_view &view,
                                bool complete = false) {
    const size_t used = view.reserved();
    const size_t available = complete
                                 ? (TERMINAL_LIMIT > used ? TERMINAL_LIMIT - used : 0)
                                 : (std::min(viewport_bytes, TERMINAL_LIMIT) > used
                                        ? std::min(viewport_bytes, TERMINAL_LIMIT) - used
                                        : 0);
    const size_t viewport = complete
                                ? TERMINAL_LIMIT
                                : std::min(viewport_bytes, s_terminal_view.viewport_capacity());
    std::string output = complete ? s_scrollback.text() : s_scrollback.viewport(viewport);
    if (s_wifi_ui_state == cyberdeck_shell_session::wifi_ui_state_t::SEARCH_SELECT) {
        output += s_wifi_search_menu.render();
    } else if (s_wifi_ui_state == cyberdeck_shell_session::wifi_ui_state_t::SAVED_SELECT || s_wifi_ui_state == cyberdeck_shell_session::wifi_ui_state_t::SAVED_CONFIRM) {
        output += s_wifi_saved_menu.render();
        if (s_wifi_ui_state == cyberdeck_shell_session::wifi_ui_state_t::SAVED_CONFIRM) {
            output += "Press ENTER again to forget, ESC to keep.\n";
        }
    }
    if (ble_list_is_visible()) output += s_ble_model.devices().render();
    const cyberdeck_ble::screen ble_screen = s_ble_model.current_screen();
    if (ble_screen == cyberdeck_ble::screen::pairing ||
        ble_screen == cyberdeck_ble::screen::auth ||
        ble_screen == cyberdeck_ble::screen::connecting ||
        ble_screen == cyberdeck_ble::screen::connected) {
        output += s_ble_model.status_line();
        output += "\n";
        if (ble_screen == cyberdeck_ble::screen::auth &&
            s_ble_model.pending_auth_action() == cyberdeck_ble::auth_io_action::input) {
            output += "Passkey input: ";
            output.append(s_shell_app.ble_auth_input_size(), '*');
            output += "\n";
        }
    }
    if (!complete && output.size() > available)
        output = truncate_left_utf8(output, available);
    return output;
}

void render_terminal() {
    if (!s_terminal) return;
    const cyberdeck_shell_console::line_view view = compose_console_line();
    /* The view owns the bounded visual window and its touch offset.  Feed it
     * the complete bounded scrollback so a swipe can expose older lines; the
     * textual dump still reads directly from the model below. */
    std::string output = get_rendered_output(view, true);
    const std::string editor = view.text();
    std::string visual = output + editor;
    std::size_t cursor_byte = 0;
    std::size_t cursor_chars = 0;
    while (cursor_byte < editor.size() && cursor_chars < view.cursor_chars()) {
        ++cursor_byte;
        while (cursor_byte < editor.size() &&
               (static_cast<unsigned char>(editor[cursor_byte]) & 0xC0U) == 0x80U) ++cursor_byte;
        ++cursor_chars;
    }
    visual.insert(output.size() + cursor_byte, "|");

    RenderGuard guard;
    /* The line slots are the only visible surface.  The textarea remains an
     * input target for LVGL's keyboard and never receives visual scrollback. */
    s_terminal_view.render(visual);
    lv_textarea_set_text(s_terminal, editor.c_str());

    /* The textarea is also the editing surface while SSH is connected.  Do
     * not reset its cursor to the beginning: the model remains authoritative
     * in every session, and the application already clamped it to the fitted
     * UTF-8 window. */
    uint32_t char_pos = static_cast<uint32_t>(view.cursor_chars());
    lv_textarea_set_cursor_pos(s_terminal, char_pos);
    s_terminal_output_dirty = false;
}

void append_output(const char *data, size_t len, bool repaint) {
    if (!data || !len) return;
    s_scrollback.append(data, len);
    s_terminal_output_dirty = true;
    if (repaint) render_terminal();
}

void process_terminal_output(lv_timer_t *)
{
    if (s_terminal_output_dirty) render_terminal();
}

void reset_ssh_output_filter() {
    s_ssh_output_filter.flush(nullptr, 0);
}

uint32_t mark_ssh_event_discarded(std::atomic<uint32_t> &counter) {
    uint32_t dropped = counter.load(std::memory_order_relaxed);
    while (dropped != std::numeric_limits<uint32_t>::max() &&
           !counter.compare_exchange_weak(dropped, dropped + 1, std::memory_order_relaxed)) {}

    uint32_t epoch = s_ssh_discard_epoch.load(std::memory_order_relaxed);
    while (epoch != std::numeric_limits<uint32_t>::max() &&
           !s_ssh_discard_epoch.compare_exchange_weak(epoch, epoch + 1,
                                                      std::memory_order_relaxed)) {}
    return epoch;
}

uint32_t current_ssh_discard_epoch() {
    return s_ssh_discard_epoch.load(std::memory_order_relaxed);
}

void discard_ssh_line_composer() {
    s_ssh_line_composer.flush(nullptr, 0);
}

void append_line(const std::string &line) { append_output(line.data(), line.size()); }

void show_ssh(bool ssh) {
    (void)ssh;
    if (s_terminal) lv_obj_add_state(s_terminal, LV_STATE_FOCUSED);
    render_terminal();
}

void process_ssh_data(const char *data, size_t length) {
    if (!data || !length) return;
    std::string filtered(length + 1, '\0');
    const size_t written = s_ssh_output_filter.feed(data, length, &filtered[0], filtered.size());
    if (!written) return;
    std::string displayed(written + cyberdeck_edit_line::limit + 1, '\0');
    const size_t displayed_size = s_ssh_line_composer.feed(filtered.data(), written,
                                                           &displayed[0], displayed.size());
    append_output(displayed.data(), displayed_size, false);
}

void process_ssh_state(ssh_client_state_t state, const char *message) {
    static const char *names[] = {"OFFLINE", "CONNECTING", "PASSWORD", "AUTH", "ONLINE", "CLOSING", "ERROR", "HOST KEY"};
    size_t i = static_cast<size_t>(state);
    char status[180];
    snprintf(status, sizeof(status), "[%s] %s", i < sizeof(names) / sizeof(names[0]) ? names[i] : "UNKNOWN", message ? message : "");
    /* SSH state is part of the terminal/event log, not the compact header. */
    append_line(status);
    append_line("\n");
    cyberdeck_apps::logger *logger = cyberdeck_apps::global_runtime().app_logger();
    if (logger != nullptr) logger->write(state == SSH_CLIENT_ERROR ? 'E' : 'I', "ssh", status);
    if (state == SSH_CLIENT_DISCONNECTED || state == SSH_CLIENT_DISCONNECTING || state == SSH_CLIENT_ERROR) {
        char pending[1];
        const size_t written = s_ssh_output_filter.flush(pending, sizeof(pending));
        if (written) {
            std::string filtered(cyberdeck_edit_line::limit + 2, '\0');
            const size_t displayed = s_ssh_line_composer.feed(pending, written,
                                                               &filtered[0], filtered.size());
            append_output(filtered.data(), displayed);
        }
        std::string retained(cyberdeck_edit_line::limit + 1, '\0');
        const size_t displayed = s_ssh_line_composer.flush(&retained[0], retained.size());
        append_output(retained.data(), displayed);
    }
    if (state == SSH_CLIENT_NEED_PASSWORD || state == SSH_CLIENT_CONNECTED) {
        s_shell_app.clear_editor();
    }
    render_terminal();
}

void on_ssh_data(ssh_client_generation_t generation, const char *data, size_t length)
{
    if (s_ssh_event_queue == nullptr || data == nullptr || length == 0) return;
    if (length > k_ssh_event_data_limit) {
        /* Do not partially enqueue an invalid transport chunk: advance the
         * epoch so the ANSI consumer resynchronizes at the next event. */
        mark_ssh_event_discarded(s_ssh_data_queue_drop_count);
        return;
    }
    if (uxQueueMessagesWaiting(s_ssh_event_queue) >= k_ssh_event_queue_capacity - 1) {
        mark_ssh_event_discarded(s_ssh_data_queue_drop_count);
        return;
    }
    ssh_ui_event &event = s_ssh_data_event;
    event = {};
    event.generation = generation;
    event.discard_epoch = current_ssh_discard_epoch();
    event.kind = ssh_ui_event_kind::data;
    event.length = static_cast<uint16_t>(length);
    std::memcpy(event.data, data, event.length);
    if (xQueueSend(s_ssh_event_queue, &event, 0) != pdTRUE) {
        mark_ssh_event_discarded(s_ssh_data_queue_drop_count);
    }
}

void on_ssh_state(ssh_client_generation_t generation, ssh_client_state_t state, const char *message)
{
    if (s_ssh_event_queue == nullptr) return;
    ssh_ui_event &event = s_ssh_state_event;
    event = {};
    event.generation = generation;
    event.discard_epoch = current_ssh_discard_epoch();
    event.kind = ssh_ui_event_kind::state;
    event.state = state;
    if (message != nullptr) {
        std::snprintf(event.message, sizeof(event.message), "%s", message);
    }
    if (xQueueSend(s_ssh_event_queue, &event, 0) == pdTRUE) return;

    /* State transitions have priority over old data, but never silently replace
     * another state: a full state-only queue is an observable, bounded loss. */
    ssh_ui_event &discarded = s_ssh_discarded_event;
    if (xQueueReceive(s_ssh_event_queue, &discarded, 0) == pdTRUE &&
        discarded.kind == ssh_ui_event_kind::data) {
        mark_ssh_event_discarded(s_ssh_data_queue_drop_count);
        event.discard_epoch = current_ssh_discard_epoch();
        if (xQueueSend(s_ssh_event_queue, &event, 0) != pdTRUE) {
            mark_ssh_event_discarded(s_ssh_state_queue_drop_count);
        }
        return;
    }
    mark_ssh_event_discarded(s_ssh_state_queue_drop_count);
}

void process_ssh_events(lv_timer_t *)
{
    if (s_ssh_event_queue == nullptr) return;
    /* Reclaim into the shared slot instead of a local copy: the queue element
     * must not be materialized on the LVGL task stack. */
    while (xQueueReceive(s_ssh_event_queue, &s_ssh_event_slot, 0) == pdTRUE) {
        if (s_ssh_event_slot.generation != s_ssh_expected_generation) continue;
        if (s_ssh_event_slot.discard_epoch > s_ssh_applied_discard_epoch) {
            /* A queue gap can leave an ANSI sequence half-consumed.  Drop only
             * that uncertain parser tail; never synthesize or replay bytes. */
            reset_ssh_output_filter();
            s_ssh_output_filter.resync_after_gap();
            s_ssh_applied_discard_epoch = s_ssh_event_slot.discard_epoch;
        }
        if (s_ssh_event_slot.kind == ssh_ui_event_kind::data) {
            process_ssh_data(s_ssh_event_slot.data, s_ssh_event_slot.length);
        } else {
            process_ssh_state(s_ssh_event_slot.state, s_ssh_event_slot.message);
        }
    }
}




void focused(lv_event_t *event) {
    (void)event;
    if (s_keyboard && !tab5_keyboard_is_connected()) {
        lv_keyboard_set_textarea(s_keyboard, s_terminal); hidden(s_keyboard, false);
        lv_obj_set_size(s_keyboard, LV_PCT(100), 300);
        lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

void virtual_keyboard_changed(lv_event_t *event) {
    if (!event || lv_event_get_target(event) != s_keyboard || !s_keyboard) return;
    const uint32_t button = lv_keyboard_get_selected_button(s_keyboard);
    const char *text = lv_keyboard_get_button_text(s_keyboard, button);
    if (!text) return;

    // LVGL handles these two buttons directly on the textarea, so no
    // LV_EVENT_KEY reaches terminal_key.  Mirror the movement in the model
    // and render once more to keep the widget and s_cursor in lockstep.
    if (strcmp(text, LV_SYMBOL_LEFT) == 0) local_key(LV_KEY_LEFT);
    else if (strcmp(text, LV_SYMBOL_RIGHT) == 0) local_key(LV_KEY_RIGHT);
}

void terminal_insert(lv_event_t *event) {
    if (!event || s_rendering || !s_terminal) return;
    const char *inserted = static_cast<const char *>(lv_event_get_param(event));
    if (!inserted || !*inserted) return;

    /* Editing keys, passkey digits and multi-newline pastes are all session
     * decisions.  LVGL edits the textarea as part of this event, so the
     * session re-renders and the widget is restored afterwards. */
    bool virtual_enter_handled = false;
    (void)s_shell_app.insert_virtual_text(inserted, &virtual_enter_handled);
    s_virtual_enter_handled = virtual_enter_handled;
    render_terminal();
}

void terminal_changed(lv_event_t *) {
    if (s_rendering || !s_terminal) return;
    const char *text = lv_textarea_get_text(s_terminal); if (!text) return;
    // VALUE_CHANGED is retained only for the virtual keyboard's Enter.  Do
    // not reimport the textarea contents: it is a rendering surface, while
    // the session line/cursor are the single source of truth for editing.
    const size_t length = strlen(text);
    if (length > 0 && text[length - 1] == '\n') {
        if (s_virtual_enter_handled) s_virtual_enter_handled = false;
        else local_key(LV_KEY_ENTER);
    }
    render_terminal();
}

void terminal_key(lv_event_t *event) {
    if (!event || s_rendering) return;
    uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL ||
         key == LV_KEY_LEFT || key == LV_KEY_RIGHT || key == LV_KEY_UP || key == LV_KEY_DOWN ||
         key == LV_KEY_HOME || key == LV_KEY_END || key == LV_KEY_NEXT || key == LV_KEY_ESC) {
        local_key(key);
        // Do not let the textarea apply the navigation/editing key a second
        // time after the model-backed handler above.  In particular this
        // keeps virtual left/right arrows synchronized with the session cursor.
        lv_event_stop_processing(event);
        lv_event_stop_bubbling(event);
    }
}
} // namespace

namespace {
/* Shell session host: borrows UI-owned output, models, menus and async
 * handles.  Defined after every UI service so the overrides stay one-liners. */
void shell_session_host::append_output_line(const std::string &line) { append_line(line); }
void shell_session_host::write_output(const char *data, std::size_t length)
{
    append_output(data, length);
}
void shell_session_host::append_output_text(const std::string &text) { s_scrollback.append(text.data(), text.size()); }
void shell_session_host::clear_output() { s_scrollback.clear(); }
void shell_session_host::render() { render_terminal(); }
cyberdeck_ble::state_machine &shell_session_host::ble_model() { return s_ble_model; }
cyberdeck_ble::device_list &shell_session_host::ble_scan_devices() { return s_ble_scan_devices; }
std::size_t shell_session_host::copy_ble_bonds(ble_bond_snapshot_t *out, std::size_t capacity)
{
    return ble_bonds_copy(out, capacity);
}
void shell_session_host::clear_ble_notice() { s_ble_last_notice.clear(); }
void shell_session_host::mark_ble_transient_uncommitted() { s_ble_transient_committed = false; }
void shell_session_host::submit_ble_actions() { ble_submit_actions(); }
void shell_session_host::sync_ble_transient() { sync_ble_transient_block(); }
cyberdeck_shell_session::wifi_ui_state_t &shell_session_host::wifi_state() { return s_wifi_ui_state; }
cyberdeck_wifi::state_machine &shell_session_host::wifi_model() { return s_wifi_model; }
cyberdeck_wifi_search_menu &shell_session_host::wifi_search_menu() { return s_wifi_search_menu; }
cyberdeck_wifi_saved_menu &shell_session_host::wifi_saved_menu() { return s_wifi_saved_menu; }
std::uint64_t &shell_session_host::wifi_scan_generation() { return s_wifi_scan_generation; }
esp_err_t shell_session_host::wifi_connect(const char *ssid, const char *password)
{
    return cyberdeck_apps::service_ports::wifi_connect(ssid, password);
}
esp_err_t shell_session_host::wifi_cancel_connection() { return cyberdeck_apps::service_ports::wifi_cancel_connection(); }
void shell_session_host::wifi_forget(const char *ssid) { cyberdeck_apps::service_ports::wifi_forget(ssid); }
bool shell_session_host::wifi_enabled() const { return cyberdeck_apps::service_ports::wifi_enabled(); }
std::uint64_t shell_session_host::wifi_current_token() const { return cyberdeck_apps::service_ports::wifi_current_token(); }
bool shell_session_host::wifi_status(wifi_status_t *out) { return cyberdeck_apps::service_ports::wifi_status(out); }
bool shell_session_host::wifi_storage_ready() { return wifi_storage_mount() == ESP_OK; }
bool shell_session_host::wifi_storage_load_all(wifi_saved_list_t *list)
{
    return ::wifi_storage_load_all(list) == ESP_OK;
}
bool shell_session_host::wifi_storage_find(const char *ssid, char *out_password, std::size_t max_len)
{
    return ::wifi_storage_find(ssid, out_password, max_len);
}
bool shell_session_host::wifi_begin_scan(std::uint64_t generation)
{
    wifi_scan_context *scan = new (std::nothrow) wifi_scan_context{generation};
    if (s_wifi_scan_context_mutex != nullptr) {
        xSemaphoreTake(s_wifi_scan_context_mutex, portMAX_DELAY);
        s_wifi_scan_context = scan;
        xSemaphoreGive(s_wifi_scan_context_mutex);
    }
    const esp_err_t err = scan == nullptr ? ESP_ERR_NO_MEM : cyberdeck_apps::service_ports::wifi_scan(on_wifi_scan_done, scan);
    if (err != ESP_OK) {
        if (s_wifi_scan_context_mutex != nullptr) {
            xSemaphoreTake(s_wifi_scan_context_mutex, portMAX_DELAY);
            if (s_wifi_scan_context == scan) s_wifi_scan_context = nullptr;
            xSemaphoreGive(s_wifi_scan_context_mutex);
        }
        delete scan;
        return false;
    }
    return true;
}
void shell_session_host::wifi_cancel_scan()
{
    /* Do not hold the UI ownership lock while the manager arbitrates
     * callback ownership.  The manager returns true only when the callback
     * owns ctx and will release it. */
    if (s_wifi_scan_context_mutex == nullptr) return;
    xSemaphoreTake(s_wifi_scan_context_mutex, portMAX_DELAY);
    wifi_scan_context *scan = s_wifi_scan_context;
    xSemaphoreGive(s_wifi_scan_context_mutex);
    const bool callback_owned = scan != nullptr && cyberdeck_apps::service_ports::wifi_cancel_scan(on_wifi_scan_done, scan);
    xSemaphoreTake(s_wifi_scan_context_mutex, portMAX_DELAY);
    const bool still_current = s_wifi_scan_context == scan;
    if (still_current) s_wifi_scan_context = nullptr;
    if (scan != nullptr && still_current && !callback_owned) delete scan;
    xSemaphoreGive(s_wifi_scan_context_mutex);
}
std::string shell_session_host::build_wifi_audit_save_path() { return wifi_audit_save_path(); }
void shell_session_host::wifi_audit_begin()
{
    if (!s_wifi_audit.initialized()) s_wifi_audit.initialize();
    (void)s_wifi_audit.begin({false, {}, {}, {}});
}
bool shell_session_host::wifi_audit_save(const std::string &path)
{
    return s_wifi_audit.enqueue_save(s_wifi_audit.snapshot_view().token, path);
}
void shell_session_host::set_ssh_visible(bool visible) { show_ssh(visible); }
void shell_session_host::reset_ssh_filter() { reset_ssh_output_filter(); }
void shell_session_host::discard_ssh_composer() { discard_ssh_line_composer(); }
cyberdeck_ssh_line_composer &shell_session_host::ssh_composer() { return s_ssh_line_composer; }
esp_err_t shell_session_host::ssh_connect(const char *user, const char *host, int port)
{
    const esp_err_t result = cyberdeck_apps::service_ports::ssh_connect(
        user, host, port, on_ssh_data, on_ssh_state);
    if (result == ESP_OK) {
        s_ssh_expected_generation = cyberdeck_apps::service_ports::ssh_generation();
        s_ssh_applied_discard_epoch = current_ssh_discard_epoch();
    }
    return result;
}
cyberdeck_session_state shell_session_host::ssh_phase() const
{
    switch (cyberdeck_apps::service_ports::ssh_state()) {
    case SSH_CLIENT_NEED_PASSWORD: return cyberdeck_session_state::PASSWORD;
    case SSH_CLIENT_NEED_HOST_KEY: return cyberdeck_session_state::HOST_KEY;
    case SSH_CLIENT_CONNECTED: return cyberdeck_session_state::CONNECTED;
    default: return cyberdeck_session_state::MENU;
    }
}
esp_err_t shell_session_host::ssh_send_data(const char *data, std::size_t length)
{
    return cyberdeck_apps::service_ports::ssh_send_data(data, length);
}
esp_err_t shell_session_host::ssh_send_password(const char *password)
{
    return cyberdeck_apps::service_ports::ssh_send_password(password);
}
void shell_session_host::ssh_accept_host_key() { cyberdeck_apps::service_ports::ssh_accept_host_key(); }
void shell_session_host::screen_turn_on() { screen_off_turn_on(); }
void shell_session_host::screen_turn_off() { screen_off_turn_off(); }
esp_err_t shell_session_host::screen_set_timeout_minutes(std::uint16_t minutes)
{
    return screen_off_set_timeout_minutes(minutes);
}
bool shell_session_host::battery_protection_set_enabled(bool enabled)
{
    return ::battery_protection_set_enabled(enabled);
}
bool shell_session_host::battery_protection_started() const
{
    return ::battery_protection_started();
}
bool shell_session_host::battery_protection_snapshot(
    cyberdeck_battery_protection::snapshot *out) const
{
    return ::battery_protection_get_policy_snapshot(out);
}
void shell_session_host::log_event(char level, const char *tag, const char *message)
{
    cyberdeck_apps::logger *logger = cyberdeck_apps::global_runtime().app_logger();
    if (logger != nullptr) logger->write(level, tag, message);
}
std::string shell_session_host::recent_events(std::size_t count)
{
    std::string logged;
    cyberdeck_apps::logger *logger = cyberdeck_apps::global_runtime().app_logger();
    if (logger == nullptr) return logged;
    logger->latest(count, [](const char *event, void *ctx) {
        if (event == nullptr || ctx == nullptr) return;
        auto *out = static_cast<std::string *>(ctx);
        out->append(event);
        out->append("\n");
    }, &logged);
    return logged;
}
bool shell_session_host::cat_enqueue(const char *cwd, const char *line)
{
    return s_cat_worker_ready && cyberdeck_cat_worker_enqueue(cwd, line);
}
cyberdeck_local_shell &shell_session_host::local_shell() { return s_local_shell; }
 cyberdeck_apps::runtime &shell_session_host::app_runtime() { return cyberdeck_apps::global_runtime(); }

/* LVGL key codes never cross into the session: translate here so the
 * controller depends only on its own key vocabulary. */
cyberdeck_shell_session::key translate_session_key(uint32_t key)
{
    using session_key = cyberdeck_shell_session::key;
    switch (key) {
    case LV_KEY_UP: return session_key::up;
    case LV_KEY_DOWN: return session_key::down;
    case LV_KEY_LEFT: return session_key::left;
    case LV_KEY_RIGHT: return session_key::right;
    case LV_KEY_HOME: return session_key::home;
    case LV_KEY_END: return session_key::end;
    case LV_KEY_NEXT: return session_key::next_tab;
    case LV_KEY_ENTER: return session_key::enter;
    case LV_KEY_BACKSPACE: return session_key::backspace;
    case LV_KEY_DEL: return session_key::del;
    case LV_KEY_ESC: return session_key::esc;
    default: return session_key::unknown;
    }
}

void local_key(uint32_t key) { s_shell_app.handle_key(translate_session_key(key)); }

} // namespace

extern "C" esp_err_t cyberdeck_ui_term_dump(char *buffer, size_t capacity, size_t *out_bytes,
                                             int *out_truncated)
{
    if (out_bytes == nullptr || out_truncated == nullptr || (capacity > 0 && buffer == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_bytes = 0;
    *out_truncated = 0;
    if (!bsp_display_lock(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_terminal == nullptr) {
        bsp_display_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    const cyberdeck_shell_console::line_view view = compose_console_line();
    const std::string rendered = get_rendered_output(view, true) + view.text();
    const std::string snapshot = rendered.size() > capacity
                                     ? truncate_left_utf8(rendered, capacity)
                                     : rendered;
    if (!snapshot.empty()) {
        std::memcpy(buffer, snapshot.data(), snapshot.size());
    }
    *out_bytes = snapshot.size();
    *out_truncated = rendered.size() > snapshot.size() ? 1 : 0;
    bsp_display_unlock();
    return ESP_OK;
}

extern "C" esp_err_t cyberdeck_ui_init(void) {
       if (s_ui_ready) return ESP_OK;
       /* Lend the session host to the shell application before anything can
        * compose a prompt or dispatch a key.  The console itself is created by
        * the supervisor's start hook, so the shell owns its own lifecycle. */
       s_shell_app.attach_console(s_shell_session_host);
       if (!s_keyboard_dispatch.start(on_keyboard_event, nullptr)) return ESP_ERR_NO_MEM;
       s_wifi_state_queue = xQueueCreate(1, sizeof(wifi_state_update));
       if (s_wifi_state_queue == nullptr) {
           destroy_ui_resource_handles();
           return ESP_ERR_NO_MEM;
       }
       /* Keep one late result alongside the current scan.  Generation checks
        * discard it without allowing it to starve the newer result. */
       s_wifi_scan_queue = xQueueCreate(2, sizeof(wifi_scan_result *));
       s_wifi_scan_context_mutex = xSemaphoreCreateMutex();
         if (s_wifi_scan_queue == nullptr || s_wifi_scan_context_mutex == nullptr) {
            destroy_ui_resource_handles();
             return ESP_ERR_NO_MEM;
         }
          s_ble_event_queue = xQueueCreate(9, sizeof(ble_mgr_event_t));
          if (s_ble_event_queue == nullptr) {
             destroy_ui_resource_handles();
              return ESP_ERR_NO_MEM;
          }
          s_ssh_event_queue = xQueueCreate(k_ssh_event_queue_capacity, sizeof(ssh_ui_event));
          if (s_ssh_event_queue == nullptr) {
              destroy_ui_resource_handles();
              return ESP_ERR_NO_MEM;
          }
         s_ble_observer = cyberdeck_apps::service_ports::ble_register_observer(on_ble_event, nullptr);
         s_cat_worker_ready = cyberdeck_cat_worker_start("/sdcard", on_cat_result, nullptr);
      auto &window_manager = cyberdeck_window_manager_adapter::global();
      if (!window_manager.init()) {
          destroy_ui_resource_handles();
          return ESP_ERR_NO_MEM;
      }
      if (window_manager.policy().create(1, s_shell_view_context) != cyberdeck_window_manager::view_status::ok) {
          destroy_ui_resource_handles();
          return ESP_ERR_NO_MEM;
      }
      s_screen = window_manager.screen();
      s_menu = window_manager.content();
      style_base(s_menu, BLACK, WHITE); lv_obj_set_style_pad_all(s_menu, 0, 0); disable_scrolling(s_menu);
      if (!s_header_view.create(window_manager.system_bar())) {
          destroy_ui_resource_handles();
          return ESP_ERR_NO_MEM;
      }
      cyberdeck_apps::service_ports::wifi_set_state_callback(on_wifi_state, nullptr);
s_last_clock_text.clear();
    update_clock(nullptr);
       s_clock_timer = lv_timer_create(update_clock, 1000, nullptr);
       s_wifi_state_timer = lv_timer_create(process_wifi_state, 100, nullptr);
       s_wifi_scan_timer = lv_timer_create(process_wifi_scan, 100, nullptr);
        s_ble_timer = lv_timer_create(process_ble_events, 100, nullptr);
        s_ssh_timer = lv_timer_create(process_ssh_events, 100, nullptr);
       s_wifi_audit_timer = lv_timer_create(process_wifi_audit, 100, nullptr);
       s_battery_timer = lv_timer_create(process_battery_protection, 1000, nullptr);
       s_terminal_output_timer = lv_timer_create(process_terminal_output, 100, nullptr);
       if (s_clock_timer == nullptr || s_wifi_state_timer == nullptr ||
           s_wifi_scan_timer == nullptr || s_ble_timer == nullptr ||
           s_ssh_timer == nullptr || s_wifi_audit_timer == nullptr ||
           s_battery_timer == nullptr || s_terminal_output_timer == nullptr) {
          destroy_ui_resource_handles();
          return ESP_ERR_NO_MEM;
      }
       const cyberdeck_terminal_view::callbacks terminal_callbacks{
          focused, terminal_insert, terminal_changed, terminal_key,
          virtual_keyboard_changed, terminal_geometry_changed};
       if (!s_terminal_view.create(s_screen, s_menu, TERMINAL_LIMIT, terminal_callbacks)) {
           destroy_ui_resource_handles();
           return ESP_ERR_NO_MEM;
       }
       s_terminal = s_terminal_view.textarea();
      reset_ssh_output_filter();
     discard_ssh_line_composer();
     s_ble_transient_active = false;
     s_ble_transient_committed = false;
       s_scrollback.clear();
       s_scrollback.append("CYBERDECK5 READY\n", sizeof("CYBERDECK5 READY\n") - 1);
       render_terminal();
       /* The shell supervisor starts after this function returns and may run
        * on the boot task.  Let the existing LVGL timer perform the first
        * prompt render in the authorized context. */
       s_terminal_output_dirty = true;

       s_keyboard = s_terminal_view.keyboard();
     s_ui_ready = true;
     return ESP_OK;
}

extern "C" void cyberdeck_ui_deinit(void)
{
     s_cat_worker_ready = false;
    destroy_ui_resource_handles();
}

extern "C" void cyberdeck_keyboard_input(const char *text, size_t length, uint8_t modifier, uint32_t special_key) {
      s_keyboard_dispatch.submit(text, length, modifier, special_key);
}
