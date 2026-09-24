#include "platform/display/cyberdeck_ui.h"
#include "platform/logging/event_log.h"
#include "features/shell/cyberdeck_history.h"
#include "features/shell/cyberdeck_shell_utils.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "features/ssh/ssh_client.h"
#include "platform/input/tab5_keyboard.h"
#include "features/wifi/wifi_mgr.h"
#include "platform/display/cyberdeck_wifi_indicator.h"
#include "platform/display/cyberdeck_wifi_icon.h"
#include "platform/display/cyberdeck_clock.h"
#include "platform/sensors/ina226_reader.h"
#include "features/shell/cyberdeck_terminal_filter.h"
#include "features/shell/cyberdeck_ssh_line_composer.h"
#include "features/wifi/cyberdeck_wifi_menu.h"
#include "features/wifi/cyberdeck_wifi_state_machine.h"
#include "features/wifi/cyberdeck_wifi_audit.h"
#include "features/shell/cyberdeck_edit_line.h"
#include "features/shell/cyberdeck_local_shell.h"
#include "features/shell/cyberdeck_cat_worker.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <ctime>
#include <cstdint>
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

extern const lv_font_t cyberdeck_font;

namespace {
enum class wifi_ui_state_t {
    IDLE,
    SCANNING,
    SEARCH_SELECT,
    SEARCH_PASSWORD,
    CONNECTING,
    SAVED_SELECT,
    SAVED_CONFIRM
};

constexpr size_t TERMINAL_LIMIT = 12288;
constexpr UBaseType_t KEYBOARD_EVENT_QUEUE_CAPACITY = 8;
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
lv_obj_t *s_clock_status = nullptr;
lv_obj_t *s_wifi_status = nullptr;
lv_obj_t *s_battery_status = nullptr;
lv_obj_t *s_battery_symbol = nullptr;
lv_obj_t *s_battery_percentage = nullptr;
lv_obj_t *s_keyboard = nullptr;
std::string s_output;
std::string s_line;
cyberdeck_edit_line s_editor;
cyberdeck_history s_history;
cyberdeck_terminal_filter s_ssh_output_filter;
cyberdeck_ssh_line_composer s_ssh_line_composer;
size_t s_cursor = 0;
bool s_rendering = false;
bool s_virtual_enter_handled = false;
std::string s_last_clock_text;
cyberdeck_local_shell s_local_shell("/sdcard");
bool s_cat_worker_ready = false;
wifi_ui_state_t s_wifi_ui_state = wifi_ui_state_t::IDLE;
cyberdeck_wifi_search_menu s_wifi_search_menu;
cyberdeck_wifi_saved_menu s_wifi_saved_menu;
std::string s_selected_ap_ssid;
cyberdeck_wifi::state_machine s_wifi_model;
 cyberdeck_wifi_audit::audit_controller s_wifi_audit;
 std::uint64_t s_wifi_audit_reported_token = 0;
 cyberdeck_wifi_audit::state s_wifi_audit_reported_state = cyberdeck_wifi_audit::state::unavailable;
std::uint64_t s_wifi_connection_token = 0;
std::uint64_t s_wifi_model_connection_token = 0;
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
QueueHandle_t s_keyboard_event_queue = nullptr;
SemaphoreHandle_t s_keyboard_async_mutex = nullptr;

struct keyboard_event_context {
    size_t length;
    uint8_t modifier;
    uint32_t special_key;
    char text[];
};

void discard_keyboard_event_from_queue(keyboard_event_context *event)
{
    keyboard_event_context *pending[KEYBOARD_EVENT_QUEUE_CAPACITY]{};
    UBaseType_t pending_count = 0;

    while (pending_count < KEYBOARD_EVENT_QUEUE_CAPACITY &&
           xQueueReceive(s_keyboard_event_queue, &pending[pending_count], 0) == pdTRUE) {
        ++pending_count;
    }

    for (UBaseType_t i = 0; i < pending_count; ++i) {
        if (pending[i] == event) continue;
        (void)xQueueSend(s_keyboard_event_queue, &pending[i], 0);
    }
}

void destroy_ui_resource_handles()
{
    s_wifi_audit.teardown();
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
    if (s_keyboard_async_mutex != nullptr) {
        vSemaphoreDelete(s_keyboard_async_mutex);
        s_keyboard_async_mutex = nullptr;
    }
    if (s_keyboard_event_queue != nullptr) {
        vQueueDelete(s_keyboard_event_queue);
        s_keyboard_event_queue = nullptr;
    }
}

void zero_string(std::string &s);
void append_line(const std::string &line);
void hidden(lv_obj_t *obj, bool value);
void sync_editor();
void sync_line();
void render_terminal();
void execute_line(bool line_already_sent);
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

bool is_cat_request(const std::string &line)
{
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.compare(first, 3, "cat") != 0) return false;
    return first + 3 == line.size() || line[first + 3] == ' ' || line[first + 3] == '\t';
}

bool is_cat_help_request(const std::string &line)
{
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.compare(first, 3, "cat") != 0) return false;
    size_t option = line.find_first_not_of(" \t", first + 3);
    if (option == std::string::npos) return false;
    const size_t end = line.find_first_of(" \t", option);
    const std::string argument = end == std::string::npos ? line.substr(option) : line.substr(option, end - option);
    if (argument != "-h" && argument != "--help") return false;
    return end == std::string::npos || line.find_first_not_of(" \t", end) == std::string::npos;
}

