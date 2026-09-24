#!/usr/bin/env python3
"""RED structural contract for the INA226 battery reader.

The reader is an ESP-IDF/FreeRTOS integration and is intentionally not linked
on the host.  This test inspects the real source/header seam and fixes the
approved integration invariants without faking I2C, a simulator, or hardware.

Required production seam:
  * INA226 address 0x41 on the I2C bus;
  * configuration and calibration are written during initialization;
  * sampling runs in a dedicated task with a one-second cadence;
  * a copied/owned snapshot is published for the UI;
  * no CHG_EN/CHG_STAT, NVS, or battery-protection policy is introduced;
  * the sampling task does not call I2C or LVGL from an LVGL timer.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
READER = ROOT / "components/cyberdeck/src/platform/sensors/ina226_reader.cpp"
HEADER = ROOT / "components/cyberdeck/include/platform/sensors/ina226_reader.h"
BATTERY_STATUS = ROOT / "components/cyberdeck/include/platform/sensors/battery_status.h"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
APP = ROOT / "main/app_main.cpp"
COMPONENT = ROOT / "components/cyberdeck/CMakeLists.txt"
MAKEFILE_PATH = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"


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
    require(READER.exists(), f"INA226 reader implementation is missing: {READER}")
    require(HEADER.exists(), f"INA226 reader public header is missing: {HEADER}")
    require(BATTERY_STATUS.exists(), f"battery status header is missing: {BATTERY_STATUS}")
    reader = strip_comments(READER.read_text(encoding="utf-8"))
    header = strip_comments(HEADER.read_text(encoding="utf-8"))
    battery_status = strip_comments(BATTERY_STATUS.read_text(encoding="utf-8"))
    ui = strip_comments(UI.read_text(encoding="utf-8"))
    app = strip_comments(APP.read_text(encoding="utf-8"))
    component = strip_comments(COMPONENT.read_text(encoding="utf-8"))
    makefile = strip_comments(MAKEFILE_PATH.read_text(encoding="utf-8"))
    codemap = strip_comments(CODEMAP.read_text(encoding="utf-8"))

    combined = reader + "\n" + header
    # The address is part of the hardware contract, not a configurable default.
    require(re.search(r"(?:0[xX]41\b|INA226.*(?:I2C_)?(?:ADDRESS|ADDR))", combined, re.I),
            "reader must use INA226 I2C address 0x41")
    require("INA226" in combined.upper(), "reader/header must identify the INA226 device")

    # Configuration and calibration must be explicit writes, not merely comments
    # or a generic I2C probe.  Register names and write calls are both required.
    require(re.search(r"\bCONFIG\b|CONFIG_REG|CONFIGURATION", combined, re.I),
            "reader must define/use INA226 configuration")
    require(re.search(r"(?:CALIBRATION_(?:REGISTER|VALUE)|CALIB_REG)", combined, re.I),
            "reader must define/use INA226 calibration")
    require(re.search(r"(?:write|writ|send|write_reg|i2c_write)", reader, re.I),
            "reader must write configuration/calibration registers")
    init = function_body(reader, "ina226_reader_init")
    require(re.search(r"config", init, re.I) and re.search(r"calib", init, re.I),
            "initialization must contain both configuration and calibration")
    require(re.search(r"(?:write|writ|i2c)", init, re.I),
            "initialization must perform register writes")

    # A dedicated task and one-second cadence are required.  Accept the common
    # FreeRTOS spellings, but reject an LVGL timer as the sampling mechanism.
    require("xTaskCreate" in reader, "reader must create a dedicated task")
    require("vTaskDelay" in reader, "reader task must pace its samples with vTaskDelay")
    require(re.search(r"pdMS_TO_TICKS\s*\(\s*1000\s*\)|1000\s*\)", reader),
            "reader task must sample every 1000 ms")
    require("lv_timer_create" not in reader,
            "battery sampling must not be an LVGL timer")
    require("vTaskDelay(pdMS_TO_TICKS(1000))" in reader or
            re.search(r"vTaskDelay\s*\(\s*pdMS_TO_TICKS\s*\(\s*1000\s*\)", reader),
            "reader must use an explicit one-second task delay")

    # Snapshot publication is the only UI-facing result seam.
    require(re.search(r"snapshot", combined, re.I), "reader must expose a battery snapshot")
    require(re.search(r"std::mutex|portMUX|QueueHandle_t|std::atomic", combined),
            "reader snapshot publication must have synchronization")
    require(re.search(r"get_snapshot|snapshot\s*\(", reader),
            "reader must provide a snapshot accessor")
    require("battery_status.h" in header,
            "reader header must expose the shared battery snapshot contract")
    require(re.search(r"percent|percentage|capacity", battery_status, re.I),
            "snapshot must carry percentage/capacity data")

    # Forbidden hardware features and persistence/policy layers are explicit
    # non-goals for this minimal reader.
    for forbidden in ("CHG_EN", "CHG_STAT", "nvs_", "nvs_flash", "battery_protection",
                      "charge_enable", "charge_status", "protection_policy"):
        require(forbidden.lower() not in combined.lower(),
                f"reader must not introduce forbidden feature: {forbidden}")

    # The task body is the sampling boundary.  I2C is allowed there, but not in
    # an LVGL timer callback; this explicitly checks the timer/task separation.
    task = function_body(reader, "ina226_reader_task")
    require(re.search(r"(?:i2c|read|write)", task, re.I),
            "sampling task must perform the INA226 transaction")
    require("lv_" not in task.lower(), "INA226 task must not call LVGL")
    timer_blocks = re.findall(r"lv_timer_create\s*\([^;]*\);", reader)
    require(not timer_blocks, "reader must not register an LVGL sampling timer")
    require("lvgl.h" not in reader.lower() and "bsp_display" not in reader.lower(),
            "reader must remain independent of LVGL/display locking")

    # The reader must be composed in the real component/UI boot path, while a
    # reader failure must not make app_main fatal.
    require("ina226_reader.cpp" in component, "component must register INA226 reader source")
    require("ina226_reader.h" in combined, "reader header must be part of the contract")
    require("ina226_reader_start" in ui or "ina226_reader_init" in ui,
            "UI/boot composition must initialize the reader")
    require(re.search(r"ina226_reader_(?:start|init)\s*\([^;]*\)\s*;?", app),
            "app_main must explicitly compose reader startup")
    start = next((m for m in re.finditer(r"ina226_reader_(?:start|init)\s*\(", app)), None)
    require(start is not None, "app_main must contain reader startup")
    if start is not None:
        tail = app[start.start():]
        require(not re.search(r"ESP_ERROR_CHECK\s*\(\s*ina226_reader_", tail),
                "INA226 reader startup must be non-fatal to app boot")
        require(re.search(r"ESP_LOG[EWI]\s*\(", tail),
                "INA226 reader startup failure must be logged")

    # Keep the host target discoverable from both the Makefile and map.
    require("test_battery_reader_contract" in makefile,
            "Makefile must register the INA226 reader structural target")
    require("ina226_reader.cpp" in makefile,
            "Makefile must depend on the real INA226 reader source")
    require("ina226_reader.cpp" in codemap and "test_battery_reader_contract" in codemap,
            "code-map.md must document the INA226 reader seam and test")

    print("PASS: INA226 reader structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
