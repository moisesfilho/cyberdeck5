#include "apps/shell/cyberdeck_shell_app.h"

#include <new>

namespace cyberdeck_shell_app {
namespace {

constexpr cyberdeck_apps::manifest k_manifest{
    "cyberdeck.shell", "Terminal shell", "1.0.0", "Primary foreground terminal application",
    {},                                                            /* command */
    {"cyberdeck.event_log"}, 1,                                   /* dependencies */
    {"display", "input", "storage"}, 3,                           /* resources */
    1000,                                                          /* lifecycle */
    "1",                                                           /* api_version */
    cyberdeck_apps::app_type::foreground,
    {"display", "input", "storage", "shell"}, 4,                  /* capabilities */
    8192,                                                          /* stack_bytes */
    8,                                                             /* queue_depth */
    {}, 0,                                                         /* commands */
    true};                                                         /* owns_console */

} // namespace

const cyberdeck_apps::manifest &application::get_manifest() const { return k_manifest; }

bool application::init() { return true; }

bool application::start()
{
    if (running_) return true;
    /* Fail closed: without a composition host there is no console to own. */
    if (host_ == nullptr) return false;
    /* A fresh session object per start is what keeps history, editing and the
     * Wi-Fi/BLE connection tokens isolated across cycles. */
    std::unique_ptr<cyberdeck_shell_session::session> created(
        new (std::nothrow) cyberdeck_shell_session::session(*host_));
    if (!created) return false;
    console_ = std::move(created);
    running_ = true;
    /* Paint the prompt through the composition so the first frame already
     * belongs to a running shell application. */
    host_->render();
    return true;
}

bool application::stop()
{
    if (!running_) return true;
    if (console_) {
        /* The transient passkey buffer must never outlive the console. */
        console_->clear_ble_auth_input();
        console_.reset();
    }
    running_ = false;
    return true;
}

bool application::teardown() { return stop(); }

bool application::running() const { return running_; }

cyberdeck_apps::result application::execute(std::string_view, std::string_view) { return {}; }

void application::attach_console(cyberdeck_shell_session::host &host)
{
    host_ = &host;
    /* An attach after stop() leaves the application stopped: the supervisor
     * owns the cycle, so a new console is only created by the next start(). */
}

void application::detach_console()
{
    (void)stop();
    host_ = nullptr;
}

cyberdeck_shell_session::session *application::console() { return console_.get(); }

bool application::console_ready() const { return console_ != nullptr; }

cyberdeck_shell_console::session_mode application::mode() const
{
    if (host_ == nullptr || !console_) return cyberdeck_shell_console::session_mode::menu;
    return cyberdeck_shell_console::mode_for(host_->ssh_phase());
}



void application::execute_line(bool line_already_sent)
{
    if (!console_) return;
    console_->execute_line(line_already_sent);
}

void application::handle_key(cyberdeck_shell_session::key pressed)
{
    if (!console_) return;
    console_->handle_key(pressed);
}

void application::sync_editor()
{
    if (!console_) return;
    console_->sync_editor();
}

void application::sync_line()
{
    if (!console_) return;
    console_->sync_line();
}

void application::clear_editor()
{
    if (!console_) return;
    console_->clear_editor();
}

void application::clear_ble_auth_input()
{
    if (!console_) return;
    console_->clear_ble_auth_input();
}

std::size_t application::ble_auth_input_size() const
{
    return console_ ? console_->ble_auth_input().size() : 0;
}

bool application::insert_physical_text(const char *text, std::size_t length)
{
    return console_ ? console_->insert_physical_text(text, length) : false;
}

bool application::insert_modified_key(char character, std::uint8_t modifier)
{
    return console_ ? console_->insert_modified_key(character, modifier) : false;
}

bool application::insert_virtual_text(const char *text, bool *virtual_enter_handled)
{
    if (virtual_enter_handled != nullptr) *virtual_enter_handled = false;
    return console_ ? console_->insert_virtual_text(text, virtual_enter_handled) : false;
}

std::uint64_t application::wifi_connection_token() const
{
    return console_ ? console_->wifi_connection_token() : 0;
}

std::uint64_t application::wifi_model_connection_token() const
{
    return console_ ? console_->wifi_model_connection_token() : 0;
}

void application::invalidate_wifi_connection()
{
    if (!console_) return;
    console_->invalidate_wifi_connection();
}

cyberdeck_shell_console::line_input application::current_input() const
{
    cyberdeck_shell_console::line_input input;
    if (!console_) return input;
    input.line = console_->line();
    input.visible_line = console_->editor().visible_line();
    input.cursor_bytes = console_->cursor();
    return input;
}

cyberdeck_shell_console::line_view application::compose_line(
    const cyberdeck_shell_console::surface_state &state)
{
    /* The editor session state follows the SSH mode before the visible line is
     * read, otherwise a stale editor mask could leak into the prompt. */
    sync_editor();
    return cyberdeck_shell_console::compose(state, current_input());
}

application &global_application()
{
    static application instance;
    return instance;
}

} // namespace cyberdeck_shell_app