void process_keyboard_event_async(void *)
{
    keyboard_event_context *event = nullptr;
    if (s_keyboard_event_queue == nullptr || s_keyboard_async_mutex == nullptr) {
        return;
    }

    /* The producer keeps this mutex across enqueue + lv_async_call().  Do
     * not let this callback dequeue the snapshot until lv_async_call() has
     * returned; otherwise a failed scheduling call could roll back an event
     * that this callback has already consumed and freed. */
    if (xSemaphoreTake(s_keyboard_async_mutex, portMAX_DELAY) != pdTRUE) return;
    const BaseType_t received = xQueueReceive(s_keyboard_event_queue, &event, 0);
    xSemaphoreGive(s_keyboard_async_mutex);
    if (received != pdTRUE || event == nullptr) return;

    /* lv_async_call invokes this on the LVGL task.  In particular, do not
     * acquire bsp_display_lock here: this callback is already in that
     * context, and some of the paths below can synchronously render. */
    lv_display_trigger_activity(lv_disp_get_default());
    if (s_keyboard) hidden(s_keyboard, true);
    if (event->text[0] != '\0' && event->length != 0) {
        sync_editor();
        if (event->modifier & 0x01U && event->length == 1) {
            std::string seq = cyberdeck_encode_ssh_key(
                static_cast<uint8_t>(event->text[0]), event->modifier);
            if (!seq.empty()) ssh_client_send_data(seq.data(), seq.size());
        } else if (s_editor.insert_physical(event->text, event->length)) {
            sync_line();
            render_terminal();
        }
    } else if (event->special_key) {
        local_key(event->special_key);
    }
    free(event);
}

bool begin_ui_wifi_connection(const char *ssid, const char *password)
{
    if (ssid == nullptr) return false;
    s_wifi_model.begin_connection(ssid, password != nullptr ? password : "");
    auto actions = s_wifi_model.take_actions();
    if (actions.empty() || actions.front().kind != cyberdeck_wifi::action_kind::connect) return false;
    const std::uint64_t model_token = actions.front().token;
    if (wifi_mgr_connect(ssid, password) != ESP_OK) {
        s_wifi_model.connection_callback(model_token, cyberdeck_wifi::connection_event::failed);
        auto failed_actions = s_wifi_model.take_actions();
        for (auto &action : failed_actions) zero_string(action.password);
        failed_actions.clear();
        s_wifi_ui_state = wifi_ui_state_t::IDLE;
        s_wifi_model_connection_token = 0;
        s_wifi_connection_token = 0;
        zero_string(s_line);
        s_editor.clear();
        s_cursor = 0;
        append_line("Wi-Fi connection could not be started.\n");
        for (auto &action : actions) zero_string(action.password);
        actions.clear();
        return false;
    }
    s_wifi_model_connection_token = model_token;
    s_wifi_connection_token = wifi_mgr_connection_token();
    s_wifi_ui_state = wifi_ui_state_t::CONNECTING;
    for (auto &action : actions) zero_string(action.password);
    actions.clear();
    return true;
}

struct wifi_scan_context {
    std::uint64_t generation;
};
std::uint64_t s_wifi_scan_generation = 0;
wifi_scan_context *s_wifi_scan_context = nullptr;

cyberdeck_session_state editor_session(ssh_client_state_t state)
{
    if (state == SSH_CLIENT_NEED_PASSWORD) return cyberdeck_session_state::PASSWORD;
    if (state == SSH_CLIENT_NEED_HOST_KEY) return cyberdeck_session_state::HOST_KEY;
    if (state == SSH_CLIENT_CONNECTED) return cyberdeck_session_state::CONNECTED;
    return cyberdeck_session_state::MENU;
}

void sync_editor()
{
    s_editor.set_session(editor_session(ssh_client_get_state()));
    s_line = s_editor.line();
    s_cursor = s_editor.cursor();
}

void sync_line()
{
    s_line = s_editor.line();
    s_cursor = s_editor.cursor();
}

void render_terminal();
void append_line(const std::string &line);

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

void zero_bytes(char *buffer, size_t size) {
    if (buffer == nullptr) return;
    volatile unsigned char *p = reinterpret_cast<volatile unsigned char *>(buffer);
    while (size-- != 0) *p++ = 0;
}

void wipe_wifi_actions(std::vector<cyberdeck_wifi::action> &actions) {
    for (auto &action : actions) zero_string(action.password);
    actions.clear();
}

struct string_wiper {
    std::string &value;
    ~string_wiper() { zero_string(value); }
};

size_t utf8_char_count(const std::string &str) {
    size_t count = 0;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else i += 1;
        count++;
    }
    return count;
}

size_t utf8_valid_start_offset(const std::string &str, size_t drop_bytes) {
    if (drop_bytes >= str.size()) return str.size();
    while (drop_bytes < str.size() && (static_cast<unsigned char>(str[drop_bytes]) & 0xC0) == 0x80) {
        drop_bytes++;
    }
    return drop_bytes;
}

std::string truncate_left_utf8(const std::string &str, size_t max_bytes) {
    if (str.size() <= max_bytes) return str;
    if (max_bytes == 0) return {};
    const size_t drop_bytes = str.size() - max_bytes;
    return str.substr(utf8_valid_start_offset(str, drop_bytes));
}

std::string fit_prompt_marker(const std::string &marker) {
    if (marker.size() <= TERMINAL_LIMIT) return marker;

    /* The local prompt must retain its final directory component and the
     * shell terminator even when a deeply nested path exceeds the terminal
     * budget.  The marker is always formed as <cwd> + "$ ". */
    constexpr size_t prompt_suffix_size = 2;
    if (marker.size() >= prompt_suffix_size && marker.compare(marker.size() - prompt_suffix_size,
                                                               prompt_suffix_size, "$ ") == 0) {
        const size_t cwd_budget = TERMINAL_LIMIT > prompt_suffix_size
                                ? TERMINAL_LIMIT - prompt_suffix_size : 0;
        return truncate_left_utf8(marker.substr(0, marker.size() - prompt_suffix_size), cwd_budget) + "$ ";
    }
    return truncate_left_utf8(marker, TERMINAL_LIMIT);
}

std::string fit_visible_line(const std::string &line, size_t marker_bytes) {
    const size_t budget = TERMINAL_LIMIT > marker_bytes ? TERMINAL_LIMIT - marker_bytes : 0;
    return truncate_left_utf8(line, budget);
}

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

