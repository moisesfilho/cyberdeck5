#!/usr/bin/env python3
"""RED structural contract for shell-driven screen protection.

The LVGL/BSP/NVS adapter is not host-linkable.  Its pure policy/state ABI is
exercised by test_screen_protection.cpp; this contract inspects the real
firmware sources for command routing, the shared help catalog row reached by
cyberdeck_help_text() delegation, persistence, timer/state integration, and
the transitive ui.type -> keyboard -> shell path.  It never opens hardware, a
simulator, Serial Automation Bridge, or a display.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
STATE_SOURCE = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp"
STATE_HEADER = ROOT / "components/cyberdeck/include/platform/display/cyberdeck_screen_protection.h"
STATE_CONTRACT = ROOT / "tests/host/keymap/contracts/cyberdeck_screen_protection.h"
SCREEN_SOURCE = ROOT / "components/cyberdeck/src/platform/display/screen_off.cpp"
SCREEN_HEADER = ROOT / "components/cyberdeck/include/platform/display/screen_off.h"
SHELL_SOURCE = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp"
SHELL_HEADER = ROOT / "components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h"
HELP_HEADER = ROOT / "components/cyberdeck/include/features/shell/cyberdeck_shell_help.h"
HELP_FIXTURE = ROOT / "tests/host/keymap/contracts/cyberdeck_help.h"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SERIAL = ROOT / "components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp"
APP = ROOT / "main/app_main.cpp"
COMPONENT = ROOT / "components/cyberdeck/CMakeLists.txt"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"
GITIGNORE = ROOT / ".gitignore"
SHELL_TEST = ROOT / "tests/host/keymap/test_shell_utils.cpp"
HELP_TEST = ROOT / "tests/host/keymap/test_help_unification.cpp"
SERIAL_TEST = ROOT / "tests/host/keymap/test_serial_ndjson_dispatch.cpp"

# The screen command is documented by the shared help catalog.  These are the
# exact approved strings: the usage literal carries the whole subcommand set
# and the 0-1440 range, and the rendered row is usage + " - " + description.
SCREEN_HELP_USAGE = "screen [on|off|timeout <0-1440>]"
SCREEN_HELP_DESCRIPTION = "control screen protection"
SCREEN_HELP_LINE = f"{SCREEN_HELP_USAGE} - {SCREEN_HELP_DESCRIPTION}"
SCREEN_CATALOG_ROW = re.compile(
    r'\{"screen"\s*,\s*"screen \[on\|off\|timeout <0-1440>\]"\s*,\s*'
    r'"control screen protection"\}'
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source: str, signature: str) -> str:
    """Return a function body, skipping declarations/forward declarations."""
    start = 0
    while True:
        marker = source.find(signature, start)
        require(marker >= 0, f"missing function containing {signature!r}")
        opening = source.find("{", marker)
        semicolon = source.find(";", marker)
        if semicolon >= 0 and semicolon < opening:
            start = semicolon + 1
            continue
        require(opening >= 0, f"missing body for {signature!r}")
        depth = 0
        for index in range(opening, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[opening + 1:index]
        raise AssertionError(f"unterminated function {signature!r}")


def switch_case(source: str, marker: str) -> str:
    start = source.find(marker)
    require(start >= 0, f"missing switch case {marker!r}")
    next_case = source.find("case ", start + len(marker))
    if next_case < 0:
        next_case = source.find("default:", start + len(marker))
    return source[start:next_case if next_case >= 0 else len(source)]


def require_before(body: str, first: str, second: str, message: str) -> None:
    require(first in body and second in body, message)
    require(body.index(first) < body.index(second), message)


def catalog_rows(help_header: str) -> list[str]:
    """Return the ordered catalog rows of the shared, host-linkable help header."""
    table = re.search(r"kCatalog\s*=\s*\{\{(.*?)\}\}\s*;", help_header, flags=re.S)
    require(table is not None,
            "shared help header must declare the ordered kCatalog table")
    return re.findall(r'\{"[^"]*"[^{}]*\}', table.group(1))


def makefile_prerequisites(makefile: str, target: str) -> str:
    """Return the prerequisite list declared for a Makefile target.

    Simple ``NAME := value`` definitions are expanded so the check does not
    depend on whether a prerequisite is spelled as a variable or literally.
    """
    match = re.search(rf"^{re.escape(target)}:[ \t]*(.*)$", makefile, flags=re.M)
    require(match is not None, f"Makefile must register the {target} target")
    definitions = dict(re.findall(r"^(\w+)[ \t]*:?=[ \t]*(\S+)[ \t]*$",
                                 makefile, flags=re.M))

    prerequisites = match.group(1)
    for _ in range(4):  # bounded expansion: a cycle must not loop forever
        expanded = re.sub(
            r"\$\((\w+)\)",
            lambda found: definitions.get(found.group(1), found.group(0)),
            prerequisites,
        )
        if expanded == prerequisites:
            break
        prerequisites = expanded
    return prerequisites


def check_pure_contract(state_source: str, state_header: str, component: str) -> None:
    combined = state_source + "\n" + state_header
    for token in (
        "default_timeout_minutes",
        "max_timeout_minutes",
        "parse_timeout_minutes",
        "persisted_timeout",
        "class state",
        "set_timeout_minutes",
        "persistence_view",
        "restore",
        "evaluate_inactivity",
    ):
        require(token in combined, f"pure screen contract must expose {token}")
    require(re.search(r"default_timeout_minutes\s*=\s*2\b", combined),
            "screen protection default must be exactly 2 minutes")
    require(re.search(r"max_timeout_minutes\s*=\s*1440\b", combined),
            "screen protection maximum must be exactly 1440 minutes")

    forbidden = (
        "lvgl.h", "bsp/esp-bsp.h", "nvs.h", "nvs_flash.h", "freertos",
        "xTaskCreate", "vTaskDelay", "screen_off_init",
    )
    for token in forbidden:
        require(token not in state_source,
                f"pure screen state must not depend on {token}")
    require("cyberdeck_screen_protection.cpp" in component,
            "component CMake must register the pure screen protection source")


def check_command_parser(shell: str, header: str) -> None:
    for token in (
        "CYBERDECK_CMD_SCREEN_ON",
        "CYBERDECK_CMD_SCREEN_OFF",
        "CYBERDECK_CMD_SCREEN_TIMEOUT",
    ):
        require(token in header, f"shell command enum must expose {token}")

    parser = function_body(shell, "cyberdeck_cmd_t cyberdeck_parse_command(")
    for literal in ('"screen on"', '"screen off"', '"screen timeout"'):
        require(literal in parser,
                f"command parser must recognize {literal}")
    require("CYBERDECK_CMD_SCREEN_ON" in parser and
            "CYBERDECK_CMD_SCREEN_OFF" in parser and
            "CYBERDECK_CMD_SCREEN_TIMEOUT" in parser,
            "command parser must route all screen commands")
    require("cmd.args" in parser,
            "screen timeout must forward its operand to the strict parser")

    help_text = function_body(shell, "std::string cyberdeck_help_text()")
    require("cyberdeck_shell_help::text()" in help_text,
            "help must render the shared catalog instead of a local literal list")

    test_source = strip_comments(SHELL_TEST.read_text(encoding="utf-8"))
    require("CYBERDECK_CMD_SCREEN_ON" in test_source and
            "CYBERDECK_CMD_SCREEN_OFF" in test_source and
            "CYBERDECK_CMD_SCREEN_TIMEOUT" in test_source,
            "host shell tests must cover all screen command routes")
    # The rendered help row is asserted by check_shared_help_catalog(), which
    # matches the whole row ("<usage> - <description>").  The previous check here
    # required the usage with a closing quote, a form no test source can contain.
    require(SCREEN_HELP_LINE in test_source,
            "host shell help contract must include the screen command")


def check_shared_help_catalog(help_header: str, help_fixture: str, shell: str,
                              shell_test: str, help_test: str) -> None:
    """The screen row must live in the one shared catalog, reached by delegation.

    The adapter used to inline the help rows.  It now delegates to
    cyberdeck_shell_help::text(), so the screen usage/range literal is only
    observable in the shared header, in the host fixture, and in the behavioral
    tests that compare production output byte for byte.  Requiring all four is
    what keeps the screen command documented after the catalog refactor.
    """
    rows = catalog_rows(help_header)
    screen_rows = [row for row in rows if re.match(r'\{"screen"', row)]
    require(len(screen_rows) == 1,
            "shared help catalog must carry exactly one screen row, "
            f"found {len(screen_rows)}")
    require(any(SCREEN_CATALOG_ROW.fullmatch(row) for row in screen_rows),
            f"shared help catalog must contain the approved screen row {screen_rows}")
    require(help_header.count(SCREEN_HELP_USAGE) == 1,
            "screen usage/range literal must appear once in the shared catalog")
    require(help_header.count(SCREEN_HELP_DESCRIPTION) == 1,
            "screen description must appear once in the shared catalog")

    # The catalog header is the single source: the adapter may only carry the
    # compile-time description guard, never its own renderable copy.
    require(SCREEN_HELP_USAGE not in shell,
            "shell_utils.cpp must not re-declare the screen help usage literal")
    require(shell.count(SCREEN_HELP_DESCRIPTION) == 1,
            "shell_utils.cpp must reference the screen description only in the "
            "compile-time catalog guard")
    require('#include "features/shell/cyberdeck_shell_help.h"' in shell,
            "shell_utils.cpp must include the shared help catalog header")
    help_text = function_body(shell, "std::string cyberdeck_help_text()")
    require("cyberdeck_shell_help::text()" in help_text,
            "cyberdeck_help_text() must delegate to cyberdeck_shell_help::text()")
    require(help_text.count("return") == 1,
            "cyberdeck_help_text() must only return the shared catalog text")

    # The host fixture is the behavioral oracle: the exact rendered row must be
    # present once, so the byte-for-byte comparison can detect any drift.
    require(help_fixture.count(SCREEN_HELP_LINE) == 1,
            "host help fixture must contain the approved screen row exactly once")
    require(help_fixture.count(SCREEN_HELP_USAGE) == 1,
            "screen usage/range literal must appear once in the host help fixture")

    # Both behavioral executables must compare production output against that
    # fixture; a fixture without them would not protect the rendered catalog.
    for source, label in ((shell_test, "test_shell_utils.cpp"),
                          (help_test, "test_help_unification.cpp")):
        require("kUnifiedHelpText" in source,
                f"{label} must compare against the shared help fixture")
        require("cyberdeck_help_text()" in source,
                f"{label} must exercise the production help entry point")
    require(SCREEN_HELP_LINE in shell_test,
            "test_shell_utils.cpp must assert the exact screen help row")


def check_ui_routing(ui: str) -> None:
    execute = function_body(ui, "void execute_line(")
    require("CYBERDECK_CMD_SCREEN_ON" in execute and
            "CYBERDECK_CMD_SCREEN_OFF" in execute and
            "CYBERDECK_CMD_SCREEN_TIMEOUT" in execute,
            "UI command switch must route all screen commands")

    on_case = switch_case(execute, "case CYBERDECK_CMD_SCREEN_ON:")
    require("screen_off_turn_on" in on_case,
            "screen on must call the real screen adapter")

    off_case = switch_case(execute, "case CYBERDECK_CMD_SCREEN_OFF:")
    require("screen_off_turn_off" in off_case,
            "screen off must call the real screen adapter")

    timeout_case = switch_case(execute, "case CYBERDECK_CMD_SCREEN_TIMEOUT:")
    require("parse_timeout_minutes" in timeout_case,
            "UI must validate timeout through the pure strict parser")
    require("screen_off_set_timeout_minutes" in timeout_case,
            "UI must apply a valid timeout through the screen adapter")
    require_before(timeout_case, "parse_timeout_minutes",
                   "screen_off_set_timeout_minutes",
                   "timeout must be parsed before it mutates screen state")
    require("append_line" in timeout_case,
            "timeout command must report validation/persistence outcome")
    require("nvs_" not in timeout_case,
            "UI command handling must not perform NVS I/O directly")


def check_adapter(screen: str, header: str, ui: str, app: str, component: str) -> None:
    for api in (
        "screen_off_init",
        "screen_off_turn_on",
        "screen_off_turn_off",
        "screen_off_set_timeout_minutes",
    ):
        require(api in header, f"screen public header must expose {api}")
        require(api in screen, f"screen adapter must define {api}")

    require("cyberdeck_screen_protection" in screen,
            "screen adapter must use the host-testable policy/state seam")
    require("evaluate_inactivity" in screen,
            "timer callback must use pure inactivity state evaluation")
    require("lv_display_get_inactive_time" in screen,
            "timer callback must consume LVGL inactivity time")

    timer_bodies = []
    for match in re.finditer(r"\bvoid\s+(\w+)\s*\([^)]*lv_timer_t\s*\*", screen):
        try:
            timer_bodies.append(function_body(screen, match.group(0)))
        except AssertionError:
            pass
    inactivity_timers = [body for body in timer_bodies
                         if "lv_display_get_inactive_time" in body]
    require(inactivity_timers,
            "screen adapter must retain an LVGL inactivity timer callback")
    timer = inactivity_timers[0]
    require("evaluate_inactivity" in timer,
            "timer must delegate the transition decision to pure state")
    require("INACTIVITY_TIMEOUT_MS" not in timer,
            "timer must not retain the obsolete hard-coded 120-second threshold")

    timer_create = re.search(r"lv_timer_create\s*\((.*?),\s*1000\s*,",
                             screen, flags=re.S)
    require(timer_create is not None,
            "screen inactivity timer must retain the one-second polling cadence")
    require("DOUBLE_TAP_WINDOW_MS" in screen and
            "LV_EVENT_CLICKED" in screen,
            "double-tap wake behavior must remain covered")

    set_timeout = function_body(screen, "screen_off_set_timeout_minutes(")
    require("set_timeout_minutes" in set_timeout,
            "screen timeout API must update the pure state before committing")
    require("persist" in set_timeout or "save" in set_timeout or
            "nvs_commit" in set_timeout,
            "screen timeout API must persist the accepted state")

    turn_on = function_body(screen, "screen_off_turn_on(")
    turn_off = function_body(screen, "screen_off_turn_off(")
    require("show_screen" in turn_on or "lv_display_trigger_activity" in turn_on,
            "manual screen on must restore the display/activity")
    require("hide_screen" in turn_off or "bsp_display_brightness_set" in turn_off,
            "manual screen off must use the same guarded display path")

    init = function_body(screen, "screen_off_init(")
    timer_position = init.find("lv_timer_create")
    require(timer_position >= 0, "screen init must create the inactivity timer")
    before_timer = init[:timer_position]
    require("load" in before_timer or "restore" in before_timer or "nvs_open" in before_timer,
            "NVS timeout restoration must complete before the timer starts")

    require(re.search(r"#include\s*[<\"](?:nvs|nvs_flash)\.h[>\"]", screen),
            "screen adapter must include the NVS API")
    for token, pattern in (
        ("open", r"\bnvs_open\s*\("),
        ("read", r"\bnvs_get_(?:u16|u32|blob)\s*\("),
        ("write", r"\bnvs_set_(?:u16|u32|blob)\s*\("),
        ("commit", r"\bnvs_commit\s*\("),
        ("close", r"\bnvs_close\s*\("),
    ):
        require(re.search(pattern, screen),
                f"screen NVS persistence must perform {token}")
    require("ESP_ERR_NVS_NOT_FOUND" in screen and
            "default_timeout_minutes" in screen,
            "missing/corrupt persisted timeout must fall back to the 2-minute default")
    require("persistence_view" in screen and "restore" in screen,
            "adapter must persist and restore both effective and last-positive values")
    require("nvs_" not in ui,
            "NVS I/O must remain inside the screen adapter, not the UI")

    nvs_init = app.find("nvs_flash_init")
    screen_init = app.find("screen_off_init")
    require(nvs_init >= 0 and screen_init >= 0 and nvs_init < screen_init,
            "NVS must be initialized before screen protection restores its timeout")
    require("nvs_flash" in component,
            "component must retain the ESP-IDF NVS dependency")


def check_serial_transitive_path(serial: str) -> None:
    require('"ui.type"' in serial, "serial bridge must retain ui.type")
    device = function_body(serial, "device_result device_exec(")
    require('req.type == "ui.type"' in device and "exec_ui_type" in device,
            "ui.type must dispatch to the text executor")

    executor = function_body(serial, "void exec_ui_type(")
    segmented = function_body(serial, "void inject_text_segmented(")
    text = function_body(serial, "void inject_text(")
    enter = function_body(serial, "void inject_enter()")
    require("inject_text_segmented" in executor and "inject_enter" in executor,
            "ui.type must inject text and submit with Enter transitively")
    require("inject_text(" in segmented,
            "segmented injection must delegate to the bounded text injector")
    require("cyberdeck_keyboard_input" in text,
            "text injection must reach cyberdeck_keyboard_input")
    require("cyberdeck_keyboard_input" in enter and "LV_KEY_ENTER" in enter,
            "ui.type must submit through the physical-keyboard Enter seam")
    require("screen_off_" not in strip_comments(serial),
            "screen commands must flow through ui.type, not a serial-only side channel")

    serial_test = strip_comments(SERIAL_TEST.read_text(encoding="utf-8"))
    for command in ("screen on", "screen off", "screen timeout 0",
                    "screen timeout 1440"):
        require(command in serial_test,
                f"ui.type host protocol test must carry {command!r}")


def check_wiring(contract: str) -> None:
    makefile = MAKEFILE.read_text(encoding="utf-8")
    codemap = strip_comments(CODEMAP.read_text(encoding="utf-8"))
    gitignore = GITIGNORE.read_text(encoding="utf-8")

    require("test_screen_protection:" in makefile and
            "test_screen_protection_contract:" in makefile,
            "Makefile must register behavioral and structural screen tests")
    require("cyberdeck_screen_protection.cpp" in makefile,
            "Makefile must link the real pure production source")
    require("test_screen_protection.cpp" in codemap and
            "test_screen_protection_contract.py" in codemap and
            "cyberdeck_screen_protection.cpp" in codemap,
            "code-map must document the screen source and both tests")
    require("!tests/host/keymap/test_screen_protection.cpp" in gitignore and
            "!tests/host/keymap/test_screen_protection_contract.py" in gitignore,
            "screen test sources must be trackable despite the host-test ignore rule")
    require("class state" in contract and "persisted_timeout" in contract,
            "test contract must retain the pure state/persistence ABI")

    # Every input this contract inspects must be a declared prerequisite, so a
    # stale catalog/fixture/test file cannot silently invalidate the checks.
    prerequisites = makefile_prerequisites(makefile, "test_screen_protection_contract")
    for token, message in (
        ("cyberdeck_shell_help.h",
         "screen contract must depend on the shared help catalog header"),
        ("contracts/cyberdeck_help.h",
         "screen contract must depend on the host help fixture"),
        ("test_shell_utils.cpp",
         "screen contract must depend on the behavioral shell test"),
        ("test_help_unification.cpp",
         "screen contract must depend on the behavioral help unification test"),
        ("test_serial_ndjson_dispatch.cpp",
         "screen contract must depend on the serial ui.type protocol test"),
    ):
        require(token in prerequisites, message)


def main() -> int:
    require(STATE_SOURCE.exists(),
            f"pure screen protection source is missing: {STATE_SOURCE}")
    require(STATE_HEADER.exists(),
            f"pure screen protection header is missing: {STATE_HEADER}")
    require(SCREEN_SOURCE.exists() and SCREEN_HEADER.exists(),
            "screen adapter source/header is missing")
    require(STATE_CONTRACT.exists(),
            f"host screen contract is missing: {STATE_CONTRACT}")

    state_source = strip_comments(STATE_SOURCE.read_text(encoding="utf-8"))
    state_header = strip_comments(STATE_HEADER.read_text(encoding="utf-8"))
    contract = STATE_CONTRACT.read_text(encoding="utf-8")
    screen = strip_comments(SCREEN_SOURCE.read_text(encoding="utf-8"))
    screen_header = strip_comments(SCREEN_HEADER.read_text(encoding="utf-8"))
    shell = strip_comments(SHELL_SOURCE.read_text(encoding="utf-8"))
    shell_header = strip_comments(SHELL_HEADER.read_text(encoding="utf-8"))
    help_header = strip_comments(HELP_HEADER.read_text(encoding="utf-8"))
    help_fixture = strip_comments(HELP_FIXTURE.read_text(encoding="utf-8"))
    shell_test = strip_comments(SHELL_TEST.read_text(encoding="utf-8"))
    help_test = strip_comments(HELP_TEST.read_text(encoding="utf-8"))
    ui = strip_comments(UI.read_text(encoding="utf-8"))
    serial = strip_comments(SERIAL.read_text(encoding="utf-8"))
    app = strip_comments(APP.read_text(encoding="utf-8"))
    component = strip_comments(COMPONENT.read_text(encoding="utf-8"))

    require(HELP_HEADER.exists() and HELP_FIXTURE.exists() and HELP_TEST.exists(),
            "shared help catalog, host fixture, or behavioral help test is missing")

    check_pure_contract(state_source, state_header, component)
    check_command_parser(shell, shell_header)
    check_shared_help_catalog(help_header, help_fixture, shell, shell_test, help_test)
    check_ui_routing(ui)
    check_adapter(screen, screen_header, ui, app, component)
    check_serial_transitive_path(serial)
    check_wiring(contract)
    print("PASS: screen protection structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
