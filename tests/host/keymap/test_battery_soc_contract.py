#!/usr/bin/env python3
"""Structural contract for the pure battery state-of-charge estimator.

TEST-SOC-33 / TEST-SOC-35 from the approved matrix: the estimator must stay a
pure module (no ESP-IDF, FreeRTOS, I2C, NVS, LVGL or BSP), be registered in the
firmware build, mirror its public ABI in this directory's contracts/ header, and
anchor both the seed and the re-anchor on the open-circuit voltage helper that
lives in the pure battery module.  The Makefile must expose the behavioural and
structural targets and the code map must document the module and the TEST-SOC
traceability.

It reads production source only; it never opens hardware, a simulator or the
Serial Automation Bridge.
"""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SENSOR_DIR = ROOT / "components/cyberdeck/src/platform/sensors"
INCLUDE_SENSOR_DIR = ROOT / "components/cyberdeck/include/platform/sensors"
SOC_SOURCE = SENSOR_DIR / "cyberdeck_battery_soc.cpp"
SOC_HEADER = INCLUDE_SENSOR_DIR / "cyberdeck_battery_soc.h"
SOC_CONTRACT = ROOT / "tests/host/keymap/contracts/cyberdeck_battery_soc.h"
SOC_TEST = ROOT / "tests/host/keymap/test_battery_soc.cpp"
STATUS_SOURCE = SENSOR_DIR / "battery_status.cpp"
STATUS_HEADER = INCLUDE_SENSOR_DIR / "battery_status.h"
POLICY_SOURCE = SENSOR_DIR / "cyberdeck_battery_protection.cpp"
POLICY_HEADER = INCLUDE_SENSOR_DIR / "cyberdeck_battery_protection.h"
COMPONENT = ROOT / "components/cyberdeck/CMakeLists.txt"
RULES_PATH = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"

TRACEABILITY = (
    "TEST-SOC-01..18/26 -> test_battery_soc.cpp",
    "TEST-SOC-19..26 -> test_battery_protection.cpp",
    "TEST-SOC-36 -> test_battery_protection.cpp (classify before estimate)",
    "TEST-SOC-29/30 -> test_battery_contract.cpp + test_battery_reader_contract.py",
    "TEST-SOC-31 -> test_battery_reader_contract.py",
    "TEST-SOC-32/34 -> test_battery_protection_contract.py",
    "TEST-SOC-33/35 -> test_battery_soc_contract.py",
)