const char *battery_level_symbol(const std::int32_t percentage)
{
    if (percentage >= 100) return LV_SYMBOL_BATTERY_FULL;
    if (percentage >= 75) return LV_SYMBOL_BATTERY_3;
    if (percentage >= 50) return LV_SYMBOL_BATTERY_2;
    if (percentage >= 25) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

void refresh_battery_status()
{
    if (s_battery_status == nullptr || s_battery_symbol == nullptr ||
        s_battery_percentage == nullptr) {
        return;
    }

    cyberdeck_battery::snapshot value{};
    const bool reader_started = ina226_reader_started();
    const bool battery_available = reader_started &&
        ina226_reader_get_snapshot(&value) && value.available;
    if (!battery_available) {
        lv_obj_add_flag(s_battery_status, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const std::int32_t percentage = value.percentage < 0
        ? 0 : value.percentage > 100 ? 100 : value.percentage;
    if (value.charge == cyberdeck_battery::charge_class::charging) {
        lv_label_set_text(s_battery_symbol, LV_SYMBOL_CHARGE " " LV_SYMBOL_PLUS);
    } else {
        lv_label_set_text(s_battery_symbol, battery_level_symbol(percentage));
    }

    char percentage_text[8] = {};
    snprintf(percentage_text, sizeof(percentage_text), "%d%%", static_cast<int>(percentage));
    lv_label_set_text(s_battery_percentage, percentage_text);
    lv_obj_clear_flag(s_battery_status, LV_OBJ_FLAG_HIDDEN);
}

void update_clock(lv_timer_t *) {
    refresh_battery_status();
    if (!s_clock_status) return;

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
    lv_label_set_text(s_clock_status, text.c_str());
}

void process_wifi_state(lv_timer_t *) {
    /* Listener callbacks (including the HTTP screenshot server) are dispatched
     * only here, in the display task; Wi-Fi event tasks handle snapshots only. */
    wifi_mgr_process_state_callbacks();
    if (s_wifi_state_queue == nullptr) return;
    wifi_state_update update = {};
    if (xQueueReceive(s_wifi_state_queue, &update, 0) != pdTRUE) return;

    const wifi_status_t *status = &update.status;
    const bool enabled = update.enabled;
    s_latest_wifi_status = *status;
    if (s_wifi_ui_state == wifi_ui_state_t::CONNECTING &&
        s_wifi_model.active_connection_token() == s_wifi_model_connection_token &&
        status->connection_token == s_wifi_connection_token) {
        if (status->connected && status->has_ip) {
            s_wifi_model.connection_callback(s_wifi_model_connection_token,
                                             cyberdeck_wifi::connection_event::connected);
            s_wifi_model.connection_callback(s_wifi_model_connection_token,
                                             cyberdeck_wifi::connection_event::has_ip);
            auto completed_actions = s_wifi_model.take_actions();
            wipe_wifi_actions(completed_actions);
            s_wifi_model_connection_token = 0;
            s_wifi_connection_token = 0;
            append_line("Wi-Fi connected.\n");
            s_wifi_ui_state = wifi_ui_state_t::IDLE;
            zero_string(s_line);
            s_editor.clear();
            s_cursor = 0;
            render_terminal();
        } else if (!status->connected) {
            s_wifi_model.connection_callback(s_wifi_model_connection_token,
                                             cyberdeck_wifi::connection_event::failed);
            auto failed_actions = s_wifi_model.take_actions();
            wipe_wifi_actions(failed_actions);
            s_wifi_model_connection_token = 0;
            s_wifi_connection_token = 0;
            append_line("Wi-Fi connection failed or timed out.\n");
            s_wifi_ui_state = wifi_ui_state_t::IDLE;
            zero_string(s_line);
            s_editor.clear();
            s_cursor = 0;
            render_terminal();
        }
    }
    if (!s_wifi_status) return;
    const bool lit = cyberdeck_wifi_indicator_is_lit(enabled, status->connected,
                                                      status->has_ip);
    /* This callback is run by LVGL's timer handler, i.e. the display/UI
     * context.  Do not take bsp_display_lock() here: the display task already
     * owns it, and taking it again would deadlock. */
    cyberdeck_wifi_icon_set_color(s_wifi_status, lit ? 0xF2F2F2 : 0x8A8A8A);
}

void process_wifi_scan(lv_timer_t *) {
    if (s_wifi_scan_queue == nullptr) return;

    wifi_scan_result *result = nullptr;
    if (xQueueReceive(s_wifi_scan_queue, &result, 0) != pdTRUE || result == nullptr) return;

    const int count = result->count < 0 ? 0 :
                      (result->count > WIFI_SCAN_MAX_APS ? WIFI_SCAN_MAX_APS : result->count);
    if (result->generation == s_wifi_scan_generation &&
        s_wifi_ui_state == wifi_ui_state_t::SCANNING &&
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
            s_output += "No Wi-Fi networks found.\n";
            s_wifi_ui_state = wifi_ui_state_t::IDLE;
        } else {
            s_wifi_search_menu.set_aps(result->aps, count);
            if (s_wifi_search_menu.count() == 0) {
                s_output += "No Wi-Fi networks found.\n";
                s_wifi_ui_state = wifi_ui_state_t::IDLE;
            } else {
                s_wifi_ui_state = wifi_ui_state_t::SEARCH_SELECT;
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

void render_terminal();

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

std::string get_rendered_output() {
    const ssh_client_state_t state = ssh_client_get_state();
    const bool password = (state == SSH_CLIENT_NEED_PASSWORD) || (s_wifi_ui_state == wifi_ui_state_t::SEARCH_PASSWORD);
    const bool connected = state == SSH_CLIENT_CONNECTED;
    sync_editor();
    const std::string visible_line = connected ? s_editor.visible_line() : (password ? std::string(s_line.size(), '*') : s_line);
    const std::string marker = connected ? "" : (password ? "Password: " : (s_wifi_ui_state == wifi_ui_state_t::SEARCH_SELECT || s_wifi_ui_state == wifi_ui_state_t::SAVED_SELECT || s_wifi_ui_state == wifi_ui_state_t::SAVED_CONFIRM ? "" : fit_prompt_marker(s_local_shell.cwd() + "$ ")));
    const std::string fitted_line = fit_visible_line(visible_line, marker.size());
    const size_t used = marker.size() + fitted_line.size();
    const size_t available = TERMINAL_LIMIT > used ? TERMINAL_LIMIT - used : 0;
    std::string output = s_output;
    if (s_wifi_ui_state == wifi_ui_state_t::SEARCH_SELECT) {
        output += s_wifi_search_menu.render();
    } else if (s_wifi_ui_state == wifi_ui_state_t::SAVED_SELECT || s_wifi_ui_state == wifi_ui_state_t::SAVED_CONFIRM) {
        output += s_wifi_saved_menu.render();
        if (s_wifi_ui_state == wifi_ui_state_t::SAVED_CONFIRM) {
            output += "Press ENTER again to forget, ESC to keep.\n";
        }
    }
    if (output.size() > available) output = truncate_left_utf8(output, available);
    return output;
}

void render_terminal() {
    if (!s_terminal) return;
    const ssh_client_state_t state = ssh_client_get_state();
    const bool password = (state == SSH_CLIENT_NEED_PASSWORD) || (s_wifi_ui_state == wifi_ui_state_t::SEARCH_PASSWORD);
    const bool connected = state == SSH_CLIENT_CONNECTED;
    sync_editor();
    const std::string visible_line = connected ? s_editor.visible_line() : (password ? std::string(s_line.size(), '*') : s_line);
    const std::string marker = connected ? "" : (password ? "Password: " : (s_wifi_ui_state == wifi_ui_state_t::SEARCH_SELECT || s_wifi_ui_state == wifi_ui_state_t::SAVED_SELECT || s_wifi_ui_state == wifi_ui_state_t::SAVED_CONFIRM ? "" : fit_prompt_marker(s_local_shell.cwd() + "$ ")));
    const std::string fitted_line = fit_visible_line(visible_line, marker.size());

    std::string output = get_rendered_output();
    std::string text = output + marker + fitted_line;

    RenderGuard guard;
    lv_textarea_set_text(s_terminal, text.c_str());

    /* The textarea is also the editing surface while SSH is connected.  Do
     * not reset its cursor to the beginning: the model remains authoritative
     * in every session. */
    size_t cursor_bytes = s_cursor;
    if (cursor_bytes > s_line.size()) cursor_bytes = s_line.size();
    const size_t line_start = visible_line.size() - fitted_line.size();
    if (cursor_bytes < line_start) cursor_bytes = line_start;

    uint32_t char_pos = static_cast<uint32_t>(utf8_char_count(output) + utf8_char_count(marker) +
                                              utf8_char_count(visible_line.substr(line_start, cursor_bytes - line_start)));
    lv_textarea_set_cursor_pos(s_terminal, char_pos);
}

void append_output(const char *data, size_t len) {
    if (!data || !len) return;
    s_output.append(data, len);
    if (s_output.size() > TERMINAL_LIMIT) {
        size_t excess = s_output.size() - TERMINAL_LIMIT;
        size_t safe_offset = utf8_valid_start_offset(s_output, excess);
        s_output.erase(0, safe_offset);
    }
    render_terminal();
}

void reset_ssh_output_filter() {
    s_ssh_output_filter.flush(nullptr, 0);
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

void on_ssh_data(const char *data, size_t length) {
    if (!data || !length) return;
    std::string filtered(length + 1, '\0');
    const size_t written = s_ssh_output_filter.feed(data, length, &filtered[0], filtered.size());
    if (!written) return;
    std::string displayed(written + cyberdeck_edit_line::limit + 1, '\0');
    const size_t displayed_size = s_ssh_line_composer.feed(filtered.data(), written,
                                                           &displayed[0], displayed.size());
    append_output(displayed.data(), displayed_size);
}

void on_ssh_state(ssh_client_state_t state, const char *message) {
    static const char *names[] = {"OFFLINE", "CONNECTING", "PASSWORD", "AUTH", "ONLINE", "CLOSING", "ERROR", "HOST KEY"};
    size_t i = static_cast<size_t>(state);
    char status[180];
    snprintf(status, sizeof(status), "[%s] %s", i < sizeof(names) / sizeof(names[0]) ? names[i] : "UNKNOWN", message ? message : "");
    /* SSH state is part of the terminal/event log, not the compact header. */
    append_line(status);
    append_line("\n");
    event_log_write(state == SSH_CLIENT_ERROR ? 'E' : 'I', "ssh", status);
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
        zero_string(s_line);
        s_editor.clear();
        s_cursor = 0;
    }
    render_terminal();
}

void execute_line(bool line_already_sent = false) {
    const ssh_client_state_t state = ssh_client_get_state();
    /* enter() clears the editor, so reject a connected Enter first. */
    if (state == SSH_CLIENT_CONNECTED && s_ssh_line_composer.active()) {
        render_terminal();
        return;
    }
    sync_editor();
    cyberdeck_enter_result entered = s_editor.enter(line_already_sent);
    string_wiper entered_wiper{entered.payload};
    std::string line = entered.payload;
    string_wiper line_wiper{line};
    zero_string(s_line);
    s_cursor = 0;
    s_history.reset_position();

    if (state == SSH_CLIENT_NEED_HOST_KEY) {
        event_log_write('I', "shell", "host key accepted");
        ssh_client_accept_host_key();
        render_terminal();
        return;
    }
    if (state == SSH_CLIENT_CONNECTED) {
        if (entered.action == cyberdeck_enter_action::SEND_LINE_NEWLINE ||
            entered.action == cyberdeck_enter_action::SEND_NEWLINE) {
            event_log_write('I', "ssh", "command sent to interactive session");
            reset_ssh_output_filter();
            const size_t command_length = entered.payload.empty() ? 0 : entered.payload.size() - 1;
            std::string local(command_length + 1, '\0');
            const size_t local_size = s_ssh_line_composer.begin(
                entered.payload.data(), command_length, &local[0], local.size());
            append_output(local.data(), local_size);
            if (ssh_client_send_data(entered.payload.data(), entered.payload.size()) != ESP_OK) {
                discard_ssh_line_composer();
            }
        } else {
            render_terminal();
            return;
        }
        render_terminal();
        return;
    }
    if (line.find_first_not_of(" \t") == std::string::npos) {
        render_terminal();
        return;
    }
    if (state == SSH_CLIENT_NEED_PASSWORD) {
        if (entered.action == cyberdeck_enter_action::SEND_PASSWORD)
            ssh_client_send_password(line.c_str());
        zero_string(line);
        render_terminal();
        return;
    }
    if (s_wifi_ui_state == wifi_ui_state_t::SEARCH_PASSWORD) {
        char msg[128];
        snprintf(msg, sizeof(msg), "\nConnecting to %s...\n", s_selected_ap_ssid.c_str());
        append_line(msg);
        (void)begin_ui_wifi_connection(s_selected_ap_ssid.c_str(), line.c_str());
        zero_string(line);
        render_terminal();
        return;
    }
    if (entered.action != cyberdeck_enter_action::LOCAL_COMMAND) { render_terminal(); return; }
    append_line(entered.echo);
    event_log_write('I', "shell", line.c_str());
    s_history.add(line);
    /* cat is the only local command whose file I/O is deliberately moved off
     * the LVGL task.  CAT_WORK_QUEUE_CAPACITY is bounded in the worker. */
    if (is_cat_request(line) && !is_cat_help_request(line)) {
        if (!s_cat_worker_ready || !cyberdeck_cat_worker_enqueue(s_local_shell.cwd().c_str(), line.c_str()))
            append_line("cat: worker queue unavailable\n");
        render_terminal();
        return;
    }
    const cyberdeck_local_shell_result local = s_local_shell.execute(line);
    if (local.status == cyberdeck_local_shell_status::handled) {
        append_line(local.output);
        render_terminal();
        return;
    }
    if (local.status == cyberdeck_local_shell_status::rejected) {
        append_line(local.output.empty() ? "command rejected\n" : local.output);
        render_terminal();
        return;
    }
    cyberdeck_cmd_t cmd = cyberdeck_parse_command(line.c_str());
    switch (cmd.type) {
    case CYBERDECK_CMD_HELP: append_line(cyberdeck_help_text()); break;
        case CYBERDECK_CMD_CLEAR: s_output.clear(); reset_ssh_output_filter(); discard_ssh_line_composer(); break;
    case CYBERDECK_CMD_WIFI: {
        wifi_status_t st = {};
        if (wifi_mgr_get_status(&st) == ESP_OK) {
            char out[128]; snprintf(out, sizeof(out), "wifi: %s%s%s\n", wifi_mgr_is_enabled() ? "enabled" : "disabled", st.connected ? " connected " : " disconnected", st.connected ? st.ip : ""); append_line(out);
        } else append_line("wifi: unavailable\n");
        break;
    }
    case CYBERDECK_CMD_WIFI_SEARCH: {
        if (!wifi_mgr_is_enabled()) {
            append_line("wifi: disabled\n");
            break;
        }
        s_wifi_ui_state = wifi_ui_state_t::SCANNING;
        s_wifi_model.begin_search();
        append_line("Scanning Wi-Fi networks...\n");
        s_wifi_scan_generation = s_wifi_model.active_scan_token();
        wifi_scan_context *scan = new (std::nothrow) wifi_scan_context{s_wifi_scan_generation};
        if (s_wifi_scan_context_mutex != nullptr) {
            xSemaphoreTake(s_wifi_scan_context_mutex, portMAX_DELAY);
            s_wifi_scan_context = scan;
            xSemaphoreGive(s_wifi_scan_context_mutex);
        }
        esp_err_t err = scan == nullptr ? ESP_ERR_NO_MEM : wifi_mgr_scan(on_wifi_scan_done, scan);
        if (err != ESP_OK) {
            if (s_wifi_scan_context_mutex != nullptr) {
                xSemaphoreTake(s_wifi_scan_context_mutex, portMAX_DELAY);
                if (s_wifi_scan_context == scan) s_wifi_scan_context = nullptr;
                xSemaphoreGive(s_wifi_scan_context_mutex);
            }
            delete scan;
            append_line("Failed to start Wi-Fi scan.\n");
            s_wifi_ui_state = wifi_ui_state_t::IDLE;
        }
        break;
    }
    case CYBERDECK_CMD_WIFI_AUDIT: {
        if (!s_wifi_audit.initialized()) s_wifi_audit.initialize();
        (void)s_wifi_audit.begin({false, {}, {}, {}});
        break;
    }
    case CYBERDECK_CMD_WIFI_AUDIT_SAVE: {
        const auto value = s_wifi_audit.snapshot_view();
        const std::string path = wifi_audit_save_path();
        if (!path.empty() && s_wifi_audit.enqueue_save(value.token, path)) {
            append_line("wifi audit save requested\n");
        } else {
            append_line("wifi audit save unavailable\n");
        }
        break;
    }
    case CYBERDECK_CMD_WIFI_SAVED: {
        s_wifi_model.begin_saved();
        wifi_saved_list_t list;
        if (wifi_storage_mount() != ESP_OK || wifi_storage_load_all(&list) != ESP_OK || list.count == 0) {
            append_line("No saved Wi-Fi networks.\n");
            break;
        }
        s_wifi_saved_menu.set_list(list);
        for (int i = 0; i < list.count; ++i) {
            zero_bytes(list.items[i].password, sizeof(list.items[i].password));
        }
        s_wifi_ui_state = wifi_ui_state_t::SAVED_SELECT;
        break;
    }
    case CYBERDECK_CMD_LOG: {
        append_line("ultimos eventos:\n");
        const size_t count = event_log_latest(10, [](const char *event, void *) {
            append_line(event);
            append_line("\n");
        }, nullptr);
        if (count == 0) {
            append_line("(nenhum evento disponivel)\n");
        }
        break;
    }
    case CYBERDECK_CMD_SSH: {
        std::string user, host; int port = 22;
        if (cmd.args.empty() || !cyberdeck_parse_ssh_target(cmd.args.c_str(), user, host, port)) { append_line("usage: ssh [user@]host[:port]\n"); break; }
        reset_ssh_output_filter();
        discard_ssh_line_composer();
        show_ssh(true);
        if (ssh_client_connect(user.c_str(), host.c_str(), port, on_ssh_data, on_ssh_state) != ESP_OK) {
            const char *error = "[ERROR] unable to start SSH session";
            append_line(error);
            append_line("\n");
            event_log_write('E', "ssh", error);
        }
        break;
    }
    case CYBERDECK_CMD_EMPTY: break;
    default: append_line("unknown command; type help\n"); break;
    }
    render_terminal();
}

void move_history(int direction) {
    if (s_history.empty()) return;
    if (direction < 0) s_history.move_up();
    else s_history.move_down();
    s_line = s_history.current();
    s_cursor = s_line.size();
    s_editor.clear();
    s_editor.insert(s_line.data(), s_line.size());
}

void local_key(uint32_t key) {
    if (s_wifi_ui_state == wifi_ui_state_t::SEARCH_SELECT) {
        if (key == LV_KEY_UP) {
            s_wifi_search_menu.move_up();
            render_terminal();
            return;
        } else if (key == LV_KEY_DOWN) {
            s_wifi_search_menu.move_down();
            render_terminal();
            return;
        } else if (key == LV_KEY_ESC) {
            s_output.append(s_wifi_search_menu.render());
            append_line("Wi-Fi search cancelled.\n");
            s_wifi_ui_state = wifi_ui_state_t::IDLE;
            render_terminal();
            return;
        } else if (key == LV_KEY_ENTER) {
            const auto *ap = s_wifi_search_menu.selected_item();
            if (ap) {
                s_selected_ap_ssid = ap->ssid;
                s_output.append(s_wifi_search_menu.render());
                if (ap->is_open) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Connecting to %s...\n", ap->ssid);
                    append_line(msg);
                    (void)begin_ui_wifi_connection(ap->ssid, "");
                } else {
                    char saved_pwd[65] = "";
                    bool has_saved = wifi_storage_find(ap->ssid, saved_pwd, sizeof(saved_pwd));
                    if (has_saved) {
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Connecting to %s...\n", ap->ssid);
                        append_line(msg);
                        (void)begin_ui_wifi_connection(ap->ssid, saved_pwd);
                        zero_bytes(saved_pwd, sizeof(saved_pwd));
                    } else {
                        char prompt[160];
                        snprintf(prompt, sizeof(prompt), "Password for %s:\n", ap->ssid);
                        append_line(prompt);
                        s_wifi_ui_state = wifi_ui_state_t::SEARCH_PASSWORD;
                        zero_string(s_line); s_editor.clear(); s_cursor = 0;
                    }
                }
            } else {
                s_wifi_ui_state = wifi_ui_state_t::IDLE;
            }
            render_terminal();
            return;
        }
    } else if (s_wifi_ui_state == wifi_ui_state_t::SAVED_SELECT) {
        if (key == LV_KEY_UP) {
            s_wifi_saved_menu.move_up();
            render_terminal();
            return;
        } else if (key == LV_KEY_DOWN) {
            s_wifi_saved_menu.move_down();
            render_terminal();
            return;
        } else if (key == LV_KEY_ESC) {
            s_output.append(s_wifi_saved_menu.render());
            append_line("Done.\n");
            s_wifi_ui_state = wifi_ui_state_t::IDLE;
            render_terminal();
            return;
        } else if (key == LV_KEY_ENTER) {
            std::string ssid_to_forget = s_wifi_saved_menu.selected_ssid();
            if (!ssid_to_forget.empty()) {
                append_line("Press ENTER again to forget this network, or ESC to keep it.\n");
                s_wifi_ui_state = wifi_ui_state_t::SAVED_CONFIRM;
            }
            render_terminal();
            return;
        }
    } else if (s_wifi_ui_state == wifi_ui_state_t::SAVED_CONFIRM) {
        if (key == LV_KEY_ESC) {
            s_wifi_ui_state = wifi_ui_state_t::SAVED_SELECT;
            render_terminal();
            return;
        } else if (key == LV_KEY_ENTER) {
            std::string ssid_to_forget = s_wifi_saved_menu.selected_ssid();
            if (!ssid_to_forget.empty()) {
                wifi_mgr_forget(ssid_to_forget.c_str());
                char msg[128];
                snprintf(msg, sizeof(msg), "Forgot network '%s'.\n", ssid_to_forget.c_str());
                append_line(msg);
                s_wifi_saved_menu.remove_selected();
                if (s_wifi_saved_menu.count() == 0) {
                    append_line("No more saved networks.\n");
                    s_wifi_ui_state = wifi_ui_state_t::IDLE;
                }
            }
            s_wifi_ui_state = s_wifi_saved_menu.count() == 0 ? wifi_ui_state_t::IDLE : wifi_ui_state_t::SAVED_SELECT;
            render_terminal();
            return;
        }
    } else if (s_wifi_ui_state == wifi_ui_state_t::SCANNING) {
        if (key == LV_KEY_ESC) {
            ++s_wifi_scan_generation;
            s_wifi_model.press(cyberdeck_wifi::key::escape);
            wifi_scan_context *scan = nullptr;
            if (s_wifi_scan_context_mutex != nullptr) {
                xSemaphoreTake(s_wifi_scan_context_mutex, portMAX_DELAY);
                scan = s_wifi_scan_context;
                xSemaphoreGive(s_wifi_scan_context_mutex);
                /* Do not hold the UI ownership lock while the manager
                 * arbitrates callback ownership.  The manager returns true
                 * only when the callback owns ctx and will release it. */
                const bool callback_owned = scan != nullptr && wifi_mgr_cancel_scan(on_wifi_scan_done, scan);
                xSemaphoreTake(s_wifi_scan_context_mutex, portMAX_DELAY);
                const bool still_current = s_wifi_scan_context == scan;
                if (still_current) s_wifi_scan_context = nullptr;
                if (scan != nullptr && still_current && !callback_owned) delete scan;
                xSemaphoreGive(s_wifi_scan_context_mutex);
            }
            s_wifi_ui_state = wifi_ui_state_t::IDLE;
            s_wifi_model_connection_token = 0;
            s_wifi_connection_token = 0;
            append_line("Wi-Fi search cancelled.\n");
            render_terminal();
            return;
        }
    } else if (s_wifi_ui_state == wifi_ui_state_t::SEARCH_PASSWORD) {
        if (key == LV_KEY_ESC) {
            s_wifi_model.press(cyberdeck_wifi::key::escape);
            auto actions = s_wifi_model.take_actions();
            wipe_wifi_actions(actions);
            append_line("\nWi-Fi connect cancelled.\n");
            s_wifi_ui_state = wifi_ui_state_t::IDLE;
            s_wifi_model_connection_token = 0;
            s_wifi_connection_token = 0;
            zero_string(s_line); s_editor.clear(); s_cursor = 0;
            render_terminal();
            return;
        }
    }

    if (s_wifi_ui_state == wifi_ui_state_t::CONNECTING && key == LV_KEY_ESC) {
        ++s_wifi_connection_token; /* invalidate callbacks before the worker runs */
        s_wifi_model.cancel_connection();
        auto cancelled_actions = s_wifi_model.take_actions();
        wipe_wifi_actions(cancelled_actions);
        (void)wifi_mgr_cancel_connection();
        s_wifi_ui_state = wifi_ui_state_t::IDLE;
        zero_string(s_line); s_editor.clear(); s_cursor = 0;
        append_line("Wi-Fi connect cancelled.\n");
        render_terminal();
        return;
    }

    if (s_wifi_ui_state == wifi_ui_state_t::CONNECTING) {
        render_terminal();
        return;
    }
    sync_editor();
    if (key == LV_KEY_ENTER) execute_line();
    else if (key == LV_KEY_BACKSPACE) s_editor.backspace();
    else if (key == LV_KEY_DEL) s_editor.del();
    else if (key == LV_KEY_LEFT) s_editor.cursor_left();
    else if (key == LV_KEY_RIGHT) s_editor.cursor_right();
    else if (key == LV_KEY_HOME) s_editor.cursor_home();
    else if (key == LV_KEY_END) s_editor.cursor_end();
    else if (key == LV_KEY_NEXT) s_editor.insert("\t", 1);
    else if (key == LV_KEY_UP) move_history(-1);
    else if (key == LV_KEY_DOWN) move_history(1);
    sync_line();
    render_terminal();
}

void focused(lv_event_t *event) {
    (void)event;
    if (s_keyboard && !tab5_keyboard_is_connected()) {
        lv_keyboard_set_textarea(s_keyboard, s_terminal); hidden(s_keyboard, false);
        lv_obj_set_size(s_keyboard, LV_PCT(100), 300); lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
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

    // The virtual keyboard reports editing keys through INSERT as a single
    // control byte.  They are actions, not text: route them through the same
    // UTF-8-aware editor used by the physical keyboard.
    uint32_t editing_key = 0;
    if (inserted[1] == '\0') {
        if (static_cast<unsigned char>(inserted[0]) == 0x7F) editing_key = LV_KEY_BACKSPACE;
        else if (static_cast<unsigned char>(inserted[0]) == 0x08) editing_key = LV_KEY_DEL;
    }

    // LVGL edits the textarea as part of this event.  Keep the edit state in
    // s_line instead; render_terminal() will restore the widget afterwards.
    if (editing_key) {
        local_key(editing_key);
        return;
    }

    /* LVGL may deliver a paste/chunk containing one or more newlines.  Keep
     * every part of the chunk and execute each line exactly once instead of
     * dropping the complete insertion. */
    sync_editor();
    const char *part = inserted;
    bool had_newline = false;
    while (*part != '\0') {
        const char *newline = strchr(part, '\n');
        const size_t length = newline != nullptr
                            ? static_cast<size_t>(newline - part)
                            : strlen(part);
        if (length != 0 && s_editor.insert_virtual(part, length)) sync_line();
        if (newline == nullptr) break;
        had_newline = true;
        execute_line();
        part = newline + 1;
        sync_editor();
        /* A paste may contain several newlines, but CONNECTED can have only
         * one line in flight.  Stop after the first Enter while its echo is
         * pending, retaining the next pasted line for editing instead of
         * turning it into another send. */
        if (ssh_client_get_state() == SSH_CLIENT_CONNECTED &&
            s_ssh_line_composer.active()) {
            const char *next_newline = strchr(part, '\n');
            const size_t remainder = next_newline != nullptr
                                   ? static_cast<size_t>(next_newline - part)
                                   : strlen(part);
            if (remainder != 0 && s_editor.insert_virtual(part, remainder)) sync_line();
            break;
        }
    }
    s_virtual_enter_handled = had_newline && part == inserted + strlen(inserted);
    render_terminal();
}

void terminal_changed(lv_event_t *) {
    if (s_rendering || !s_terminal) return;
    const char *text = lv_textarea_get_text(s_terminal); if (!text) return;
    // VALUE_CHANGED is retained only for the virtual keyboard's Enter.  Do
    // not reimport the textarea contents: it is a rendering surface, while
    // s_line/s_cursor are the single source of truth for editing.
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
        // keeps virtual left/right arrows synchronized with s_cursor.
        lv_event_stop_processing(event);
        lv_event_stop_bubbling(event);
    }
}
} // namespace

extern "C" esp_err_t cyberdeck_ui_init(void) {
       s_keyboard_event_queue = xQueueCreate(KEYBOARD_EVENT_QUEUE_CAPACITY,
                                             sizeof(keyboard_event_context *));
       s_keyboard_async_mutex = xSemaphoreCreateMutex();
       if (s_keyboard_event_queue == nullptr || s_keyboard_async_mutex == nullptr) {
           if (s_keyboard_event_queue != nullptr) {
               vQueueDelete(s_keyboard_event_queue);
               s_keyboard_event_queue = nullptr;
           }
           if (s_keyboard_async_mutex != nullptr) {
               vSemaphoreDelete(s_keyboard_async_mutex);
               s_keyboard_async_mutex = nullptr;
           }
           return ESP_ERR_NO_MEM;
       }
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
        s_cat_worker_ready = cyberdeck_cat_worker_start("/sdcard", on_cat_result, nullptr);
     s_screen = lv_scr_act(); style_base(s_screen, BLACK, WHITE); lv_obj_set_style_pad_all(s_screen, 12, 0); lv_obj_set_layout(s_screen, LV_LAYOUT_NONE); disable_scrolling(s_screen);
    s_menu = lv_obj_create(s_screen); lv_obj_set_size(s_menu, LV_PCT(100), LV_PCT(100)); style_base(s_menu, BLACK, WHITE); lv_obj_set_style_pad_all(s_menu, 0, 0); lv_obj_set_flex_flow(s_menu, LV_FLEX_FLOW_COLUMN); disable_scrolling(s_menu);
    lv_obj_t *header = lv_obj_create(s_menu);
    lv_obj_set_size(header, lv_pct(100), 42);
    style_base(header, BLACK, WHITE);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_column(header, 0, 0);
    lv_obj_set_style_pad_row(header, 0, 0);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    disable_scrolling(header);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "CYBERDECK5");
    lv_obj_set_width(title, LV_PCT(30));
    style_base(title, BLACK, WHITE);

    s_clock_status = lv_label_create(header);
    lv_label_set_text(s_clock_status, "");
    lv_obj_set_width(s_clock_status, LV_PCT(40));
    lv_obj_set_style_text_align(s_clock_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_clock_status, LV_LABEL_LONG_CLIP);
    style_base(s_clock_status, BLACK, MUTED);

    lv_obj_t *header_right = lv_obj_create(header);
    lv_obj_set_width(header_right, LV_PCT(30));
    lv_obj_set_height(header_right, 42);
    style_base(header_right, BLACK, WHITE);
    lv_obj_set_style_pad_all(header_right, 0, 0);
    lv_obj_set_style_pad_column(header_right, 2, 0);
    lv_obj_set_style_pad_row(header_right, 0, 0);
    lv_obj_set_flex_flow(header_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    disable_scrolling(header_right);

    s_wifi_status = cyberdeck_wifi_icon_create(header_right);
    lv_obj_set_width(
        s_wifi_status,
        static_cast<int32_t>((CYBERDECK_WIFI_ICON_RADIUS_2 +
                              CYBERDECK_WIFI_ICON_DEFAULT_THICKNESS) * 2.0f));
    lv_obj_set_flex_grow(s_wifi_status, 0);

    s_battery_status = lv_obj_create(header_right);
    lv_obj_set_size(s_battery_status, LV_SIZE_CONTENT, 42);
    lv_obj_set_flex_grow(s_battery_status, 0);
    style_base(s_battery_status, BLACK, WHITE);
    lv_obj_set_style_bg_opa(s_battery_status, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_battery_status, 0, 0);
    lv_obj_set_style_pad_left(s_battery_status, 4, 0);
    lv_obj_set_flex_flow(s_battery_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_battery_status, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    disable_scrolling(s_battery_status);

    s_battery_symbol = lv_label_create(s_battery_status);
    lv_label_set_text(s_battery_symbol, "");
    style_base(s_battery_symbol, BLACK, MUTED);
    s_battery_percentage = lv_label_create(s_battery_status);
    lv_label_set_text(s_battery_percentage, "");
    lv_obj_set_style_pad_column(s_battery_status, 3, 0);
    style_base(s_battery_percentage, BLACK, WHITE);
    lv_obj_add_flag(s_battery_status, LV_OBJ_FLAG_HIDDEN);

    lv_obj_update_layout(header);
    lv_obj_update_layout(header_right);
    cyberdeck_wifi_icon_update_layout(s_wifi_status,
                                       lv_obj_get_width(s_wifi_status));
    wifi_mgr_set_state_callback(on_wifi_state, nullptr);
    s_last_clock_text.clear();
    update_clock(nullptr);
      lv_timer_create(update_clock, 1000, nullptr);
      lv_timer_create(process_wifi_state, 100, nullptr);
       lv_timer_create(process_wifi_scan, 100, nullptr);
       lv_timer_create(process_wifi_audit, 100, nullptr);
     s_terminal = lv_textarea_create(s_menu); lv_obj_set_width(s_terminal, LV_PCT(100)); lv_obj_set_flex_grow(s_terminal, 1); style_base(s_terminal, SURFACE, WHITE); lv_obj_set_style_border_width(s_terminal, 1, 0); lv_obj_set_style_border_color(s_terminal, BORDER, 0); lv_obj_set_style_pad_all(s_terminal, 12, 0); lv_textarea_set_one_line(s_terminal, false); lv_textarea_set_max_length(s_terminal, TERMINAL_LIMIT); lv_obj_set_scroll_dir(s_terminal, LV_DIR_ALL); lv_obj_set_scroll_chain(s_terminal, false); lv_obj_set_scrollbar_mode(s_terminal, LV_SCROLLBAR_MODE_OFF); lv_obj_add_event_cb(s_terminal, focused, LV_EVENT_FOCUSED, nullptr); lv_obj_add_event_cb(s_terminal, terminal_insert, LV_EVENT_INSERT, nullptr); lv_obj_add_event_cb(s_terminal, terminal_changed, LV_EVENT_VALUE_CHANGED, nullptr); lv_obj_add_event_cb(s_terminal, terminal_key, LV_EVENT_KEY, nullptr);
    reset_ssh_output_filter();
    discard_ssh_line_composer();
    s_output = "CYBERDECK5 READY\n"; render_terminal();

     s_keyboard = lv_keyboard_create(s_screen); hidden(s_keyboard, true); lv_keyboard_set_textarea(s_keyboard, s_terminal); lv_obj_add_event_cb(s_keyboard, virtual_keyboard_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    disable_scrolling(s_keyboard);
    return ESP_OK;
}

extern "C" void cyberdeck_ui_deinit(void)
{
    cyberdeck_cat_worker_teardown();
    s_cat_worker_ready = false;
    destroy_ui_resource_handles();
}

extern "C" void cyberdeck_keyboard_input(const char *text, size_t length, uint8_t modifier, uint32_t special_key) {
     const size_t payload_length = text != nullptr ? length : 0;
     if (payload_length == 0 && special_key == 0) return;
     keyboard_event_context *event = static_cast<keyboard_event_context *>(
         malloc(sizeof(keyboard_event_context) + payload_length + 1));
     if (event == nullptr) return;
     event->length = payload_length;
     event->modifier = modifier;
     event->special_key = special_key;
     if (payload_length != 0) memcpy(event->text, text, payload_length);
     event->text[payload_length] = '\0';

     /* The queue is bounded and owns accepted snapshots.  Serialize the
      * short LVGL enqueue, not the eventual UI work. */
     if (s_keyboard_async_mutex == nullptr ||
         xSemaphoreTake(s_keyboard_async_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
         free(event);
         return;
     }
     if (s_keyboard_event_queue == nullptr ||
         xQueueSend(s_keyboard_event_queue, &event, 0) != pdTRUE) {
         xSemaphoreGive(s_keyboard_async_mutex);
         free(event);
         return;
     }
      const lv_result_t async_result = lv_async_call(process_keyboard_event_async, nullptr);
      if (async_result != LV_RESULT_OK) {
          discard_keyboard_event_from_queue(event);
          free(event);
      }
      xSemaphoreGive(s_keyboard_async_mutex);
}
