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
import fnmatch
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
STATE_SOURCE = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp"
STATE_HEADER = ROOT / "components/cyberdeck/include/platform/display/cyberdeck_screen_protection.h"
STATE_CONTRACT = ROOT / "tests/host/keymap/contracts/cyberdeck_screen_protection.h"
SCREEN_SOURCE = ROOT / "components/cyberdeck/src/platform/display/screen_off.cpp"
SCREEN_HEADER = ROOT / "components/cyberdeck/include/platform/display/screen_off.h"
SHELL_SOURCE = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp"
SHELL_HEADER = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h"
HELP_HEADER = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h"
HELP_FIXTURE = ROOT / "tests/host/keymap/contracts/cyberdeck_help.h"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"
SERIAL = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"
APP = ROOT / "main/app_main.cpp"
COMPONENT = ROOT / "components/cyberdeck/CMakeLists.txt"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"
SHELL_TEST = ROOT / "tests/host/keymap/test_shell_utils.cpp"
HELP_TEST = ROOT / "tests/host/keymap/test_help_unification.cpp"
SERIAL_TEST = ROOT / "tests/host/keymap/test_serial_ndjson_dispatch.cpp"
SCREEN_TEST_FILE = ROOT / "tests/host/keymap/test_screen_protection.cpp"