FORBIDDEN_IN_MODULE = (
    "esp_", "freertos", "FreeRTOS", "xTaskCreate", "vTaskDelay", "task.h",
    "lvgl", "lv_obj", "lv_timer", "LV_SYMBOL_", "i2c_master", "bsp_i2c",
    "esp_io_expander", "bsp_io_expander", "nvs_", "NVS", "esp_log", "ESP_LOG",
    "stdio", "printf", "std::string", "std::vector",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str | None:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{", source)
    if match is None:
        return None
    opening = source.find("{", match.start())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    return None


def check_module_is_pure(source: str, header: str) -> None:
    combined = header + "\n" + source
    for token in FORBIDDEN_IN_MODULE:
        require(token not in combined,
                f"the pure state-of-charge module must not reference {token!r}")

    includes = re.findall(r"^\s*#\s*include\s+([^\n]+)", source, re.M)
    allowed_source = ('"platform/sensors/cyberdeck_battery_soc.h"',
                      '"platform/sensors/battery_status.h"', "<cstdint>")
    for include in includes:
        require(include.strip() in allowed_source,
                f"the state-of-charge source may only include "
                f"{allowed_source}, found {include.strip()!r}")

    header_includes = re.findall(r"^\s*#\s*include\s+([^\n]+)", header, re.M)
    for include in header_includes:
        require(include.strip() in ("<cstdint>",),
                f"the state-of-charge header may only include <cstdint>, "
                f"found {include.strip()!r}")

    for token in ("pack_capacity_mah", "max_elapsed_ms", "reanchor_quiet_ms",
                  "reanchor_current_tolerance_ma", "max_slew_percentage_points",
                  "anchor_confirmations",
                  "struct input", "struct reading", "class state",
                  "elapsed_ms", "bool seeded", "percentage"):
        require(token in combined,
                f"the state-of-charge ABI must expose {token}")

    for pattern, description in (
        (r"pack_capacity_mah\s*=\s*2000\b", "pack capacity must be exactly 2000 mAh"),
        (r"max_elapsed_ms\s*=\s*60000\b", "the integration window must be 60000 ms"),
        (r"reanchor_quiet_ms\s*=\s*60000\b", "the re-anchor quiet time must be 60000 ms"),
        (r"reanchor_current_tolerance_ma\s*=\s*15\b", "the quiet current band must be 15 mA"),
        (r"anchor_confirmations\s*=\s*3\b", "the anchor must need three confirmations"),
        (r"max_slew_percentage_points\s*=\s*1\b", "the published slew must be one point"),
    ):
        require(re.search(pattern, combined, re.IGNORECASE) is not None, description)

    # The estimate is anchored on the pure open-circuit voltage helper, used by
    # both the seed and the re-anchor, and it never re-implements the window.
    require("ocv_from_terminal_mv" in source,
            "the estimator must anchor on ocv_from_terminal_mv")
    require("seed_from_ocv(ocv)" in source,
            "the seed must use the open-circuit voltage")
    require(re.search(r"percentage_from_bus_voltage_mv\s*\(\s*ocv(?:_mv)?\s*\)",
                      source) is not None,
            "the re-anchor must use the open-circuit voltage")
    # Scoped to the anchor function: the whole-source match above also matches
    # the seed, so on its own it could stay green after the anchor stopped using
    # the open-circuit value.
    anchor = function_body(source, "update_anchor")
    require(anchor is not None,
            "the estimator must implement the single-shot anchor in update_anchor")
    anchor = anchor or ""
    require(re.search(r"percentage_from_bus_voltage_mv\s*\(\s*ocv(?:_mv)?\s*\)",
                      anchor) is not None,
            "update_anchor must derive its target from the open-circuit voltage")
    require("anchored_" in anchor,
            "update_anchor must stay single-shot through the anchored_ flag")
    require("anchor_confirmations" in anchor,
            "update_anchor must wait for the approved confirmation count")
    require(re.search(r"percentage_unit_scale\s*/\s*2", anchor) is not None,
            "update_anchor must write the centre of the target bin")
    require("percentage_from_bus_voltage_mv" in source,
            "the estimator must reuse percentage_from_bus_voltage_mv")
    require(re.search(r"\b6000\b|\b8230\b", source) is None,
            "the estimator must not duplicate the 6000..8230 mV window")

    require(re.search(r"equivalent_series_mohm\s*=\s*330\b", header_of_status()) is not None,
            "the pure battery module must declare the 330 mOhm series resistance")
    require("ocv_from_terminal_mv" in header_of_status(),
            "the pure battery module must declare ocv_from_terminal_mv")
    require("ocv_from_terminal_mv" in read(STATUS_SOURCE),
            "ocv_from_terminal_mv must be defined in battery_status.cpp")


def header_of_status() -> str:
    return strip_comments(read(STATUS_HEADER))


def check_registration(component: str, policy_source: str, header: str) -> None:
    require("src/platform/sensors/cyberdeck_battery_soc.cpp" in component,
            "the firmware build must compile the pure state-of-charge source")
    require("cyberdeck_battery_soc.h" in SOC_CONTRACT.read_text(encoding="utf-8"),
            "the test contract must name the production state-of-charge header")
    require("cyberdeck_battery_soc.h" in policy_source,
            "the battery policy must consume the state-of-charge module")
    require("cyberdeck_battery_soc::state" in policy_source and
            "soc_.observe(" in policy_source,
            "the battery policy must own one estimator instance")
    input_start = header.find("struct input")
    require(input_start >= 0, "the estimator ABI must declare struct input")
    input_block = header[input_start:input_start + 900]
    for field in ("valid", "present", "charging", "bus_voltage_mv",
                  "current_ma", "elapsed_ms"):
        require(field in input_block,
                f"the estimator input must carry {field}")


def check_makefile_and_codemap(makefile: str, codemap: str) -> None:
    for target in ("test_battery_soc:", "test_battery_soc_contract:"):
        require(target in makefile, f"the Makefile must register {target}")
    require("test_battery_soc.cpp" in makefile and
            "test_battery_soc_contract.py" in makefile,
            "the Makefile must depend on both state-of-charge tests")
    require(re.search(r"(?m)^BATTERY_SOC_BIN\s*:=\s*test_battery_soc_bin", makefile) is not None,
            "the state-of-charge executable must be test_battery_soc_bin")
    require(re.search(r"(?m)^BATTERY_SOC_OBJS\s*:=\s*.*test_battery_soc/soc\.o",
                      makefile) is not None,
            "the state-of-charge target must compile incrementally per TU")
    require("$(BATTERY_SOC_BIN)" in makefile.split("BINS :=", 1)[1],
            "the state-of-charge executable must be registered in BINS")
    require(re.search(r"--no-print-directory test_battery_soc\s*$", makefile, re.M) is not None and
            re.search(r"--no-print-directory test_battery_soc_contract\s*$", makefile, re.M) is not None,
            "the aggregated test target must run both state-of-charge targets")
    clean = makefile.split("\nclean:", 1)[1]
    require("test_battery_soc" in clean,
            "clean must remove the state-of-charge objects and executable")

    for token in ("cyberdeck_battery_soc.cpp", "cyberdeck_battery_soc.h",
                  "cyberdeck_battery_soc", "test_battery_soc.cpp",
                  "test_battery_soc_contract.py", "TEST-SOC-",
                  "REQ-BAT-011", "REQ-BAT-012", "pack_capacity_mah",
                  "ocv_from_terminal_mv", "current_ma_from_raw"):
        require(token in codemap,
                f"code-map.md must document the state-of-charge item {token}")

    for source in ("test_battery_soc.cpp", "test_battery_soc_contract.py"):
        visible = subprocess.run(
            ["git", "check-ignore", "-q", "--no-index", f"tests/host/keymap/{source}"],
            cwd=str(ROOT), capture_output=True).returncode
        require(visible != 0,
                f"gitignore must keep tests/host/keymap/{source} trackable")
    for artifact in ("test_battery_soc_bin", "test_battery_soc"):
        ignored = subprocess.run(
            ["git", "check-ignore", "-q", "--no-index", f"tests/host/keymap/{artifact}"],
            cwd=str(ROOT), capture_output=True).returncode
        require(ignored == 0,
                f"gitignore must keep the compiled {artifact} ignored")


def main() -> int:
    for path in (SOC_SOURCE, SOC_HEADER, SOC_CONTRACT, SOC_TEST, STATUS_SOURCE,
                 STATUS_HEADER, POLICY_SOURCE, POLICY_HEADER, COMPONENT,
                 RULES_PATH, CODEMAP):
        require(path.exists(), f"required state-of-charge contract input is missing: {path}")

    source = strip_comments(read(SOC_SOURCE))
    header = strip_comments(read(SOC_HEADER))
    policy_source = strip_comments(read(POLICY_SOURCE))
    policy_header = strip_comments(read(POLICY_HEADER))
    component = strip_comments(read(COMPONENT))
    makefile = read(RULES_PATH)
    codemap = strip_comments(read(CODEMAP))

    check_module_is_pure(source, header)
    check_registration(component, policy_source, header)
    check_makefile_and_codemap(makefile, codemap)

    require("elapsed_ms" in policy_header,
            "the policy observation must carry the elapsed interval")
    for line in TRACEABILITY:
        print(f"TRACE {line}")
    print("PASS: battery state-of-charge purity/registration structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
