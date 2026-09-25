#!/usr/bin/env python3
"""RED structural contract for the battery power/protection integration.

The pure policy is exercised by ``test_battery_protection.cpp``.  The expander,
INA226 adapter, NVS persistence, LVGL timer, shell, and serial bridge are not
host-linkable, so this test inspects the real sources instead.  It never opens
I2C, a simulator, Serial Automation Bridge, a display, or a power rail.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
SENSOR_DIR = ROOT / "components/cyberdeck/src/platform/sensors"
INCLUDE_SENSOR_DIR = ROOT / "components/cyberdeck/include/platform/sensors"
PURE_SOURCE = SENSOR_DIR / "cyberdeck_battery_protection.cpp"
PURE_HEADER = INCLUDE_SENSOR_DIR / "cyberdeck_battery_protection.h"
PURE_CONTRACT = ROOT / "tests/host/keymap/contracts/cyberdeck_battery_protection.h"
PURE_TEST = ROOT / "tests/host/keymap/test_battery_protection.cpp"
ADAPTER_SOURCE = SENSOR_DIR / "battery_protection.cpp"
ADAPTER_HEADER = INCLUDE_SENSOR_DIR / "battery_protection.h"
READER_SOURCE = SENSOR_DIR / "ina226_reader.cpp"
READER_HEADER = INCLUDE_SENSOR_DIR / "ina226_reader.h"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SHELL = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp"
SHELL_HEADER = ROOT / "components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h"
SERIAL = ROOT / "components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp"
SERIAL_TEST = ROOT / "tests/host/keymap/test_serial_ndjson_dispatch.cpp"
APP = ROOT / "main/app_main.cpp"
COMPONENT = ROOT / "components/cyberdeck/CMakeLists.txt"
RULES_PATH = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"
GITIGNORE = ROOT / ".gitignore"
ARCHITECTURE = ROOT / "docs/ARCHITECTURE.md"
ARCHITECTURE_PT_BR = ROOT / "docs/ARCHITECTURE.pt-BR.md"

TRACEABILITY = (
    "REQ-BAT-007 -> expander-B pin6 CHG_STAT active-low and pin7 CHG_EN",
    "REQ-BAT-008 -> battery/external/charging/absent/unknown thresholds and votes",
    "REQ-BAT-009 -> 90/85 protection hysteresis and fail-safe state",
    "REQ-BAT-010 -> NVS default/option, UI timer/snapshot, shell and ui.type",
)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source: str, signature: str) -> str | None:
    """Return a balanced function body, skipping declarations when possible."""
    start = 0
    while True:
        marker = source.find(signature, start)
        if marker < 0:
            return None
        opening = source.find("{", marker)
        semicolon = source.find(";", marker)
        if semicolon >= 0 and semicolon < opening:
            start = semicolon + 1
            continue
        if opening < 0:
            return None
        depth = 0
        for index in range(opening, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[opening:index + 1]
        return None


def nearby(source: str, token: str, radius: int = 500) -> str:
    position = source.find(token)
    return source[max(0, position - radius):position + radius] if position >= 0 else ""


def require_all(failures: list[str], condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def read(path: Path, failures: list[str]) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        failures.append(f"cannot read {path}: {error}")
        return ""


def check_pure_contract(failures: list[str], component: str, makefile: str) -> None:
    for path in (PURE_SOURCE, PURE_HEADER, PURE_CONTRACT, PURE_TEST):
        require_all(failures, path.exists(),
                    f"battery protection TDD input is missing: {path}")
    if not PURE_SOURCE.exists() or not PURE_HEADER.exists():
        return

    source = strip_comments(read(PURE_SOURCE, failures))
    header = strip_comments(read(PURE_HEADER, failures))
    contract = read(PURE_CONTRACT, failures)
    combined = source + "\n" + header

    for token in (
        "current_uncertainty_ma", "external_voltage_mv", "absent_voltage_mv",
        "state_vote_count", "protection_enter_percentage",
        "protection_enter_voltage_mv", "protection_exit_percentage",
        "default_protection_enabled", "battery_state", "charge_signal",
        "decode_chg_stat", "class state", "observe", "last_safe_snapshot",
        "set_protection_enabled", "charger_enabled",
    ):
        require_all(failures, token in combined,
                    f"pure battery protection ABI must expose {token}")

    for pattern, description in (
        (r"current_uncertainty_ma\s*=\s*15\b", "current uncertainty must be exactly 15 mA"),
        (r"external_voltage_mv\s*=\s*7900\b", "external threshold must be exactly 7900 mV"),
        (r"absent_voltage_mv\s*=\s*8330\b", "absent threshold must be exactly 8330 mV"),
        (r"state_vote_count\s*=\s*5\b", "state stabilization must require exactly five votes"),
        (r"protection_enter_percentage\s*=\s*90\b", "protection enter percentage must be exactly 90"),
        (r"protection_enter_voltage_mv\s*=\s*8200\b", "protection enter voltage must be exactly 8200 mV"),
        (r"protection_exit_percentage\s*=\s*85\b", "protection resume percentage must be exactly 85"),
        (r"default_protection_enabled\s*=\s*true\b", "the only NVS option default must be true"),
    ):
        require_all(failures, re.search(pattern, combined, re.IGNORECASE) is not None,
                    description)

    for forbidden in ("lvgl.h", "bsp/esp-bsp.h", "nvs.h", "nvs_flash.h",
                      "freertos", "i2c_master", "esp_io_expander", "xTaskCreate"):
        require_all(failures, forbidden not in source,
                    f"pure battery policy must not depend on {forbidden}")

    for value in ("battery", "external", "charging", "absent", "unknown"):
        require_all(failures, value in combined,
                    f"battery state vocabulary must include {value}")

    require_all(failures, "cyberdeck_battery_protection.cpp" in component,
                "component CMake must register the pure battery protection source")
    require_all(failures, "cyberdeck_battery_protection.h" in contract,
                "the host contract must name the production battery header")
    # Discoverability must come from the registered build target, not from a
    # false self-reference inside the test source.  Keep the behavioral test
    # named explicitly while also rejecting a target/binary name collision.
    require_all(failures, "test_battery_protection.cpp" in makefile,
                "Makefile must keep test_battery_protection.cpp discoverable")
    binary = re.search(r"(?m)^BATTERY_PROTECTION_BIN\s*:=\s*(\S+)", makefile)
    require_all(failures, binary is not None and
                binary.group(1) != "test_battery_protection",
                "battery protection executable must not share the public target name")
    require_all(failures, re.search(
        r"(?m)^test_battery_protection:\s+\$\(BATTERY_PROTECTION_BIN\)\s*$",
        makefile) is not None,
        "test_battery_protection must depend on its distinct executable")


def check_expander_and_fail_safe(failures: list[str]) -> None:
    if not ADAPTER_SOURCE.exists() or not ADAPTER_HEADER.exists():
        return
    source = strip_comments(read(ADAPTER_SOURCE, failures))
    header = strip_comments(read(ADAPTER_HEADER, failures))
    combined = source + "\n" + header

    for token in (
        "bsp_io_expander1_init", "IO_EXPANDER_PIN_NUM_6", "IO_EXPANDER_PIN_NUM_7",
        "CHG_STAT", "CHG_EN", "esp_io_expander_get_level", "esp_io_expander_set_dir",
        "esp_io_expander_set_level", "IO_EXPANDER_INPUT", "IO_EXPANDER_OUTPUT",
    ):
        require_all(failures, token in combined,
                    f"expander protection adapter must expose {token}")

    stat_area = nearby(combined, "CHG_STAT", 700)
    en_area = nearby(combined, "CHG_EN", 700)
    require_all(failures, "IO_EXPANDER_PIN_NUM_6" in stat_area,
                "CHG_STAT must be expander-B pin 6")
    require_all(failures, "IO_EXPANDER_PIN_NUM_7" in en_area,
                "CHG_EN must be expander-B pin 7")
    require_all(failures, "IO_EXPANDER_INPUT" in stat_area,
                "CHG_STAT must be configured as an input")
    require_all(failures, "IO_EXPANDER_OUTPUT" in en_area,
                "CHG_EN must be configured as an output")
    require_all(failures, re.search(r"(?:==\s*0|!|not_charging|charging)",
                                    stat_area, re.IGNORECASE) is not None,
                "CHG_STAT decode must preserve active-low semantics")

    # The normal initialization path must actively drive CHG_EN high.  A missing
    # or invalid NVS value is allowed to use the true default, but must not use
    # false as a fallback or silently turn the charger off.
    init_body = function_body(source, "battery_protection_init(")
    if init_body is None:
        init_body = function_body(source, "battery_protection_start(")
    require_all(failures, init_body is not None,
                "protection adapter must expose an initialization/start seam")
    if init_body is not None:
        require_all(failures, re.search(r"CHG_EN[\s\S]{0,500}(?:set_level|level)[\s\S]{0,250}1",
                                        init_body, re.IGNORECASE) is not None or
                    re.search(r"set_level[\s\S]{0,250}1[\s\S]{0,500}CHG_EN",
                              init_body, re.IGNORECASE) is not None,
                "CHG_EN must be initialized high (enabled)")

    # Reader ownership is deliberately separate: it may read INA226 registers,
    # but it must not own charger GPIOs or policy.
    reader = strip_comments(read(READER_SOURCE, failures)) if READER_SOURCE.exists() else ""
    reader_header = strip_comments(read(READER_HEADER, failures)) if READER_HEADER.exists() else ""
    reader_sources = reader + "\n" + reader_header
    for pattern in (r"\bCHG_STAT\b", r"\bCHG_EN\b", r"bsp_io_expander",
                    r"esp_io_expander", r"battery_protection", r"\bnvs_"):
        require_all(failures, re.search(pattern, reader_sources, re.IGNORECASE) is None,
                    f"INA226 sensor reader must remain free of {pattern}")
    require_all(failures, "0x41" in reader or "0X41" in reader,
                "INA226 reader must retain sensor address 0x41")
    require_all(failures, "ina226_reader" in header or "ina226_reader" in source,
                "expander adapter must consume the existing INA226 reader seam")


def check_persistence(failures: list[str], app: str) -> None:
    if not ADAPTER_SOURCE.exists():
        return
    source = strip_comments(read(ADAPTER_SOURCE, failures))
    for token in ("nvs.h", "nvs_open", "nvs_get_", "nvs_set_", "nvs_commit", "nvs_close"):
        require_all(failures, token in source,
                    f"battery protection persistence must perform NVS {token}")
    require_all(failures, "default_protection_enabled" in source,
                "NVS missing/invalid path must use the true option default")
    require_all(failures, "ESP_ERR_NVS_NOT_FOUND" in source,
                "missing NVS option must be handled explicitly")
    require_all(failures, re.search(r"ESP_LOG[EWI]\s*\(", source) is not None,
                "NVS/INA/CHG failures must be logged without becoming fatal")

    for function_name in ("load_protection", "restore_protection", "persist_protection"):
        body = function_body(source, function_name + "(")
        if body is not None:
            require_all(failures, "CHG_EN" not in body and "set_level" not in body,
                        f"{function_name} failure path must not drive CHG_EN off")

    require_all(failures, app.find("nvs_flash_init") >= 0,
                "app_main must initialize NVS before protection persistence")
    require_all(failures, "battery_protection" in app,
                "app_main must compose the battery protection adapter")


def check_ui_shell_and_serial(failures: list[str]) -> None:
    ui = strip_comments(read(UI, failures))
    shell = strip_comments(read(SHELL, failures))
    shell_header = strip_comments(read(SHELL_HEADER, failures))
    serial = strip_comments(read(SERIAL, failures))
    serial_test = read(SERIAL_TEST, failures)

    # The LVGL timer consumes a copied snapshot.  It must not acquire raw I2C,
    # NVS, or expander state on the LVGL task.
    require_all(failures, "lv_timer_create" in ui,
                "UI must retain a timer for the battery snapshot")
    timer_calls = re.findall(r"lv_timer_create\s*\([^;]{0,500}", ui, re.S)
    require_all(failures, any("1000" in call for call in timer_calls),
                "battery/UI refresh timer must retain the one-second cadence")
    require_all(failures, re.search(r"battery[^;]{0,500}(?:snapshot|get_snapshot)",
                                    ui, re.IGNORECASE | re.S) is not None,
                "UI timer must consume a battery protection snapshot")
    for forbidden in ("i2c_master_", "bsp_i2c_", "esp_io_expander", "nvs_",
                      "CHG_EN", "CHG_STAT"):
        require_all(failures, forbidden not in ui,
                    f"UI battery refresh must not perform {forbidden} directly")

    parser = function_body(shell, "cyberdeck_cmd_t cyberdeck_parse_command(")
    require_all(failures, parser is not None,
                "shell parser seam is missing")
    if parser is not None:
        for command in ('"battery protection on"', '"battery protection off"',
                        '"battery protection status"'):
            require_all(failures, command in parser,
                        f"shell parser must route {command}")
        require_all(failures, re.search(r"battery[^;]{0,180}protection",
                                        shell_header, re.IGNORECASE) is not None,
                    "shell public contract must expose battery protection command")

    execute = function_body(ui, "void execute_line(")
    require_all(failures, execute is not None,
                "UI command dispatch seam is missing")
    if execute is not None:
        for command in ("battery protection on", "battery protection off",
                        "battery protection status"):
            require_all(failures, command in execute,
                        f"UI dispatch must expose {command}")
        require_all(failures, "battery_protection" in execute,
                    "UI battery commands must call the protection adapter")
        require_all(failures, "i2c_" not in execute and "nvs_" not in execute,
                    "UI command dispatch must not perform I2C/NVS directly")

    # ui.type remains the only serial route: no battery-specific serial side
    # channel is allowed, and the host protocol regression carries all forms.
    require_all(failures, '"ui.type"' in serial and "exec_ui_type" in serial,
                "serial bridge must retain the ui.type text executor")
    ui_type_body = function_body(serial, "void exec_ui_type(")
    require_all(failures, ui_type_body is not None and
                "inject_text_segmented" in ui_type_body and "inject_enter" in ui_type_body,
                "ui.type must still submit battery commands through the keyboard path")
    for command in ("battery protection on", "battery protection off",
                    "battery protection status"):
        require_all(failures, command in serial_test,
                    f"serial host regression must carry {command!r} through ui.type")


def check_traceability(failures: list[str]) -> None:
    codemap = read(CODEMAP, failures)
    makefile = read(RULES_PATH, failures)
    gitignore = read(GITIGNORE, failures)
    for token in (
        "test_battery_protection.cpp", "test_battery_protection_contract.py",
        "cyberdeck_battery_protection.cpp", "CHG_STAT", "CHG_EN",
        "7900", "8330", "90", "85", "5 votos", "battery protection",
    ):
        require_all(failures, token in codemap,
                    f"code-map.md must trace battery protection item {token}")
    for target in ("test_battery_protection:", "test_battery_protection_contract:"):
        require_all(failures, target in makefile,
                    f"Makefile must register {target}")
    require_all(failures, "test_battery_protection.cpp" in makefile and
                "test_battery_protection_contract.py" in makefile,
                "Makefile must depend on both battery protection tests")
    for exception in (
        "!tests/host/keymap/test_battery_protection.cpp",
        "!tests/host/keymap/test_battery_protection_contract.py",
    ):
        require_all(failures, exception in gitignore,
                    f"gitignore must keep {exception} trackable")

    for path in (ARCHITECTURE, ARCHITECTURE_PT_BR):
        documentation = read(path, failures)
        require_all(failures, "code-map.md" in documentation,
                    f"{path.name} must point to the battery traceability map")
        for token in ("CHG_STAT", "CHG_EN", "battery protection", "7900", "8330", "90", "85"):
            require_all(failures, token in documentation,
                        f"{path.name} must document {token}")


def main() -> int:
    failures: list[str] = []
    component = read(COMPONENT, failures)
    app = strip_comments(read(APP, failures))
    makefile = read(RULES_PATH, failures)
    check_pure_contract(failures, component, makefile)
    check_expander_and_fail_safe(failures)
    check_persistence(failures, app)
    check_ui_shell_and_serial(failures)
    check_traceability(failures)

    for trace in TRACEABILITY:
        print(f"TRACE {trace}")
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(f"FAIL: {len(failures)} battery protection structural checks")
        return 1
    print("PASS: battery protection expander/persistence/UI/shell contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