# The screen command is documented by the shared help catalog.  These are the
# exact approved strings: the usage literal carries the whole subcommand set
# and the 0-1440 range, and the rendered row is usage + " - " + description.
SCREEN_HELP_USAGE = "screen [on|off|timeout <0-1440>]"
SCREEN_HELP_DESCRIPTION = "control screen protection"
SCREEN_HELP_LINE = f"{SCREEN_HELP_USAGE} - {SCREEN_HELP_DESCRIPTION}"
SCREEN_CATALOG_ROW = re.compile(
    r'\{"screen"\s*,\s*"screen \[on\|off\|timeout <0-1440>\]"\s*,\s*'
    r'"control screen protection"\s*,\s*'
    r'" {2}on\|off: enable or disable screen protection\\n'
    r' {2}timeout <0-1440>: set inactivity timeout in minutes; 0 disables it\\n"\}'
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
    require('#include "apps/shell/cyberdeck_shell_help.h"' in shell,
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


def check_ui_routing(ui: str, session: str) -> None:
    # The command switch now lives in the extracted session, reached through
    # the foreground shell application that owns the console.
    execute = function_body(session, "void session::execute_line(")
    require("s_shell_app.handle_key(" in ui,
            "UI must route line submission through the shell application")
    require("CYBERDECK_CMD_SCREEN_ON" in execute and
            "CYBERDECK_CMD_SCREEN_OFF" in execute and
            "CYBERDECK_CMD_SCREEN_TIMEOUT" in execute,
            "session command switch must route all screen commands")

    # The session reaches the screen through the host port; the UI host
    # implementation is the single place that calls the real adapter.
    on_case = switch_case(execute, "case CYBERDECK_CMD_SCREEN_ON:")
    require("host_.screen_turn_on()" in on_case,
            "screen on must go through the screen port")

    off_case = switch_case(execute, "case CYBERDECK_CMD_SCREEN_OFF:")
    require("host_.screen_turn_off()" in off_case,
            "screen off must go through the screen port")

    timeout_case = switch_case(execute, "case CYBERDECK_CMD_SCREEN_TIMEOUT:")
    require("parse_timeout_minutes" in timeout_case,
            "UI must validate timeout through the pure strict parser")
    require("host_.screen_set_timeout_minutes(" in timeout_case,
            "a valid timeout must go through the screen port")
    require_before(timeout_case, "parse_timeout_minutes",
                   "host_.screen_set_timeout_minutes",
                   "timeout must be parsed before it mutates screen state")
    require("append_output_line" in timeout_case,
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
    require("screen_off_turn_off" not in strip_comments(serial) and
            "screen_off_set_timeout_minutes" not in strip_comments(serial),
            "screen state-changing commands must flow through ui.type, not a serial-only side channel")
    # A ponte ganhou screen.status/screen.wake (saida explicita da protecao para
    # validacao remota); ela so pode usar o seam de producao para ler o estado e
    # religar, nunca reimplementar a protecao. Ver test_serial_screen_guard_contract.py.
    require("screen_off_is_active()" in strip_comments(serial) and
            "screen_off_turn_on()" in strip_comments(serial),
            "screen.status/screen.wake must use the production screen_off adapter")

    serial_test = strip_comments(SERIAL_TEST.read_text(encoding="utf-8"))
    for command in ("screen on", "screen off", "screen timeout 0",
                    "screen timeout 1440"):
        require(command in serial_test,
                f"ui.type host protocol test must carry {command!r}")


def check_wiring(contract: str) -> None:
    makefile = MAKEFILE.read_text(encoding="utf-8")
    codemap = strip_comments(CODEMAP.read_text(encoding="utf-8"))

    require("test_screen_protection:" in makefile and
            "test_screen_protection_contract:" in makefile,
            "Makefile must register behavioral and structural screen tests")
    require("cyberdeck_screen_protection.cpp" in makefile,
            "Makefile must link the real pure production source")
    require("test_screen_protection.cpp" in codemap and
            "test_screen_protection_contract.py" in codemap and
            "cyberdeck_screen_protection.cpp" in codemap,
            "code-map must document the screen source and both tests")
    # Assert the real ignore behavior; --no-index is required because an
    # already-tracked file is never reported as ignored.
    for source in ("test_screen_protection.cpp", "test_screen_protection_contract.py"):
        visible = subprocess.run(
            ["git", "check-ignore", "-q", "--no-index", f"tests/host/keymap/{source}"],
            cwd=str(ROOT), capture_output=True).returncode
        require(visible != 0,
                f"screen test sources must be trackable despite the host-test ignore rule: {source}")
    for artifact in ("test_screen_protection", "test_screen_protection_contract"):
        ignored = subprocess.run(
            ["git", "check-ignore", "-q", "--no-index", f"tests/host/keymap/{artifact}"],
            cwd=str(ROOT), capture_output=True).returncode
        require(ignored == 0,
                f"compiled host-test artifacts must stay ignored: {artifact}")
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


# ---------------------------------------------------------------------------
# REQ-MIG-59 (pilot): incremental build form of the screen-protection target.
#
# The pilot target must adopt the shape already approved and shipped for the
# migrated BLE targets (test_ble_types/test_ble_store): one real binary
# artifact named <target>_bin that depends on per-TU objects, each object
# compiled with the original flags/includes plus a generated depfile, the
# generated depfiles included by the Makefile, "Makefile" kept as a
# prerequisite of every recipe (so recipe/flag changes force a rebuild), and a
# .PHONY wrapper that executes the binary exactly once.
#
# The checks below are structural: they read the Makefile, they never build
# anything, and they never open hardware, a simulator or a display.
# ---------------------------------------------------------------------------

SCREEN_PROTECTION_TARGET = "test_screen_protection"
SCREEN_PROTECTION_BIN = "test_screen_protection_bin"
SCREEN_PROTECTION_TEST_SOURCE = "test_screen_protection.cpp"
SCREEN_PROTECTION_PRODUCTION_SOURCE = "cyberdeck_screen_protection.cpp"

# The pre-migration monolithic recipe was
#   $(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include \
#       $(SCREEN_PROTECTION_TEST) $(SCREEN_PROTECTION_SRC) -o test_screen_protection_bin
# so every per-TU compile must keep exactly these flags and include paths.
EXPECTED_COMPILE_FLAGS = ("$(CXXFLAGS)",)
EXPECTED_COMPILE_INCLUDES = ("-Icontracts", "-I../../../components/cyberdeck/include")

# The already-migrated target used as the in-repo precedent of the approved
# form: the same checker must accept it, otherwise the pilot requirement would
# describe a shape the repository never shipped.
BLE_TYPES_TARGET = "test_ble_types"
BLE_TYPES_BIN = "test_ble_types_bin"
BLE_TYPES_TEST_SOURCE = "test_ble_types.cpp"
BLE_TYPES_PRODUCTION_SOURCE = "cyberdeck_ble_types.cpp"

_MAKE_ASSIGNMENT = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(:=|::=|=|\?=|\+=)\s*(.*)$")
_MAKE_RULE_HEADER = re.compile(r"^([^:#=]+?):(?!=)\s*(.*)$")


class _MakeRule:
    """One rule of the host Makefile: targets, prerequisites and recipe."""

    def __init__(self, targets, prerequisites):
        self.targets = targets
        self.prerequisites = prerequisites
        self.recipe: list[str] = []

    def recipe_text(self) -> str:
        return "\n".join(self.recipe)


class _MakeModel:
    """Minimal reader for the GNU make subset used by the host Makefile.

    It models variable assignments, rule headers with prerequisites and
    tab-indented recipes, plus the $(VAR)/$(func ...) expansion needed to
    resolve object lists.  An unsupported function fails closed instead of
    silently expanding to an empty string, so an unknown recipe shape is
    reported instead of being accepted by accident.
    """

    def __init__(self, text: str):
        self.variables: dict[str, str] = {}
        self.rules: list[_MakeRule] = []
        self.phony_rules: list[_MakeRule] = []
        self.phony_targets: set[str] = set()
        self._parse(text)

    # -- parsing ---------------------------------------------------------
    @staticmethod
    def _logical_lines(text: str) -> list[str]:
        joined: list[str] = []
        buffer = ""
        for raw in text.split("\n"):
            line = raw.rstrip("\r")
            buffer = f"{buffer} {line.strip()}" if buffer else line
            if buffer.endswith("\\"):
                buffer = buffer[:-1].rstrip()
                continue
            joined.append(buffer)
            buffer = ""
        if buffer:
            joined.append(buffer)
        return joined

    @staticmethod
    def _strip_comment(line: str) -> str:
        index = line.find("#")
        return line if index < 0 else line[:index]

    def _parse(self, text: str) -> None:
        current: _MakeRule | None = None
        for raw in self._logical_lines(text):
            if raw.startswith("\t"):
                if current is not None:
                    current.recipe.append(raw)
                continue
            line = self._strip_comment(raw).rstrip()
            if not line.strip():
                continue
            assignment = _MAKE_ASSIGNMENT.match(line)
            if assignment:
                self._assign(assignment.group(1), assignment.group(2), assignment.group(3))
                current = None
                continue
            header = _MAKE_RULE_HEADER.match(line)
            if header:
                current = _MakeRule(header.group(1).split(), header.group(2).split())
                self.rules.append(current)
                continue
            current = None
        for rule in self.rules:
            expanded = [self.expand(name) for name in rule.targets]
            if ".PHONY" in expanded:
                # ".PHONY: a b c" declares a, b and c as phony targets.
                self.phony_rules.append(rule)
                self.phony_targets.update(
                    self.expand(" ".join(rule.prerequisites)).split())

    def _assign(self, name: str, operator: str, value: str) -> None:
        if operator == "+=":
            self.variables[name] = f"{self.variables.get(name, '')} {value}".strip()
        elif operator == "?=":
            self.variables.setdefault(name, value)
        elif operator in (":=", "::="):
            # Simple expansion: the value is resolved now, so a self reference
            # such as BINS := $(filter-out ...,$(BINS)) keeps the previous
            # value instead of expanding to an empty string.
            self.variables[name] = self.expand(value)
        else:
            self.variables[name] = value

    # -- expansion -------------------------------------------------------
    def expand(self, text: str, stack: tuple[str, ...] = ()) -> str:
        output: list[str] = []
        index = 0
        while index < len(text):
            char = text[index]
            if char == "$" and index + 1 < len(text) and text[index + 1] in "({":
                opener = text[index + 1]
                closer = ")" if opener == "(" else "}"
                depth = 1
                cursor = index + 2
                while cursor < len(text) and depth:
                    if text[cursor] == opener:
                        depth += 1
                    elif text[cursor] == closer:
                        depth -= 1
                    cursor += 1
                if depth:
                    output.append(text[index:])
                    break
                output.append(self._reference(text[index + 2:cursor - 1], stack))
                index = cursor
                continue
            output.append(char)
            index += 1
        return "".join(output)

    def _reference(self, inner: str, stack: tuple[str, ...]) -> str:
        inner = inner.strip()
        if re.fullmatch(r"[A-Za-z0-9_.]+", inner):
            return self._variable(inner, stack)
        call = re.match(r"^([A-Za-z0-9_-]+)\s+(.*)$", inner, flags=re.S)
        if call:
            return self._call(call.group(1), call.group(2), stack)
        return f"$({inner})"

    def _variable(self, name: str, stack: tuple[str, ...]) -> str:
        if name in stack:
            return ""
        value = self.variables.get(name)
        if value is None:
            return ""
        return self.expand(value, stack + (name,))

    def _call(self, function: str, arguments: str, stack: tuple[str, ...]) -> str:
        function = function.lower()
        if function in ("addsuffix", "addprefix", "filter", "filter-out"):
            head, _, tail = arguments.partition(",")
            words = self.expand(tail, stack).split()
            if function == "addsuffix":
                suffix = self.expand(head, stack)
                return " ".join(word + suffix for word in words)
            if function == "addprefix":
                prefix = self.expand(head, stack)
                return " ".join(prefix + word for word in words)
            patterns = head.split()
            matched = lambda word: any(fnmatch.fnmatchcase(word, p) for p in patterns)
            if function == "filter":
                return " ".join(word for word in words if matched(word))
            return " ".join(word for word in words if not matched(word))
        if function == "sort":
            return " ".join(sorted({self.expand(word, stack) for word in arguments.split()}))
        if function == "firstword":
            _, _, tail = arguments.partition(",")
            words = tail.split()
            return words[0] if words else ""
        if function in ("word", "words"):
            head, _, tail = arguments.partition(",")
            try:
                position = int(self.expand(head, stack))
            except ValueError:
                return ""
            words = [self.expand(word, stack) for word in tail.split()]
            if function == "word":
                return words[position - 1] if 0 < position <= len(words) else ""
            return " ".join(words[position - 1:]) if position > 0 else ""
        if function == "strip":
            return " ".join(self.expand(arguments, stack).split())
        if function == "subst":
            parts = arguments.split(",", 2)
            if len(parts) < 3:
                return ""
            old, new, subject = (self.expand(part, stack) for part in parts)
            return subject.replace(old, new)
        raise AssertionError(
            f"unsupported make function {function!r}: the structural check must "
            f"fail closed instead of expanding it to an empty value")

    # -- queries ---------------------------------------------------------
    def rules_for(self, target: str) -> list[_MakeRule]:
        found = []
        for rule in self.rules:
            if rule in self.phony_rules:
                continue
            if target in [self.expand(name) for name in rule.targets]:
                found.append(rule)
        return found


def _compiled_source(model: _MakeModel, rule: _MakeRule) -> str | None:
    """Return the single translation unit compiled by an object rule."""
    declared = model.expand(" ".join(rule.prerequisites)).split()
    sources = [item for item in declared if item.endswith((".cpp", ".c"))]
    if len(sources) == 1:
        return sources[0]
    spelled: list[str] = []
    for token in model.expand(rule.recipe_text()).split():
        if token.endswith((".cpp", ".c")) and token not in spelled:
            spelled.append(token)
    return spelled[0] if len(spelled) == 1 else None


def _object_for(compiled: dict[str, str], source_name: str) -> str:
    for source, obj in compiled.items():
        if os.path.basename(source) == source_name:
            return obj
    raise AssertionError(f"no per-TU object compiles {source_name}")


def require_incremental_form(makefile_text: str, target: str, binary: str,
                             test_source: str, production_source: str,
                             label: str) -> None:
    """Require the approved incremental build form of one host test target."""
    model = _MakeModel(makefile_text)

    # 1. one real, unique binary artifact.
    bin_rules = model.rules_for(binary)
    require(len(bin_rules) == 1,
            f"{label}: exactly one rule must produce {binary}, found {len(bin_rules)}")
    bin_rule = bin_rules[0]
    require(binary.endswith("_bin"),
            f"{label}: the artifact must be named <target>_bin")
    for rule in model.rules:
        if rule in model.phony_rules:
            continue
        for name in rule.targets:
            if model.expand(name) != target:
                continue
            require(not re.search(r"(?:^|\s)-o(?:\s|$)", rule.recipe_text()),
                    f"{label}: {target} must stay a run wrapper, it must not link "
                    f"an artifact of its own")
    bins = model.expand(model.variables.get("BINS", "")).split()
    require(binary in bins,
            f"{label}: BINS must list {binary} so the aggregate builds it and "
            f"clean removes it")

    # 2. the binary depends on per-TU objects, not on the sources themselves.
    prerequisites = model.expand(" ".join(bin_rule.prerequisites)).split()
    objects = [item for item in prerequisites if item.endswith(".o")]
    sources = [item for item in prerequisites if item.endswith((".cpp", ".c"))]
    require(objects,
            f"{label}: {binary} must depend on per-TU objects (approved "
            f"incremental form), found only {prerequisites}")
    require(not sources,
            f"{label}: {binary} must depend on per-TU objects instead of "
            f"sources, found {sources}")

    # 3. every object has its own compile rule with the original flags and
    #    includes, a generated depfile and the Makefile prerequisite.
    compiled: dict[str, str] = {}
    cxxflags = model.expand(model.variables.get("CXXFLAGS", ""))
    for obj in objects:
        obj_rules = model.rules_for(obj)
        require(len(obj_rules) == 1,
                f"{label}: object {obj} must have exactly one compile rule, "
                f"found {len(obj_rules)}")
        rule = obj_rules[0]
        recipe = rule.recipe_text()
        require(re.search(r"(?:^|\s)-c(?:\s|$)", recipe),
                f"{label}: object {obj} must be compiled with -c")
        source = _compiled_source(model, rule)
        require(source,
                f"{label}: object {obj} must compile exactly one translation unit")
        require(source not in compiled,
                f"{label}: {source} must be compiled exactly once, it is also "
                f"compiled by {compiled.get(source)}")
        compiled[source] = obj
        require("$(CXXFLAGS)" in recipe or (cxxflags and cxxflags in recipe),
                f"{label}: object {obj} must keep the original compile flag "
                f"$(CXXFLAGS)")
        for include in EXPECTED_COMPILE_INCLUDES:
            require(include in recipe,
                    f"{label}: object {obj} must keep the original include path "
                    f"{include}")
        require("-MMD -MP" in recipe,
                f"{label}: object {obj} must generate its depfile with -MMD -MP")
        require(re.search(r"-MF\s+\S*\.d\b", recipe),
                f"{label}: object {obj} must write its depfile with -MF <object>.d")
        require("Makefile" in rule.prerequisites,
                f"{label}: object {obj} must keep the Makefile prerequisite so "
                f"recipe/flag changes rebuild it")

    # 4. the generated depfiles are included by the Makefile.
    include_lines = [model.expand(line.strip())
                     for line in makefile_text.splitlines()
                     if line.strip().startswith("-include")]
    require(include_lines,
            f"{label}: the Makefile must include the generated depfiles with -include")
    included = " ".join(include_lines).split()
    for obj in objects:
        require(f"{obj}.d" in included,
                f"{label}: the depfile include must list {obj}.d")

    # 5. the link rule keeps the Makefile prerequisite.
    require("Makefile" in bin_rule.prerequisites,
            f"{label}: {binary} must keep the Makefile prerequisite so "
            f"recipe/flag changes relink it")

    # 6. link order: test object before production object.
    test_object = _object_for(compiled, test_source)
    production_object = _object_for(compiled, production_source)
    link = model.expand(bin_rule.recipe_text())
    require(test_object in link,
            f"{label}: the link recipe must use the test object {test_object}")
    require(production_object in link,
            f"{label}: the link recipe must use the production object "
            f"{production_object}")
    require(link.index(test_object) < link.index(production_object),
            f"{label}: the link order must be test then production "
            f"({test_object} before {production_object})")

    # 7. the .PHONY wrapper executes the binary exactly once.
    require(target in model.phony_targets,
            f"{label}: {target} must be declared as a .PHONY wrapper target")
    wrapper_rules = model.rules_for(target)
    require(len(wrapper_rules) == 1,
            f"{label}: {target} must have exactly one wrapper rule, found "
            f"{len(wrapper_rules)}")
    wrapper = wrapper_rules[0]
    wrapper_prerequisites = model.expand(" ".join(wrapper.prerequisites)).split()
    require(binary in wrapper_prerequisites,
            f"{label}: the .PHONY wrapper must depend on {binary}")
    wrapper_recipe = model.expand(wrapper.recipe_text())
    executions = len(re.findall(rf"\b{re.escape(binary)}\b", wrapper_recipe))
    require(executions == 1,
            f"{label}: the .PHONY wrapper must execute {binary} exactly once, "
            f"found {executions}")
    require(not re.search(r"(?:^|\s)-o(?:\s|$)", wrapper_recipe),
            f"{label}: the .PHONY wrapper must not build artifacts")

    # 8. the aggregate reaches the scenario exactly once.
    aggregate_rules = model.rules_for("test")
    require(len(aggregate_rules) == 1,
            f"{label}: the aggregate target test must exist exactly once")
    aggregate_raw = aggregate_rules[0].recipe_text()
    aggregate_recipe = model.expand(aggregate_raw)
    direct = len(re.findall(rf"\b{re.escape(binary)}\b", aggregate_recipe))
    # $(MAKE) is a built-in: it is only spelled out in the raw recipe.
    wrapper_calls = len(re.findall(
        rf"\$\(MAKE\)[^\n]*\b{re.escape(target)}\b", aggregate_raw))
    require(direct + wrapper_calls == 1,
            f"{label}: the aggregate must reach the {target} scenario exactly "
            f"once, found {direct} direct and {wrapper_calls} wrapper executions")

    # 9. clean removes the artifact and the per-TU object directory.
    clean_rules = model.rules_for("clean")
    require(len(clean_rules) == 1,
            f"{label}: the clean target must exist exactly once")
    clean_rule = clean_rules[0]
    clean_words = model.expand(clean_rule.recipe_text()).split()
    require(binary in clean_words, f"{label}: clean must remove {binary}")
    legacy = model.expand(
        model.variables.get("LEGACY_TEST_ARTIFACTS", "")).split()
    for directory in sorted({os.path.dirname(obj) for obj in objects}):
        require(directory,
                f"{label}: the per-TU objects must live in a dedicated directory")
        removed: list[str] = []
        directory_index = -1
        for index, line in enumerate(clean_rule.recipe):
            if not line.strip().startswith("rm -rf"):
                continue
            words = model.expand(line).split()
            if directory_index < 0 and directory in words:
                directory_index = index
            removed.extend(words)
        require(directory in removed,
                f"{label}: clean must remove the per-TU object directory {directory}")
        if directory in legacy:
            # rm -f fails on an existing directory, so the object directory must
            # be removed before the legacy artifact list is processed.
            legacy_index = next(
                (index for index, line in enumerate(clean_rule.recipe)
                 if "LEGACY_TEST_ARTIFACTS" in line), -1)
            require(legacy_index < 0 or directory_index < legacy_index,
                    f"{label}: clean must remove the per-TU object directory "
                    f"{directory} before the legacy artifact list, otherwise "
                    f"rm -f fails on the directory")


def check_incremental_precedent(makefile_text: str) -> None:
    """The same checker must accept the already-migrated BLE target.

    This keeps the pilot requirement anchored to a shape the repository
    already ships, instead of a shape invented by this contract.
    """
    require_incremental_form(makefile_text, BLE_TYPES_TARGET, BLE_TYPES_BIN,
                              BLE_TYPES_TEST_SOURCE, BLE_TYPES_PRODUCTION_SOURCE,
                              "host Makefile (migrated precedent)")


def check_screen_protection_incremental(makefile_text: str) -> None:
    """The pilot target must already carry the approved incremental form.

    This is the RED structural contract for REQ-MIG-59: it stays red until the
    Makefile migration of the pilot lands, and it fails only on the missing
    incremental structure, never on an unrelated regression.
    """
    try:
        require_incremental_form(makefile_text, SCREEN_PROTECTION_TARGET,
                                  SCREEN_PROTECTION_BIN,
                                  SCREEN_PROTECTION_TEST_SOURCE,
                                  SCREEN_PROTECTION_PRODUCTION_SOURCE,
                                  "host Makefile (screen-protection pilot)")
    except AssertionError as error:
        raise AssertionError(
            f"{error} (REQ-MIG-59 pilot: expected RED until the incremental "
            f"{SCREEN_PROTECTION_TARGET} migration lands)") from None


# Approved form of the pilot target, mirroring the shipped test_ble_types
# recipe.  It is the baseline of the mutation tests below: every mutation
# removes exactly one element of this form and the checker must reject it.
APPROVED_SCREEN_PROTECTION_MAKEFILE = "\n".join([
    "CXX      ?= g++",
    "CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2",
    "",
    "SCREEN_PROTECTION_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp",
    "SCREEN_PROTECTION_HDR := ../../../components/cyberdeck/include/platform/display/cyberdeck_screen_protection.h",
    "SCREEN_PROTECTION_CONTRACT_HDR := contracts/cyberdeck_screen_protection.h",
    "SCREEN_PROTECTION_TEST := test_screen_protection.cpp",
    "SCREEN_PROTECTION_BIN := test_screen_protection_bin",
    "SCREEN_PROTECTION_OBJS := test_screen_protection/test.o test_screen_protection/protection.o",
    "",
    "BINS := $(SCREEN_PROTECTION_BIN)",
    "MAKE_DEPFILES := $(addsuffix .d,$(SCREEN_PROTECTION_OBJS))",
    "",
    "%.o.d: ;",
    "-include $(MAKE_DEPFILES)",
    "",
    "test_screen_protection/test.o: $(SCREEN_PROTECTION_TEST) $(SCREEN_PROTECTION_HDR) $(SCREEN_PROTECTION_CONTRACT_HDR) Makefile",
    "\t@mkdir -p $(@D)",
    "\t$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@",
    "",
    "test_screen_protection/protection.o: $(SCREEN_PROTECTION_SRC) $(SCREEN_PROTECTION_HDR) Makefile",
    "\t@mkdir -p $(@D)",
    "\t$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@",
    "",
    "$(SCREEN_PROTECTION_BIN): $(SCREEN_PROTECTION_OBJS) Makefile",
    "\t$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include $(SCREEN_PROTECTION_OBJS) -o $@",
    "",
    ".PHONY: test_screen_protection",
    "test_screen_protection: $(SCREEN_PROTECTION_BIN)",
    "\t@./$(SCREEN_PROTECTION_BIN)",
    "",
    "test-build: $(BINS)",
    "",
    "test: test-build",
    "\t@$(MAKE) --no-print-directory test_screen_protection",
    "",
    "clean:",
    "\trm -f $(BINS)",
    "\trm -rf test_screen_protection",
    "",
])


def _mutate(text: str, old: str, new: str, label: str) -> str:
    require(old in text,
            f"mutation[{label}] could not be applied to the approved fixture")
    return text.replace(old, new, 1)


# The approved rules of the pilot, expressed with the variable names the host
# Makefile already declares.
APPROVED_SCREEN_PROTECTION_RULES = "\n".join([
    "SCREEN_PROTECTION_OBJS := test_screen_protection/test.o test_screen_protection/protection.o",
    "",
    "test_screen_protection/test.o: $(SCREEN_PROTECTION_TEST) $(SCREEN_PROTECTION_HDR) $(SCREEN_PROTECTION_CONTRACT_HDR) Makefile",
    "\t@mkdir -p $(@D)",
    "\t$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@",
    "",
    "test_screen_protection/protection.o: $(SCREEN_PROTECTION_SRC) $(SCREEN_PROTECTION_HDR) Makefile",
    "\t@mkdir -p $(@D)",
    "\t$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@",
    "",
    "$(SCREEN_PROTECTION_BIN): $(SCREEN_PROTECTION_OBJS) Makefile",
    "\t$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include $(SCREEN_PROTECTION_OBJS) -o $@",
    "",
    ".PHONY: test_screen_protection",
    "test_screen_protection: $(SCREEN_PROTECTION_BIN)",
    "\t@./$(SCREEN_PROTECTION_BIN)",
    "",
])

_MIGRATED_BLOCK = re.compile(
    r"^\.PHONY: test_screen_protection\n"
    r"^test_screen_protection_bin:.*\n(?:\t.*\n)+"
    r"^test_screen_protection:.*\n(?:\t.*\n)+",
    flags=re.M)


def _migrated_copy(makefile_text: str) -> str:
    """Return a copy of the host Makefile with the pilot in the approved form.

    Only the in-memory copy changes; the repository Makefile is never edited
    by this contract.
    """
    migrated = makefile_text
    anchor = "SCREEN_PROTECTION_TEST := test_screen_protection.cpp\n"
    require(anchor in migrated,
            "the migrated copy needs the SCREEN_PROTECTION_TEST declaration")
    # The approved link rule names its artifact through $(SCREEN_PROTECTION_BIN),
    # so the copy must declare that variable as well: without it the rule
    # target expands to an empty string and the approved form is unreachable.
    migrated = migrated.replace(
        anchor,
        anchor + f"SCREEN_PROTECTION_BIN := {SCREEN_PROTECTION_BIN}\n"
                 "SCREEN_PROTECTION_OBJS := test_screen_protection/test.o "
                 "test_screen_protection/protection.o\n",
        1)
    depfile_anchor = "MAKE_DEPFILES := $(addsuffix .d,"
    require(depfile_anchor in migrated,
            "the migrated copy needs the MAKE_DEPFILES declaration")
    migrated = migrated.replace(
        depfile_anchor,
        "MAKE_DEPFILES := $(addsuffix .d,$(SCREEN_PROTECTION_OBJS) ", 1)
    match = _MIGRATED_BLOCK.search(migrated)
    require(match is not None,
            "the migrated copy needs the current screen-protection rules")
    migrated = (migrated[:match.start()] + APPROVED_SCREEN_PROTECTION_RULES
                + migrated[match.end():])
    clean_anchor = "\n\trm -rf "
    require(clean_anchor in migrated,
            "the migrated copy needs the clean recipe")
    migrated = migrated.replace(
        clean_anchor, "\n\trm -rf test_screen_protection ", 1)
    return migrated


def check_green_path(makefile_text: str) -> None:
    """The approved form must be reachable in the real host Makefile.

    The pilot is the template for the remaining migrated targets, so the
    contract must stay satisfiable: the approved form is applied to an
    in-memory copy of the real Makefile and must be accepted there.
    """
    try:
        require_incremental_form(makefile_text, SCREEN_PROTECTION_TARGET,
                                  SCREEN_PROTECTION_BIN,
                                  SCREEN_PROTECTION_TEST_SOURCE,
                                  SCREEN_PROTECTION_PRODUCTION_SOURCE,
                                  "host Makefile (screen-protection pilot)")
        return
    except AssertionError:
        pass
    migrated = _migrated_copy(makefile_text)
    require_incremental_form(migrated, SCREEN_PROTECTION_TARGET,
                              SCREEN_PROTECTION_BIN,
                              SCREEN_PROTECTION_TEST_SOURCE,
                              SCREEN_PROTECTION_PRODUCTION_SOURCE,
                              "migrated copy of the host Makefile")


def check_mutation_detection() -> None:
    """Removing one element of the approved form must be rejected.

    Without these mutations the structural check could accept a Makefile that
    silently lost the depfile include, one of the original flags/includes, the
    Makefile prerequisite or the run wrapper.
    """
    require_incremental_form(APPROVED_SCREEN_PROTECTION_MAKEFILE,
                              SCREEN_PROTECTION_TARGET, SCREEN_PROTECTION_BIN,
                              SCREEN_PROTECTION_TEST_SOURCE,
                              SCREEN_PROTECTION_PRODUCTION_SOURCE,
                              "approved fixture")

    mutations = (
        ("depfile flag removed from the test translation unit",
         "$(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d",
         "$(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MF $@.d",
         "-MMD -MP"),
        ("depfile include removed from the Makefile",
         "-include $(MAKE_DEPFILES)\n", "", "-include"),
        ("original compile flag removed from the test translation unit",
         "$(CXX) $(CXXFLAGS) -Icontracts", "$(CXX) -Icontracts", "$(CXXFLAGS)"),
        ("original include path removed from the test translation unit",
         "$(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include",
         "$(CXXFLAGS) -I../../../components/cyberdeck/include", "-Icontracts"),
        ("Makefile prerequisite removed from the test object rule",
         "$(SCREEN_PROTECTION_CONTRACT_HDR) Makefile",
         "$(SCREEN_PROTECTION_CONTRACT_HDR)", "Makefile prerequisite"),
        ("Makefile prerequisite removed from the link rule",
         "$(SCREEN_PROTECTION_OBJS) Makefile",
         "$(SCREEN_PROTECTION_OBJS)", "Makefile prerequisite"),
        ("the .PHONY wrapper removed",
         ".PHONY: test_screen_protection\n"
         "test_screen_protection: $(SCREEN_PROTECTION_BIN)\n"
         "\t@./$(SCREEN_PROTECTION_BIN)\n\n", "", ".PHONY wrapper"),
        ("the generated depfiles dropped from the depfile list",
         "MAKE_DEPFILES := $(addsuffix .d,$(SCREEN_PROTECTION_OBJS))",
         "MAKE_DEPFILES :=", "depfile include"),
        ("second original include path removed from the test translation unit",
         "$(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include",
         "$(CXXFLAGS) -Icontracts", "-I../../../components/cyberdeck/include"),
        ("depfile output path removed from the test translation unit",
         "-MMD -MP -MF $@.d", "-MMD -MP", "-MF"),
        ("the binary made to depend on the source instead of the per-TU objects",
         "$(SCREEN_PROTECTION_BIN): $(SCREEN_PROTECTION_OBJS) Makefile",
         "$(SCREEN_PROTECTION_BIN): $(SCREEN_PROTECTION_SRC) Makefile",
         "per-TU objects"),
    )
    rejected = 0
    for label, old, new, marker in mutations:
        mutated = _mutate(APPROVED_SCREEN_PROTECTION_MAKEFILE, old, new, label)
        try:
            require_incremental_form(mutated, SCREEN_PROTECTION_TARGET,
                                      SCREEN_PROTECTION_BIN,
                                      SCREEN_PROTECTION_TEST_SOURCE,
                                      SCREEN_PROTECTION_PRODUCTION_SOURCE,
                                      f"mutation[{label}]")
        except AssertionError as error:
            require(marker in str(error),
                    f"mutation[{label}] was rejected, but not for the expected "
                    f"reason {marker!r}: {error}")
            rejected += 1
            continue
        raise AssertionError(
            f"mutation[{label}] was accepted: the structural check does not "
            f"detect the removed element")
    print(f"OK: mutation harness {rejected + 1}/{rejected + 1} "
          f"(1 positive accepted, {rejected} negatives rejected)")


# ---------------------------------------------------------------------------
# REQ-MIG-59 (pilot): depfile-driven invalidation scenarios.
#
# The incremental form only pays off when the generated depfiles describe the
# REAL include graph of both translation units.  These scenarios are answered
# by GNU make itself: an isolated copy of the tree is built with the real
# Makefile and the real compiler, and the rebuild plan is read from
# `make -n -W <header>`, i.e. make's own dependency engine answering "what
# would be rebuilt if this header changed".
#
# The shared screen-protection header is included by BOTH translation units
# (the production unit directly, the test unit through the host contract
# header), so touching it must invalidate test.o, protection.o and the link.
# The host contract header belongs to the test unit alone, so touching it must
# invalidate test.o and the link while protection.o stays intact.
#
# Nothing is fabricated: every scenario header is a real project header, the
# expected plan is derived from the depfiles the compiler generated, and the
# control scenario proves the plan follows the real include graph instead of
# assuming any header.
# ---------------------------------------------------------------------------

# A real project header that neither pilot translation unit includes: it is
# the negative control of the invalidation probe.
_PILOT_CONTROL_HEADER = (
    "../../../components/cyberdeck/include/platform/display/screen_off.h")


def _scratch_relative(make_relative: str) -> str:
    """Translate a Makefile-relative path into a repository-relative one."""
    prefix = "../../../"
    require(make_relative.startswith(prefix),
            f"the host Makefile path {make_relative!r} must stay inside the "
            f"repository tree")
    return make_relative[len(prefix):]


def _scratch_root() -> Path:
    """Return a writable directory for the isolated fixture."""
    for candidate in (os.environ.get("TMPDIR", ""), "/tmp/opencode",
                      tempfile.gettempdir()):
        if candidate and os.path.isdir(candidate) and os.access(candidate, os.W_OK):
            return Path(candidate)
    return Path(tempfile.gettempdir())


def _pilot_fixture_files(makefile_text: str) -> dict[str, str]:
    """Return the scratch-relative content of every file the pilot build needs."""
    return {
        "tests/host/keymap/Makefile": makefile_text,
        "tests/host/keymap/test_screen_protection.cpp":
            SCREEN_TEST_FILE.read_text(encoding="utf-8"),
        "tests/host/keymap/contracts/cyberdeck_screen_protection.h":
            STATE_CONTRACT.read_text(encoding="utf-8"),
        _scratch_relative(
            "../../../components/cyberdeck/include/platform/display/"
            "cyberdeck_screen_protection.h"):
            STATE_HEADER.read_text(encoding="utf-8"),
        _scratch_relative(
            "../../../components/cyberdeck/src/platform/display/"
            "cyberdeck_screen_protection.cpp"):
            STATE_SOURCE.read_text(encoding="utf-8"),
        _scratch_relative(_PILOT_CONTROL_HEADER):
            SCREEN_HEADER.read_text(encoding="utf-8"),
    }


def _materialize_fixture(files: dict[str, str]) -> Path:
    """Write the fixture into an isolated directory and return its root."""
    scratch = Path(tempfile.mkdtemp(prefix="cyberdeck-pilot-",
                                    dir=str(_scratch_root())))
    for relative, content in files.items():
        path = scratch / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    return scratch


def _make(build_dir: Path, *args: str) -> subprocess.CompletedProcess:
    """Run make inside the isolated fixture with a clean environment."""
    env = dict(os.environ)
    # The contract itself runs under make: inheriting the parent jobserver
    # flags would leak -j into the fixture build.
    env.pop("MAKEFLAGS", None)
    env.pop("MAKELEVEL", None)
    env["LC_ALL"] = "C"
    return subprocess.run(["make", "--no-print-directory", *args],
                          cwd=str(build_dir), capture_output=True, text=True,
                          env=env)


def _rebuild_plan(build_dir: Path, headers: tuple[str, ...]) -> set[str]:
    """Return the artifacts make would rebuild if the given headers changed."""
    args = ["-n"]
    for header in headers:
        args += ["-W", header]
    args.append(SCREEN_PROTECTION_BIN)
    result = _make(build_dir, *args)
    require(result.returncode == 0,
            f"the dry run of the isolated fixture failed: "
            f"{result.stderr.strip()[-800:]}")
    return set(re.findall(r"(?:^|\s)-o\s+(\S+)", result.stdout))


def _depfile_dependencies(text: str, object_path: str) -> set[str]:
    """Return the prerequisites the generated depfile declares for one object."""
    entries: set[str] = set()
    inside = False
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.endswith("\\"):
            line = line[:-1].rstrip()
        if ":" in line:
            target, _, dependencies = line.partition(":")
            inside = target.strip() == object_path
            if inside:
                entries.update(dependencies.split())
            continue
        if inside:
            entries.update(line.split())
    return entries


def _check_pilot_depfile_scenarios(files: dict[str, str], label: str) -> None:
    """Build the fixture in isolation and check the invalidation scenarios."""
    scratch = _materialize_fixture(files)
    try:
        build_dir = scratch / "tests" / "host" / "keymap"
        result = _make(build_dir, SCREEN_PROTECTION_BIN)
        require(result.returncode == 0,
                f"{label}: the pilot must build in isolation: "
                f"{(result.stderr.strip() or result.stdout.strip())[-800:]}")

        model = _MakeModel(files["tests/host/keymap/Makefile"])
        shared_header = model.expand(
            model.variables.get("SCREEN_PROTECTION_HDR", ""))
        exclusive_header = model.expand(
            model.variables.get("SCREEN_PROTECTION_CONTRACT_HDR", ""))
        require(shared_header and exclusive_header,
                f"{label}: the host Makefile must declare the shared and the "
                f"exclusive screen-protection headers")

        bin_rules = model.rules_for(SCREEN_PROTECTION_BIN)
        require(len(bin_rules) == 1,
                f"{label}: exactly one rule must produce {SCREEN_PROTECTION_BIN}")
        objects = [item for item in model.expand(
            " ".join(bin_rules[0].prerequisites)).split()
            if item.endswith(".o")]
        source_to_object: dict[str, str] = {}
        for obj in objects:
            obj_rules = model.rules_for(obj)
            require(len(obj_rules) == 1,
                    f"{label}: object {obj} must have exactly one compile rule")
            source = _compiled_source(model, obj_rules[0])
            require(source,
                    f"{label}: object {obj} must compile exactly one translation unit")
            source_to_object[source] = obj
        test_object = _object_for(source_to_object, SCREEN_PROTECTION_TEST_SOURCE)
        production_object = _object_for(source_to_object,
                                        SCREEN_PROTECTION_PRODUCTION_SOURCE)

        # The generated depfiles and their content are required for both
        # translation units: they are what makes the scenarios meaningful.
        test_depfile = build_dir / f"{test_object}.d"
        production_depfile = build_dir / f"{production_object}.d"
        for depfile, owner in ((test_depfile, test_object),
                               (production_depfile, production_object)):
            require(depfile.exists() and depfile.stat().st_size > 0,
                    f"{label}: {owner} must generate a non-empty depfile "
                    f"({owner}.d)")
        dependencies = {
            test_object: _depfile_dependencies(
                test_depfile.read_text(encoding="utf-8"), test_object),
            production_object: _depfile_dependencies(
                production_depfile.read_text(encoding="utf-8"), production_object),
        }

        def stale_objects(header: str) -> set[str]:
            return {obj for obj, deps in dependencies.items() if header in deps}

        def expected_plan(header: str) -> set[str]:
            stale = stale_objects(header)
            return stale | ({SCREEN_PROTECTION_BIN} if stale else set())

        # (a) the shared header invalidates both objects and the link.
        shared_stale = stale_objects(shared_header)
        require(shared_stale == {test_object, production_object},
                f"{label}: shared-header scenario: the shared header "
                f"{shared_header} must invalidate {test_object} and "
                f"{production_object}; the generated depfiles bind it to "
                f"{sorted(shared_stale)}")
        plan = _rebuild_plan(build_dir, (shared_header,))
        require(plan == expected_plan(shared_header) ==
                {test_object, production_object, SCREEN_PROTECTION_BIN},
                f"{label}: shared-header scenario: touching {shared_header} "
                f"must rebuild {test_object}, {production_object} and "
                f"{SCREEN_PROTECTION_BIN}; make planned {sorted(plan)}")

        # (b) the host contract header invalidates the test object and the
        #     link only: the production object must stay intact.
        exclusive_stale = stale_objects(exclusive_header)
        require(exclusive_stale == {test_object},
                f"{label}: exclusive-header scenario: the host contract header "
                f"{exclusive_header} must invalidate only {test_object}; the "
                f"generated depfiles bind it to {sorted(exclusive_stale)}")
        plan = _rebuild_plan(build_dir, (exclusive_header,))
        require(plan == expected_plan(exclusive_header) ==
                {test_object, SCREEN_PROTECTION_BIN},
                f"{label}: exclusive-header scenario: touching "
                f"{exclusive_header} must rebuild only {test_object} and "
                f"{SCREEN_PROTECTION_BIN}; make planned {sorted(plan)}")
        require(production_object not in plan,
                f"{label}: exclusive-header scenario: {production_object} must "
                f"stay intact when only {exclusive_header} changes; make "
                f"planned {sorted(plan)}")

        # (c) the probe follows the real include graph: a header neither
        #     translation unit includes must invalidate nothing.
        require(_PILOT_CONTROL_HEADER not in dependencies[test_object] and
                _PILOT_CONTROL_HEADER not in dependencies[production_object],
                f"{label}: the control header {_PILOT_CONTROL_HEADER} must "
                f"not be a dependency of the pilot translation units")
        plan = _rebuild_plan(build_dir, (_PILOT_CONTROL_HEADER,))
        require(not plan,
                f"{label}: control scenario: touching {_PILOT_CONTROL_HEADER}, "
                f"which neither pilot translation unit includes, must rebuild "
                f"nothing; make planned {sorted(plan)}")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def check_depfile_invalidation(makefile_text: str) -> None:
    """The shipped pilot must invalidate the right objects per header.

    The approved form is the template for the remaining migrated targets, so
    the same scenarios must hold there too.
    """
    _check_pilot_depfile_scenarios(_pilot_fixture_files(makefile_text),
                                   "host Makefile (screen-protection pilot)")
    _check_pilot_depfile_scenarios(
        _pilot_fixture_files(APPROVED_SCREEN_PROTECTION_MAKEFILE),
        "approved fixture")


def check_depfile_mutation_detection() -> None:
    """A broken include graph must be rejected by the invalidation scenarios.

    Without these mutations the scenarios could pass on a fixture whose
    depfiles do not describe the real includes of both translation units.
    """
    base = _pilot_fixture_files(MAKEFILE.read_text(encoding="utf-8"))
    test_source = base["tests/host/keymap/test_screen_protection.cpp"]
    production_source = base[
        "components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp"]
    shared_header = base[
        "components/cyberdeck/include/platform/display/"
        "cyberdeck_screen_protection.h"]

    bypass = test_source.replace(
        '#include "cyberdeck_screen_protection.h"',
        '#include "platform/display/cyberdeck_screen_protection.h"')
    require(bypass != test_source,
            "mutation[the test translation unit bypasses the host contract "
            "header] could not be applied to the real test source")
    detached = production_source.replace(
        '#include "platform/display/cyberdeck_screen_protection.h"',
        '#include "platform/display/cyberdeck_screen_protection_alias.h"')
    require(detached != production_source,
            "mutation[the production translation unit stops depending on the "
            "shared header] could not be applied to the real production source")

    mutations = (
        ("the test translation unit bypasses the host contract header",
         {"tests/host/keymap/test_screen_protection.cpp": bypass},
         "exclusive-header scenario"),
        ("the production translation unit stops depending on the shared header",
         {"components/cyberdeck/src/platform/display/"
          "cyberdeck_screen_protection.cpp": detached,
          "components/cyberdeck/include/platform/display/"
          "cyberdeck_screen_protection_alias.h": shared_header},
         "shared-header scenario"),
    )
    rejected = 0
    for label, overrides, marker in mutations:
        files = dict(base)
        files.update(overrides)
        try:
            _check_pilot_depfile_scenarios(files, f"mutation[{label}]")
        except AssertionError as error:
            require(marker in str(error),
                    f"mutation[{label}] was rejected, but not for the expected "
                    f"reason {marker!r}: {error}")
            rejected += 1
            continue
        raise AssertionError(
            f"mutation[{label}] was accepted: the invalidation scenarios do "
            f"not detect the broken include graph")
    print(f"OK: depfile mutation harness {rejected + 1}/{rejected + 1} "
          f"(1 positive accepted, {rejected} negatives rejected)")


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
    check_ui_routing(ui, SESSION.read_text(encoding="utf-8"))
    check_adapter(screen, screen_header, ui, app, component)
    check_serial_transitive_path(serial)
    check_wiring(contract)
    makefile = MAKEFILE.read_text(encoding="utf-8")
    check_incremental_precedent(makefile)
    check_mutation_detection()
    # The approved form must be reachable before the pilot is asserted: the
    # green path proves the requirement is satisfiable, so the RED below can
    # only mean "the migration has not landed yet".
    check_green_path(makefile)
    print("OK: REQ-MIG-59 green path accepted "
          "(approved incremental form is reachable)")
    check_screen_protection_incremental(makefile)
    print("OK: REQ-MIG-59 incremental form accepted "
          "(per-TU objects, depfiles, link and run wrapper)")
    check_depfile_invalidation(makefile)
    print("OK: REQ-MIG-59 depfile scenarios accepted "
          "(shared header invalidates both objects, host contract header only "
          "the test object)")
    check_depfile_mutation_detection()
    print("PASS: screen protection structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
