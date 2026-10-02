#!/usr/bin/env python3
"""Structural contract for Phase 7: the shell as a real foreground application.

The UI is not host-linkable, so this contract inspects the real sources to
protect the ownership boundaries the phase introduced:

  * `cyberdeck.shell` is registered as the real foreground application, not a
    service with stub hooks;
  * the supervisor owns the console lifecycle and the UI only lends its host;
  * the prompt and the command line are composed by the application, never
    rebuilt by the view;
  * the legacy command parser stays the single owner of legacy commands, so the
    supervisor cannot capture wifi/log/screen/bluetooth/ssh/help;
  * SSH is a session mode of the shell console, not a second console.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
APP_SRC = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_app.cpp"
APP_HDR = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_shell_app.h"
CONSOLE_SRC = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp"
CONSOLE_HDR = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_shell_console.h"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"
RUNTIME_SRC = ROOT / "components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
CMAKE = ROOT / "components/cyberdeck/CMakeLists.txt"
HELP = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h"


def function_body(source: str, signature: str) -> str:
    marker = 0
    while True:
        marker = source.find(signature, marker)
        if marker < 0:
            raise AssertionError(f"missing {signature}")
        opening = source.find("{", marker)
        semicolon = source.find(";", marker)
        if opening >= 0 and (semicolon < 0 or opening < semicolon):
            depth = 0
            for index in range(opening, len(source)):
                if source[index] == "{":
                    depth += 1
                elif source[index] == "}":
                    depth -= 1
                    if depth == 0:
                        return source[opening + 1:index]
            raise AssertionError(f"unclosed {signature}")
        marker += 1


def main() -> int:
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    apps = APPS.read_text(encoding="utf-8")
    app_src = APP_SRC.read_text(encoding="utf-8")
    app_hdr = APP_HDR.read_text(encoding="utf-8")
    console_src = CONSOLE_SRC.read_text(encoding="utf-8")
    console_hdr = CONSOLE_HDR.read_text(encoding="utf-8")
    session = SESSION.read_text(encoding="utf-8")
    runtime = RUNTIME_SRC.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")
    help_catalog = HELP.read_text(encoding="utf-8")

    # --- the shell is the real foreground application, not a stub ----------
    require("cyberdeck_shell_app::global_application()" in apps,
            "system apps must register the real foreground shell application")
    require("start_shell" not in apps and "stop_shell" not in apps,
            "the stub shell start/stop hooks must be gone")
    require("service_application s_shell" not in apps,
            "the shell must not be registered as a service application")
    require(re.search(r'&cyberdeck_shell_app::global_application\(\)', apps) is not None,
            "the shell application must be registered through the supervisor registry")

    manifest = re.search(
        r"constexpr cyberdeck_apps::manifest k_manifest\{(?P<body>.*?)\};", app_src, re.S)
    require(manifest is not None, "the shell application must declare a manifest")
    if manifest is not None:
        require("cyberdeck_apps::app_type::foreground" in manifest.group("body"),
                "the shell manifest must declare the foreground type")
        require('"cyberdeck.shell"' in manifest.group("body"),
                "the shell manifest must keep its id")
        require('"cyberdeck.event_log"' in manifest.group("body"),
                "the shell must declare its event log dependency")

    # --- the supervisor owns the console lifecycle -------------------------
    start = function_body(app_src, "bool application::start()")
    require("if (running_) return true;" in start,
            "the shell start hook must be idempotent")
    require("if (host_ == nullptr) return false;" in start,
            "the shell must fail closed without a composition host")
    require("new (std::nothrow) cyberdeck_shell_session::session" in start,
            "each start must create a fresh console so sessions stay isolated")
    require("running_ = true;" in start, "start must publish the running state")

    stop = function_body(app_src, "bool application::stop()")
    require("if (!running_) return true;" in stop, "the shell stop hook must be idempotent")
    require("clear_ble_auth_input()" in stop,
            "stopping the console must wipe the transient passkey buffer")
    require("console_.reset()" in stop, "stop must release the owned console")
    require("console_.reset();" in stop and "new (std::nothrow)" in start,
            "console ownership must be created on start and destroyed on stop")
    require("console_.reset" not in start,
            "start must build a new console instead of reinitializing a stale one")

    detach = function_body(app_src, "void application::detach_console()")
    require("(void)stop();" in detach,
            "detaching the composition host must stop the application")
    require("host_ = nullptr;" in detach,
            "detaching must clear the borrowed composition host")

    # --- the facade is null-safe ------------------------------------------
    for method, forbidden in (("void application::clear_editor()", "console_->clear_editor()"),
                              ("void application::execute_line(", "console_->execute_line("),
                              ("void application::handle_key(", "console_->handle_key(")):
        body = function_body(app_src, method)
        require("if (!console_) return;" in body,
                f"{method} must be a no-op without a console")
        require(forbidden in body, f"{method} must delegate to the owned console")

    require("bool application::insert_physical_text(" in app_src and
            "return console_ ? console_->insert_physical_text(text, length) : false;" in app_src,
            "input insertion must fail closed without a console")
    require("*virtual_enter_handled = false;" in app_src,
            "a dropped virtual chunk must never claim Enter")

    # --- the view does not own the console --------------------------------
    require("s_shell_app" in ui and "cyberdeck_shell_app::global_application()" in ui,
            "the UI must reach the console through the shell application")
    require("cyberdeck_shell_session::session s_shell_session" not in ui,
            "the UI must not construct the session it used to own")
    require("s_shell_app.attach_console(s_shell_session_host)" in ui,
            "the UI must lend its session host to the shell application")
    ui_init = function_body(ui, 'extern "C" esp_err_t cyberdeck_ui_init(void)')
    require(ui_init.index("attach_console(") < ui_init.index("keyboard_dispatch.start("),
            "the composition host must be attached before any input or prompt work")
    ui_deinit = function_body(ui, 'extern "C" void cyberdeck_ui_deinit(void)')
    require("destroy_ui_resource_handles()" in ui_deinit,
            "UI deinit must release the composition resources")
    handles = function_body(ui, "void destroy_ui_resource_handles()")
    require("s_shell_app.detach_console()" in handles,
            "UI teardown must detach the shell console so the passkey is wiped")

    # --- the prompt and the command line belong to the application --------
    require("cyberdeck_shell_console::line_view application::compose_line(" in app_src,
            "the application must compose the prompt and the line")
    require("s_shell_app.compose_line(surface)" in ui,
            "the view must delegate prompt composition to the application")
    for leaked in ("const std::string marker =", "const std::string fitted_line ="):
        require(leaked not in ui,
                f"the view must not rebuild prompt internals: {leaked}")

    # --- the supervisor is the only owner of `app` -------------------------
    resolve = function_body(console_src, "dispatch_target dispatcher::resolve(")
    require("token == k_supervisor_command" in resolve,
            "the `app` verb must be resolved to the supervisor")
    require(resolve.index("k_supervisor_command") < resolve.index("is_legacy_command"),
            "`app` must be claimed before the legacy reservation is consulted")
    require("is_legacy_command(token)" in resolve,
            "a legacy command must stay with the console")
    require(resolve.index("is_legacy_command(token)") < resolve.index("declares_command(token)"),
            "a legacy command must never be claimed by a declared application command")
    requires_dispatcher = function_body(console_src, "bool dispatcher::declares_command(")
    requires_manifest = "get_manifest()" in requires_dispatcher and "item.command" in requires_dispatcher
    require(requires_manifest,
            "a declared command must be read from the application manifest")

    execute = function_body(session, "void session::execute_line(")
    require("dispatcher.resolve(line) == cyberdeck_shell_console::dispatch_target::supervisor"
            in execute,
            "the session must consult the supervisor only for the tokens it owns")
    require(execute.index("dispatcher.resolve(line)") < execute.index("cyberdeck_parse_command("),
            "the dispatcher must run before the legacy parser")
    require("host_.app_runtime().execute_line(line)" not in execute,
            "the session must not hand every line to the runtime")
    for legacy in ("CYBERDECK_CMD_WIFI", "CYBERDECK_CMD_HELP", "CYBERDECK_CMD_SCREEN_ON"):
        require(legacy in execute,
                f"the legacy parser must keep owning {legacy}")
    require("local_shell().execute(line)" in execute,
            "the local shell must stay the first owner of local commands")

    # --- the reserved command list is derived from the help catalog --------
    require("cyberdeck_shell_help::kCatalog" in console_src,
            "the legacy reservation must be derived from the shared help catalog")
    require("kCatalog" in help_catalog,
            "the shared help catalog must remain the single command list")
    require("is_legacy_command" in console_hdr,
            "the reservation rule must be part of the console contract")
    catalog_commands = re.findall(r'\{"([a-z]+)",', help_catalog)
    require(len(catalog_commands) >= 17,
            "the help catalog must keep covering every shell and service command")

    # --- SSH is a session mode, not a second console -----------------------
    require("enum class session_mode" in console_hdr,
            "the console must declare explicit session modes")
    for mode in ("menu", "ssh_host_key", "ssh_password", "ssh_interactive"):
        require(mode in console_hdr, f"missing session mode {mode}")
    mode_fn = function_body(console_src, "session_mode mode_for(")
    for state in ("MENU", "PASSWORD", "HOST_KEY", "CONNECTED"):
        require(state in mode_fn,
                f"mode_for must map the SSH state {state}")
    require("session_mode application::mode() const" in app_src,
            "the application must expose the current session mode")
    app_mode = function_body(app_src, "cyberdeck_shell_console::session_mode application::mode() const")
    require("host_->ssh_phase()" in app_mode,
            "the session mode must be derived from the SSH port, not stored twice")
    require("render_terminal" not in app_src and "lv_" not in app_src,
            "the shell application must not touch the view or LVGL")

    # --- the scrollback budget cannot drift from the console budget --------
    require("constexpr size_t TERMINAL_LIMIT = cyberdeck_shell_console::k_terminal_limit;" in ui,
            "the scrollback budget must be derived from the console budget")
    require("static_assert(TERMINAL_LIMIT == cyberdeck_edit_line::limit," in ui,
            "the UI must assert the scrollback and line budgets agree")
    require("constexpr std::size_t k_terminal_limit = cyberdeck_edit_line::limit;" in console_hdr,
            "the console budget must have a single origin")
    require("lvgl" not in app_hdr and "esp_" not in app_src.replace("esp_err_t", ""),
            "the shell application must not depend on LVGL or ESP-IDF backends")

    # --- a foreground console cannot stop itself from its own console -------
    require('words[1] == "stop" && stops_console_owner(words[2])' in runtime,
        "the supervisor must refuse any stop that would cascade into the console")
    require("stop would remove the console" in runtime,
            "the refusal must explain that it protects the console")
    runtime_header = (ROOT / "components/cyberdeck/include/apps/runtime/"
                      "cyberdeck_app_runtime.h").read_text(encoding="utf-8")
    require("bool owns_console = false;" in runtime_header,
            "the manifest must declare console ownership explicitly")
    require("bool stops_console_owner(std::string_view id) const;" in runtime_header,
            "the supervisor must expose the cascade check")
    cascade = function_body(runtime, "bool runtime::cascade_stops_console(")
    require("owns_console" in cascade,
            "the cascade search must recognize the console owner itself")
    require("candidate.dependencies[dep] == id" in cascade,
            "the cascade search must follow the dependent tree, not the target alone")
    require("visited[index] = true;" in cascade,
            "the cascade search must stay bounded against dependency cycles")
    require(manifest is not None and
            manifest.group("body").rstrip().endswith("true") and
            "/* owns_console */" in app_src,
            "the shell manifest must claim console ownership")

    # --- build registration ------------------------------------------------
    for source in ("cyberdeck_shell_console.cpp", "cyberdeck_shell_app.cpp"):
        require(source in cmake, f"{source} must be registered in CMake")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: shell foreground application contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
