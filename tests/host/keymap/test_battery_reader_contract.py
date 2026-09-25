#!/usr/bin/env python3
"""RED structural contract for the INA226 battery reader.

The reader is an ESP-IDF/FreeRTOS integration and is intentionally not linked
on the host.  This test inspects the real source/header seam and fixes the
approved integration invariants without faking I2C, a simulator, or hardware.

Traceability:
  REQ-BAT-001 -> measured bus voltage, explicit 6000..8230 mV API, and no
                  capacity_mah input.
  REQ-BAT-002 -> presence/read failure/state handoff without fabricated data.
  REQ-BAT-004 -> startup remains non-fatal and no charger-control seam exists.
  REQ-BAT-005 -> INA226 address/configuration/calibration, dedicated task,
                  mutex-protected snapshot, and one-second cadence.
  REQ-BAT-006 -> the battery documentation and code map retain the complete
                  requirement-to-test traceability.

Required production seam:
  * INA226 address 0x41 on the I2C bus;
  * bus-voltage register 0x02 is read and converted to mV (1.25 mV/LSB);
  * configuration and calibration are written during initialization;
  * the sample field is explicitly bus_voltage_mv, never capacity_mah;
  * sampling runs in a dedicated task with a one-second cadence;
  * a copied/owned snapshot is published for the UI;
  * the INA226 reader remains sensor-only: it has no CHG_EN/CHG_STAT,
    expander control, NVS, or battery-protection policy; those belong to the
    separate protection adapter;
  * the sampling task does not call I2C or LVGL from an LVGL timer.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
READER = ROOT / "components/cyberdeck/src/platform/sensors/ina226_reader.cpp"
READER_HDR = ROOT / "components/cyberdeck/include/platform/sensors/ina226_reader.h"
PROTECTION_ADAPTER = ROOT / "components/cyberdeck/src/platform/sensors/battery_protection.cpp"
PROTECTION_ADAPTER_HDR = ROOT / "components/cyberdeck/include/platform/sensors/battery_protection.h"
BATTERY_STATUS = ROOT / "components/cyberdeck/include/platform/sensors/battery_status.h"
BATTERY_SOURCE = ROOT / "components/cyberdeck/src/platform/sensors/battery_status.cpp"
BATTERY_CONTRACT = ROOT / "tests/host/keymap/contracts/cyberdeck_battery.h"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
APP = ROOT / "main/app_main.cpp"
COMPONENT = ROOT / "components/cyberdeck/CMakeLists.txt"
MAKEFILE_PATH = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"
ARCHITECTURE = ROOT / "docs/ARCHITECTURE.md"
ARCHITECTURE_PT_BR = ROOT / "docs/ARCHITECTURE.pt-BR.md"

TRACEABILITY = (
    "REQ-BAT-001 -> test_battery_contract.cpp + test_battery_reader_contract.py",
    "REQ-BAT-002 -> test_battery_contract.cpp + test_battery_reader_contract.py",
    "REQ-BAT-004 -> test_battery_reader_contract.py + test_battery_ui_contract.py",
    "REQ-BAT-005 -> test_battery_reader_contract.py",
    "REQ-BAT-006 -> test_battery_reader_contract.py + docs/ARCHITECTURE*.md + code-map.md",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{", source)
    require(match is not None, f"reader must define {name}()")
    assert match is not None
    opening = source.find("{", match.start())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"unterminated body for {name}()")


def main() -> int:
    paths = (READER, READER_HDR, PROTECTION_ADAPTER, PROTECTION_ADAPTER_HDR,
             BATTERY_STATUS, BATTERY_SOURCE, BATTERY_CONTRACT, UI, APP,
             COMPONENT, MAKEFILE_PATH, CODEMAP, ARCHITECTURE, ARCHITECTURE_PT_BR)
    for path in paths:
        require(path.exists(), f"required battery contract input is missing: {path}")

    reader = strip_comments(READER.read_text(encoding="utf-8"))
    header = strip_comments(READER_HDR.read_text(encoding="utf-8"))
    protection_adapter = strip_comments(PROTECTION_ADAPTER.read_text(encoding="utf-8"))
    protection_adapter_header = strip_comments(
        PROTECTION_ADAPTER_HDR.read_text(encoding="utf-8"))
    battery_status = strip_comments(BATTERY_STATUS.read_text(encoding="utf-8"))
    battery_source = strip_comments(BATTERY_SOURCE.read_text(encoding="utf-8"))
    battery_contract = strip_comments(BATTERY_CONTRACT.read_text(encoding="utf-8"))
    ui = strip_comments(UI.read_text(encoding="utf-8"))
    app = strip_comments(APP.read_text(encoding="utf-8"))
    component = strip_comments(COMPONENT.read_text(encoding="utf-8"))
    makefile = strip_comments(MAKEFILE_PATH.read_text(encoding="utf-8"))
    codemap = strip_comments(CODEMAP.read_text(encoding="utf-8"))
    architecture = strip_comments(ARCHITECTURE.read_text(encoding="utf-8"))
    architecture_pt_br = strip_comments(
        ARCHITECTURE_PT_BR.read_text(encoding="utf-8"))

    battery_api = "\n".join((battery_status, battery_source, reader, header))

    # REQ-BAT-001: the old capacity-shaped ABI must be gone.  This is an
    # intentional RED assertion against the currently unchanged production.
    require("bus_voltage_mv" in battery_contract and
            "capacity_mah" not in battery_contract,
            "the test contract must itself use bus_voltage_mv, not capacity_mah")
    require("bus_voltage_mv" in battery_api,
            "battery API/reader must use an explicit bus_voltage_mv input")
    require("capacity_mah" not in battery_api,
            "battery production seam must not retain capacity_mah")
    require("percentage_from_bus_voltage_mv" in battery_status and
            "percentage_from_bus_voltage_mv" in battery_source,
            "battery API must expose percentage_from_bus_voltage_mv")
    require(re.search(r"\b6000\b", battery_status) is not None and
            re.search(r"\b8230\b", battery_status) is not None,
            "battery API must use the approved 6000..8230 mV validation window")
    require(re.search(r"\b8400\b", battery_api) is None,
            "battery production seam must not retain the old 8400 mV limit")
    require(re.search(r"struct\s+sample\b[\s\S]*?bus_voltage_mv", battery_status) is not None,
            "sample contract must carry bus_voltage_mv explicitly")

    # The INA226 conversion must be tied to the measured bus-voltage register,
    # not a renamed capacity variable.  1.25 mV/LSB is 5/4 in integer math.
    require(re.search(r"BUS_VOLTAGE_REGISTER\s*=\s*0x02\b", reader, re.IGNORECASE),
            "reader must read the INA226 bus-voltage register 0x02")
    require(re.search(r"bus_voltage_raw\s*\*\s*5(?:U|UL|ULL)?\s*/\s*4(?:U|UL|ULL)?",
                      reader, re.IGNORECASE) is not None or
            re.search(r"\b1\.25\b", reader) is not None,
            "reader must convert INA226 bus-voltage LSBs to mV (5/4 or 1.25)")
    require(re.search(r"bus_voltage_mv\s*[,;}]", reader) is not None,
            "reader must publish the converted value into bus_voltage_mv")
    require(re.search(r"bus_voltage_raw\s*==\s*0", reader) is not None,
            "zero/invalid bus-voltage reads must not become fabricated samples")
    require(re.search(r"0x8000|current_raw_unsigned", reader, re.IGNORECASE),
            "reader must preserve the signed current sign used by state classification")
    require(re.search(r"read_sample\s*\([^)]*\).*?return\s+false", reader, re.S) is not None,
            "failed INA226 reads must be reported as unavailable samples")

    # REQ-BAT-002: the state vocabulary must be explicit, including the
    # absence/read-failure distinction and a real neutral state.  Zero (or
    # otherwise indeterminate) current must not be routed to absent/charging.
    for state in ("absent", "unavailable", "discharging", "charging", "neutral"):
        require(re.search(rf"\b{state}\b", battery_status) is not None,
                f"battery state contract must expose {state}")
    require("consuming" not in battery_status,
            "battery state must not retain the old consuming vocabulary")
    current_classifier = function_body(battery_source, "classify_current_ma")
    require(re.search(r"\bneutral\b", current_classifier) is not None,
            "current classifier must expose a neutral zero/indeterminate state")
    require(re.search(r"\bcurrent_ma\b", current_classifier) is not None and
            re.search(r"\b0\b", current_classifier) is not None,
            "current classifier must handle zero/indeterminate current explicitly")
    require("absent" not in current_classifier and
            "unavailable" not in current_classifier,
            "current classification must not invent absent/unavailable")

    # REQ-BAT-005: preserve the dedicated task, mutex-protected copied
    # snapshot, and exact one-second cadence from the approved plan.
    require("INA226" in battery_api.upper(), "reader/header must identify INA226")
    require(re.search(r"(?:0[xX]41\b|INA226.*(?:I2C_)?(?:ADDRESS|ADDR))",
                      reader + "\n" + header, re.IGNORECASE),
            "reader must use INA226 I2C address 0x41")
    require(re.search(r"CONFIGURATION_REGISTER\s*=\s*0x00", reader, re.IGNORECASE),
            "reader must define the INA226 configuration register")
    require(re.search(r"CALIBRATION_REGISTER\s*=\s*0x05", reader, re.IGNORECASE),
            "reader must define the INA226 calibration register")
    require(re.search(r"CONFIGURATION_VALUE\s*=\s*0x4527", reader, re.IGNORECASE),
            "reader must write the approved INA226 configuration value")
    require(re.search(r"CALIBRATION_VALUE\s*=\s*0x0D55", reader, re.IGNORECASE),
            "reader must write the approved INA226 calibration value")
    init = function_body(reader, "ina226_reader_init")
    require(re.search(r"config", init, re.IGNORECASE) and
            re.search(r"calib", init, re.IGNORECASE),
            "initialization must contain both configuration and calibration")
    require(re.search(r"(?:write|writ|i2c)", init, re.IGNORECASE),
            "initialization must perform register writes")

    require("xTaskCreate" in reader, "reader must create a dedicated task")
    require("vTaskDelay" in reader, "reader task must pace samples with vTaskDelay")
    require(re.search(r"vTaskDelay\s*\(\s*pdMS_TO_TICKS\s*\(\s*1000\s*\)", reader),
            "reader task must delay exactly 1000 ms between samples")
    require("lv_timer_create" not in reader,
            "battery sampling must not be an LVGL timer")
    require(re.search(r"std::mutex|portMUX|QueueHandle_t|std::atomic", reader),
            "reader must synchronize snapshot publication")
    require("lock_guard" in reader or "mutex" in reader,
            "reader snapshot publication must use the mutex/owned snapshot")
    require(re.search(r"s_snapshot\s*=|s_snapshot_mutex|get_snapshot", reader),
            "reader must publish and expose an owned snapshot")
    require(re.search(r"get_snapshot\s*\(", header) is not None,
            "reader header must expose the snapshot accessor")
    require("battery_status.h" in header,
            "reader header must include the shared battery contract")

    task = function_body(reader, "ina226_reader_task")
    require(re.search(r"(?:i2c|read|write)", task, re.IGNORECASE),
            "sampling task must perform the INA226 transaction")
    require("lv_" not in task.lower(), "INA226 task must not call LVGL")
    require("lvgl.h" not in reader.lower() and "bsp_display" not in reader.lower(),
            "reader must remain independent of LVGL/display locking")

    # REQ-BAT-004/REQ-BAT-005: reader failure is observable but never fatal,
    # and the reader remains a sensor-only INA226 task.  CHG_EN/CHG_STAT,
    # expander control, NVS, and protection policy are owned by the separate
    # battery-protection adapter, not by this reader.
    require("ina226_reader.cpp" in component,
            "component must register the INA226 reader source")
    require("battery_protection.cpp" in component,
            "component must register the battery-protection adapter")
    require("ina226_reader.h" in battery_api,
            "reader header must be part of the contract")
    require("battery_protection_get_snapshot" in protection_adapter_header,
            "adapter header must expose the snapshot consumed by the UI")
    require(re.search(r"\bina226_reader_init\s*\(", protection_adapter) is not None,
            "protection adapter must initialize the sensor reader")
    require(re.search(r"\bina226_reader_get_raw_sample\s*\(", protection_adapter) is not None,
            "protection adapter must consume the reader sensor sample")
    require(re.search(r"\bbattery_protection_get_snapshot\s*\(", ui) is not None,
            "UI must consume the battery-protection snapshot")
    require(re.search(r"\bina226_reader_(?:get_snapshot|get_raw_sample|started)\s*\(", ui) is None,
            "UI must not reintroduce a direct INA226 reader dependency")

    # app_main composes the adapter, which owns reader startup and translates
    # sensor data into the snapshot consumed by the UI.
    app_main = function_body(app, "app_main")
    start = re.search(r"\bbattery_protection_(?:start|init)\s*\(", app_main)
    require(start is not None, "app_main must compose battery protection startup")
    assert start is not None
    tail = app_main[start.end():]
    require(re.search(r"ESP_ERROR_CHECK\s*\(\s*battery_protection_", tail) is None,
            "battery protection startup must not be wrapped in ESP_ERROR_CHECK")
    require(re.search(r"ESP_LOG[EWI]\s*\(", tail),
            "battery protection startup failure must be logged")
    require(re.search(r"(?:tab5_keyboard|screenshot|wifi_mgr|bridge_start)", tail),
            "boot must continue to later services after protection startup failure")
    require(re.search(r"\bina226_reader_(?:start|init)\s*\(", app_main) is None,
            "app_main must compose the reader through the protection adapter")

    # These restrictions apply to the reader seam only.  The new protection
    # adapter is expected to contain the opposite (CHG/NVS) responsibilities.
    reader_sources = "\n".join((reader, header))
    for pattern in (r"\bCHG_EN\b", r"\bCHG_STAT\b", r"bsp_io_expander",
                    r"esp_io_expander", r"battery_protection",
                    r"\bnvs_(?:flash|open|set|commit)\b", r"\bnvs_"):
        require(re.search(pattern, reader_sources, re.IGNORECASE) is None,
                f"INA226 reader must remain sensor-only; found {pattern}")

    # Keep the host target and the REQ->TEST map discoverable.
    require("test_battery_reader_contract" in makefile,
            "Makefile must register the INA226 reader structural target")
    require("ina226_reader.cpp" in makefile,
            "Makefile must depend on the real INA226 reader source")
    for requirement in ("REQ-BAT-001", "REQ-BAT-002", "REQ-BAT-003",
                        "REQ-BAT-004", "REQ-BAT-005", "REQ-BAT-006"):
        require(requirement in codemap,
                f"code-map.md must document {requirement} traceability")
    for documentation in (architecture, architecture_pt_br):
        require("code-map.md" in documentation,
                "battery architecture docs must point to the traceability map")
        require("neutral" in documentation.lower(),
                "battery architecture docs must document the neutral state")
        for requirement in ("REQ-BAT-001", "REQ-BAT-002", "REQ-BAT-003",
                            "REQ-BAT-004", "REQ-BAT-005", "REQ-BAT-006"):
            require(requirement in documentation,
                    f"architecture docs must document {requirement}")

    for line in TRACEABILITY:
        print(f"TRACE {line}")
    print("PASS: INA226 reader voltage/state structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
