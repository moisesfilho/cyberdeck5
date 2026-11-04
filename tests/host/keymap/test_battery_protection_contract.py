#!/usr/bin/env python3
"""RED structural contract for the battery power/protection integration.

The pure policy is exercised by ``test_battery_protection.cpp``.  The expander,
INA226 adapter, NVS persistence, LVGL timer, shell, and serial bridge are not
host-linkable, so this test inspects the real sources instead.  It never opens
I2C, a simulator, Serial Automation Bridge, a display, or a power rail.
"""
from pathlib import Path
import re
import subprocess
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
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"
SHELL = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp"
SHELL_HEADER = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h"
SERIAL = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"
SERIAL_TEST = ROOT / "tests/host/keymap/test_serial_ndjson_dispatch.cpp"
APP = ROOT / "main/app_main.cpp"
COMPONENT = ROOT / "components/cyberdeck/CMakeLists.txt"
BSP_SRC_DIR = ROOT / "components/m5stack_tab5/src"
BSP_FEATURE_EN = BSP_SRC_DIR / "bsp_feature_en.cpp"
BSP_CMAKE = ROOT / "components/m5stack_tab5/CMakeLists.txt"
RULES_PATH = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"
README = ROOT / "README.md"
README_PT = ROOT / "README.pt-BR.md"
ARCHITECTURE = ROOT / "docs/ARCHITECTURE.md"
ARCHITECTURE_PT_BR = ROOT / "docs/ARCHITECTURE.pt-BR.md"

TRACEABILITY = (
    "REQ-BAT-007 -> expander-B pin6 CHG_STAT active-low and pin7 CHG_EN",
    "REQ-BAT-008 -> battery/external/charging/absent/unknown thresholds and votes",
    "REQ-BAT-009 -> terminal-voltage cut and latched 90 % release",
    "REQ-BAT-010 -> NVS default/option, UI timer/snapshot, shell and ui.type",
)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def scope_end(source: str, start: int) -> int | None:
    """Return the index of the brace that closes the scope enclosing `start`."""
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth < 0:
                return index
    return None


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


def version_components(text: str) -> tuple[int, int, int] | None:
    """Firmware version as a numeric tuple, or None when it is not MAJOR.MINOR.PATCH.

    Zero-padded segments ("01") are rejected because the firmware version is
    stored without padding; numeric comparison keeps 1.10.0 newer than 1.9.0.
    """
    match = re.fullmatch(r"([0-9]+)\.([0-9]+)\.([0-9]+)", text)
    if match is None:
        return None
    raw = match.groups()
    if any(part != str(int(part)) for part in raw):
        return None
    major, minor, patch = (int(part) for part in raw)
    return (major, minor, patch)


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
        "state_vote_count", "no_source_vote_count",
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
        (r"no_source_vote_count\s*=\s*5\b", "the latch release must require exactly five votes"),
        (r"protection_enter_voltage_mv\s*=\s*8200\b", "protection enter voltage must be exactly 8200 mV"),
        (r"protection_exit_percentage\s*=\s*90\b", "protection resume percentage must be exactly 90"),
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

    # TEST-SOC-32: the percentage is an estimate and no longer gates the cut,
    # and the estimate never releases the latch while the pack still classifies
    # as charging (releasing there would cut again on the next 8200 mV sample).
    for path, label in ((PURE_SOURCE, "policy source"),
                        (PURE_HEADER, "policy header"),
                        (PURE_CONTRACT, "host contract")):
        content = read(path, failures)
        require_all(failures, "protection_enter_percentage" not in content,
                    f"{label} must not keep protection_enter_percentage")
    release = function_body(source, "bool latch_release_requested()")
    require_all(failures, release is not None and
                "protection_exit_percentage" in release and
                "no_source_vote_count" in release,
                "the latch release must use the 90 % estimate and the five-vote rule")
    require_all(failures, release is not None and
                "current_state_ != battery_state::charging" in release,
                "the estimate must not release the latch while still charging")

    # TEST-SOC-34: the observation carries the elapsed interval, never a
    # percentage, and the adapter feeds the pure SoC estimator.
    observation = re.search(r"struct observation\s*\{(.*?)\n\};", header, re.S)
    require_all(failures, observation is not None and
                "elapsed_ms" in observation.group(1) and
                "percentage" not in observation.group(1),
                "the policy observation must carry elapsed_ms and no percentage")
    adapter_source = strip_comments(read(ADAPTER_SOURCE, failures))
    require_all(failures, "cyberdeck_battery_soc" not in adapter_source and
                "percentage_from_bus_voltage_mv" not in adapter_source,
                "the adapter must not derive a percentage from the terminal voltage")
    require_all(failures, "elapsed_ms" in adapter_source and
                "s_policy.observe" in adapter_source,
                "the adapter must feed the policy with the elapsed interval")
    require_all(failures, "s_last_valid_sample_us" in adapter_source and
                "esp_timer_get_time" in adapter_source,
                "elapsed time must be measured from the last valid sample")
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
        "cyberdeck_expander_device_init", "IO_EXPANDER_PIN_NUM_6", "IO_EXPANDER_PIN_NUM_7",
        "CHG_STAT", "CHG_EN", "esp_io_expander_get_level", "esp_io_expander_set_dir",
        "esp_io_expander_set_level", "esp_io_expander_set_output_mode",
        "IO_EXPANDER_OUTPUT_MODE_PUSH_PULL", "IO_EXPANDER_INPUT", "IO_EXPANDER_OUTPUT",
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
        require_all(failures, re.search(r"\binit_expander\s*\(\s*\)", init_body)
                    is not None,
                    "battery protection startup must reach expander initialization")

    # Keep the real initialization path explicit: the high write must be
    # reached from init_expander(), rather than a dead constant or an
    # unrelated test-only/table value.
    expander_init = function_body(source, "init_expander(")
    require_all(failures, expander_init is not None,
                "protection adapter must expose the expander initialization path")
    if expander_init is not None:
        require_all(failures, "s_expander_configured" in expander_init,
                    "expander setup must track completion separately from handle ownership")
        require_all(failures, re.search(
            r"if\s*\(\s*s_expander_configured\s*\)\s*\{\s*return\s+ESP_OK\s*;",
            expander_init) is not None,
            "only fully configured expander setup may take the idempotent fast path")
        require_all(failures, "s_expander = cyberdeck_expander_device_handle()" in expander_init and
                    "s_expander = nullptr" not in expander_init,
                    "retry must retain an acquired handle without recreating or clearing it")
        for pattern, stage in (
            (r"esp_io_expander_set_dir\s*\(\s*s_expander\s*,\s*IO_EXPANDER_PIN_NUM_6\s*,\s*IO_EXPANDER_INPUT\s*\)", "CHG_STAT input direction"),
            (r"esp_io_expander_set_pullupdown\s*\(\s*s_expander\s*,\s*IO_EXPANDER_PIN_NUM_6\s*,\s*IO_EXPANDER_PULL_UP\s*\)", "CHG_STAT pull-up"),
        ):
            call = re.search(pattern, expander_init)
            require_all(failures, call is not None,
                        f"setup must include {stage}")
            if call is not None:
                following = expander_init[call.end():]
                next_call = re.search(r"(?:esp_io_expander_set_(?:dir|level|output_mode|pullupdown)|"
                                      r"write_chg_enable)\s*\(", following)
                stage_body = following[:next_call.start()] if next_call else following
                require_all(failures, re.search(
                    r"if\s*\(\s*err\s*!=\s*ESP_OK\s*\)\s*\{[^}]*return\s+err\s*;",
                    stage_body) is not None,
                    f"{stage} failure must abort initialization")
        require_all(failures, re.search(r"write_chg_enable\s*\(\s*true\s*\)", expander_init)
                    is not None,
                    "init_expander must actively drive CHG_EN high")
        stages = (
            (r"esp_io_expander_set_level\s*\(\s*s_expander\s*,\s*IO_EXPANDER_PIN_NUM_5\s*,\s*1\s*\)", "QC inactive-high preload"),
            (r"esp_io_expander_set_level\s*\(\s*s_expander\s*,\s*IO_EXPANDER_PIN_NUM_7\s*,\s*0\s*\)", "CHG_EN inactive-low preload"),
            (r"esp_io_expander_set_dir\s*\(\s*s_expander\s*,\s*IO_EXPANDER_PIN_NUM_5\s*,\s*IO_EXPANDER_OUTPUT\s*\)", "QC output direction"),
            (r"esp_io_expander_set_output_mode\s*\(\s*s_expander\s*,\s*IO_EXPANDER_PIN_NUM_5\s*,\s*IO_EXPANDER_OUTPUT_MODE_PUSH_PULL\s*\)", "QC push-pull mode"),
            (r"esp_io_expander_set_dir\s*\(\s*s_expander\s*,\s*IO_EXPANDER_PIN_NUM_7\s*,\s*IO_EXPANDER_OUTPUT\s*\)", "CHG_EN output direction"),
            (r"esp_io_expander_set_output_mode\s*\(\s*s_expander\s*,\s*IO_EXPANDER_PIN_NUM_7\s*,\s*IO_EXPANDER_OUTPUT_MODE_PUSH_PULL\s*\)", "CHG_EN push-pull mode"),
            (r"esp_io_expander_set_level\s*\(\s*s_expander\s*,\s*IO_EXPANDER_PIN_NUM_5\s*,\s*0\s*\)", "QC enable-low write"),
            (r"write_chg_enable\s*\(\s*true\s*\)", "CHG_EN enable-high write"),
        )
        stage_matches = []
        for pattern, stage in stages:
            call = re.search(pattern, expander_init)
            require_all(failures, call is not None,
                        f"setup must include {stage}")
            if call is not None:
                stage_matches.append((call.start(), call.end(), stage))
                following = expander_init[call.end():]
                next_call = re.search(r"(?:esp_io_expander_set_(?:dir|level|output_mode)|"
                                      r"write_chg_enable)\s*\(", following)
                stage_body = following[:next_call.start()] if next_call else following
                require_all(failures, re.search(
                    r"if\s*\(\s*err\s*!=\s*ESP_OK\s*\)\s*\{[^}]*return\s+err\s*;",
                    stage_body) is not None,
                    f"{stage} failure must return before later setup can enable P7")
        require_all(failures,
                    [item[0] for item in stage_matches] == sorted(item[0] for item in stage_matches),
                    "safe preloads, push-pull setup, QC enable, and P7 enable must remain ordered")
        chg_enable = re.search(r"write_chg_enable\s*\(\s*true\s*\)", expander_init)
        configured = re.search(r"s_expander_configured\s*=\s*true\s*;", expander_init)
        require_all(failures, configured is not None and
                    chg_enable is not None and chg_enable.start() < configured.start(),
                    "setup completion must be published only after QC and CHG_EN succeed")
        # Each failed stage, including both safe-level preloads and output-mode
        # writes, must abort before P7 can be asserted. Incomplete setup remains
        # retryable and retains the already acquired expander handle.
        require_all(failures, re.search(
            r"if\s*\(\s*s_expander\s*==\s*nullptr\s*\)\s*\{\s*"
            r"const int64_t.*?cyberdeck_expander_device_init\(\).*?s_expander\s*=\s*cyberdeck_expander_device_handle\(\)",
            expander_init, re.S) is not None,
            "retry with retained expander handle must not recreate it")
        require_all(failures, "s_last_applied_charger_state" in source and
                    "s_policy" in source,
                    "failed setup must not overwrite the last applied charger/policy state")
    require_all(failures, "k_chg_en_init" not in source,
                "dead k_chg_en_init literal must not remain in the adapter")

    # TEST-EXP-REENTRANCY: the broker mutex is not recursive. Keeping the setup
    # lease alive across write_chg_enable() makes the nested acquisition fail
    # (EBUSY -> ESP_ERR_TIMEOUT), so init would return an error with CHG_EN still
    # at the 0 preload and the protection task would never start.
    lease = expander_init.find("cyberdeck_expander_broker::lease lease(")
    lease_scope_end = None if lease < 0 else scope_end(expander_init, lease)
    chg_enable_call = expander_init.find("write_chg_enable(true)")
    require_all(failures, lease >= 0 and chg_enable_call >= 0 and lease_scope_end is not None and
                chg_enable_call > lease_scope_end,
                "write_chg_enable must run after the setup lease ends (no nested lease)")
    require_all(failures, expander_init.count("cyberdeck_expander_broker::lease lease(") == 1 and
                chg_enable_call == expander_init.rfind("write_chg_enable(true)") and
                expander_init.count("write_chg_enable(true)") == 1,
                "expander setup must hold exactly one lease and drive CHG_EN exactly once")

    # TEST-EXP-CACHE: the confirmed-level cache starts low (matching the P7=0
    # preload) and only write_chg_enable() moves it, after a successful write.
    # With a stale cache the next sample retries instead of assuming success.
    require_all(failures, re.search(r"bool s_last_applied_charger_state = false;", source) is not None,
                "confirmed CHG_EN cache must start at the P7=0 preload level")
    write_body = function_body(source, "esp_err_t write_chg_enable(")
    if write_body is None:
        require_all(failures, False, "write_chg_enable definition must be inspectable")
    else:
        assigned = write_body.find("s_last_applied_charger_state = enable;")
        failure_guard = write_body.find("return err;")
        require_all(failures, assigned >= 0 and failure_guard >= 0 and failure_guard < assigned,
                    "a failed CHG_EN write must return before the cache is updated")
        require_all(failures, re.search(r"s_last_applied_charger_state = enable;\s*return ESP_OK;",
                                        write_body) is not None,
                    "only a successful CHG_EN write may confirm the cached level")
    require_all(failures, "s_last_applied_charger_state = charger_now" not in source and
                "s_last_applied_charger_state = policy_snap" not in source and
                source.count("s_last_applied_charger_state = enable;") == 1,
                "write_chg_enable must be the only writer of the confirmed CHG_EN level")

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

    refresh = function_body(ui, "void refresh_battery_status(")
    require_all(failures, refresh is not None,
                "UI battery refresh seam is missing")
    if refresh is not None:
        policy_reads = re.findall(r"battery_protection_get_policy_snapshot\s*\(", refresh)
        require_all(failures, len(policy_reads) == 1,
                    "battery refresh must consume one copied policy snapshot")
        require_all(failures, "battery_protection_get_snapshot" not in refresh,
                    "battery refresh must not use the live shell snapshot getter")
        require_all(failures, "state_name" not in refresh and
                    "charge_signal_name" not in refresh,
                    "battery refresh must not duplicate status-name tables")

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

    # The command switch moved to the extracted session, reached through the
    # foreground shell application that owns the console.
    session = strip_comments(read(SESSION, failures))
    require_all(failures, "s_shell_app.handle_key(" in ui,
                "UI must route keys through the shell application")
    local_key = function_body(ui, "void local_key(uint32_t key)")
    require_all(failures, local_key is not None and "s_shell_app.handle_key(" in local_key,
                "the local key facade must eventually delegate to the shell application")
    execute = function_body(session, "void session::execute_line(")
    require_all(failures, execute is not None,
                "session command dispatch seam is missing")
    if execute is not None:
        for command in ("battery protection on", "battery protection off",
                         "battery protection status"):
            require_all(failures, command in execute,
                        f"session dispatch must expose {command}")
        require_all(failures, "battery_protection" in execute,
                    "battery commands must call the protection adapter")
        require_all(failures, "i2c_" not in execute and "nvs_" not in execute,
                    "command dispatch must not perform I2C/NVS directly")

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
    for token in (
        "test_battery_protection.cpp", "test_battery_protection_contract.py",
        "cyberdeck_battery_protection.cpp", "CHG_STAT", "CHG_EN",
        "7900", "8330", "8200", "90", "5 votos", "battery protection",
        "CHG_QC_EN", "REQ-CHG-QC-01", "AC-CHG-QC-01..04",
        "TEST-CHG-QC-01..07", "cyberdeck_battery_soc", "TEST-SOC-",
    ):
        require_all(failures, token in codemap,
                    f"code-map.md must trace battery protection item {token}")
    for target in ("test_battery_protection:", "test_battery_protection_contract:"):
        require_all(failures, target in makefile,
                    f"Makefile must register {target}")
    require_all(failures, "test_battery_protection.cpp" in makefile and
                "test_battery_protection_contract.py" in makefile,
                "Makefile must depend on both battery protection tests")
    # Assert the real ignore behavior; --no-index is required because an
    # already-tracked file is never reported as ignored.
    for source in ("test_battery_protection.cpp", "test_battery_protection_contract.py"):
        visible = subprocess.run(
            ["git", "check-ignore", "-q", "--no-index", f"tests/host/keymap/{source}"],
            cwd=str(ROOT), capture_output=True).returncode
        require_all(failures, visible != 0,
                    f"gitignore must keep tests/host/keymap/{source} trackable")
    for artifact in ("test_battery_protection", "test_battery_protection_contract"):
        ignored = subprocess.run(
            ["git", "check-ignore", "-q", "--no-index", f"tests/host/keymap/{artifact}"],
            cwd=str(ROOT), capture_output=True).returncode
        require_all(failures, ignored == 0,
                    f"gitignore must keep the compiled {artifact} ignored")

    for path in (ARCHITECTURE, ARCHITECTURE_PT_BR):
        documentation = read(path, failures)
        require_all(failures, "code-map.md" in documentation,
                    f"{path.name} must point to the battery traceability map")
        for token in ("CHG_STAT", "CHG_EN", "battery protection", "7900", "8330",
                      "8200", "90", "SoC"):
            require_all(failures, token in documentation,
                        f"{path.name} must document {token}")

    # The documentation gate: every current-behaviour file must state the 90 %
    # latch release, not only carry the string "90" somewhere.  "85" stays
    # allowed as an explicit historical reference (85 % of the previous
    # 6000..8230 mV window = 7895,5 mV); this check fails when a file only
    # asserts the old release.
    release_line = re.compile(r"(?i)(latch|libera\w*|release|resume)")
    for path in (README, README_PT, CODEMAP, ARCHITECTURE, ARCHITECTURE_PT_BR):
        documentation = read(path, failures)
        states_90 = any(release_line.search(line) and "90" in line
                        for line in documentation.splitlines())
        require_all(failures, states_90,
                    f"{path.name} must state the current 90 % latch release "
                    "(a line matching latch/libera/release/resume that contains "
                    "\"90\"); only the historical 85 % may remain otherwise")

    # The version must keep the MAJOR.MINOR.PATCH shape without zero padding and
    # never fall below the release that introduced this feature; a later
    # MINOR/PATCH bump must keep the contract green (numeric, not string order).
    version = read(ROOT / "version.txt", failures).strip()
    version_numbers = version_components(version)
    require_all(failures, version_numbers is not None and version_numbers >= (1, 3, 0),
                "firmware version must be at least 1.3.0 with MAJOR.MINOR.PATCH format, "
                f"got {version!r}")
    adapter = strip_comments(read(ADAPTER_SOURCE, failures))
    cached = function_body(adapter, "battery_protection_get_cached_policy_snapshot(")
    require_all(failures, cached is not None and
                "s_policy.snapshot()" in cached and "s_policy.observe(" not in cached and
                "sample_and_publish" not in cached and "apply_charger_state" not in cached and
                "write_chg_enable" not in cached,
                "shell status policy route must only copy the cached policy snapshot")
    diag = function_body(adapter, "battery_protection_get_hardware_diagnostics(")
    require_all(failures, diag is not None and
                "0x03" in diag and "0x05" in diag and "0x07" in diag and
                "0x13" not in diag and "0x0f" not in diag.lower() and
                "i2c_master_transmit_receive" in diag and "i2c_master_transmit(" not in diag and
                "ina226_reader_get_raw_diagnostics" in diag and
                "esp_io_expander_set_" not in diag and "write_chg_enable" not in diag,
                "read-only diagnostics must read 03/05/07 (never 0x0F or 0x13) and write nothing")
    status_text = SESSION.read_text(encoding="utf-8")
    require_all(failures, "battery hardware:" in status_text and
                "CHG_QC_EN_latch=" in status_text and "bus_raw[" in status_text and
                'ina_age_ms=%" PRId64' in status_text and
                'stat_age_ms=%" PRId64' in status_text and
                'snapshot_age_ms=%" PRId64' in status_text and
                "CHG_STAT_raw=%s" in status_text and
                "latch_is_not_pad_voltage" in status_text and
                "input_status_outputs_low" in status_text and
                "sequential_register_sample" in status_text,
                "status must expose cached read-only evidence, ages and explicit limits")
    require_all(failures, "out_diagnostics->sampled_at_us = esp_timer_get_time()" in
                read(READER_SOURCE, failures) and "s_cached_chg_stat_valid = chg_valid" in
                read(ADAPTER_SOURCE, failures),
                "diagnostics must timestamp cached INA and CHG_STAT samples")
    ui = strip_comments(read(UI, failures))
    status_host = function_body(ui, "bool shell_session_host::battery_protection_snapshot(")
    require_all(failures, status_host is not None and
                "battery_protection_get_cached_policy_snapshot" in status_host and
                "battery_protection_get_policy_snapshot" not in status_host,
                "battery status adapter must route to the non-sampling cache getter")
    broker_header = read(INCLUDE_SENSOR_DIR / "cyberdeck_expander_broker.h", failures)
    device_source = strip_comments(read(SENSOR_DIR / "cyberdeck_expander_device.cpp", failures))
    poweroff = strip_comments(read(ROOT / "components/cyberdeck/src/platform/power/cyberdeck_poweroff.cpp", failures))
    require_all(failures, "std::timed_mutex" in broker_header and "try_lock_for" in broker_header,
                "shared I2C broker lease must be exclusive and bounded")
    # Creating a driver handle calls the vendor reset() (0xFF to
    # PI4IO_REG_CHIP_RESET), which clears direction/latch/high-Z and returns
    # CHG_STAT/CHG_QC_EN/CHG_EN/WIFI_EN to their power-on defaults. Expander B
    # must therefore have exactly one driver instance for the whole boot: the
    # BSP singleton that bsp_feature_enable(WIFI/USB) also uses.
    require_all(failures, "bsp_io_expander1_init" in device_source and
                "esp_io_expander_new_i2c_pi4ioe5v6408" not in device_source and
                device_source.count("bsp_io_expander1_init()") == 1,
                "app-level expander owner must reuse the single BSP driver singleton")
    require_all(failures, "i2c_master_bus_add_device" in device_source and
                "BSP_IO_EXPANDER_ADDRESS_1" in device_source and
                "esp_io_expander_set_" not in device_source and
                "esp_io_expander_get_" not in device_source,
                "broker device owner must keep a raw read handle only, at the BSP address")
    require_all(failures, "cyberdeck_expander_broker::lease" in device_source,
                "the device owner must create the driver singleton under the broker lease")
    # The one-instance invariant lives in the BSP singleton. Without the guard a
    # later bsp_feature_enable(WIFI/USB), battery retry or poweroff would create a
    # second driver and re-run the vendor chip reset (0xFF to 0x01) at runtime,
    # wiping CHG_STAT direction and the CHG_QC_EN/CHG_EN/WIFI_EN latches.
    bsp_expander_source = strip_comments(read(BSP_SRC_DIR / "bsp_io_expander.c", failures))
    singleton = function_body(bsp_expander_source, "esp_io_expander_handle_t bsp_io_expander1_init(void)")
    guard = None if singleton is None else re.search(
        r"if\s*\(\s*io_expander1\s*\)\s*\{\s*return\s+io_expander1\s*;", singleton)
    create = None if singleton is None else singleton.find("esp_io_expander_new_i2c_pi4ioe5v6408")
    require_all(failures, guard is not None and create > guard.end(),
                "expander-B driver must return the cached BSP singleton before creating one")
    require_all(failures, "static esp_io_expander_handle_t io_expander1" in bsp_expander_source,
                "the BSP singleton handle must stay file-static")
    unleased = []
    for directory in (ROOT / "components/cyberdeck/src", BSP_SRC_DIR):
        for path in sorted(directory.rglob("*")):
            if path.suffix not in {".c", ".cpp"} or path.name in {
                    "bsp_io_expander.c", "bsp_feature_en.cpp", "cyberdeck_expander_device.cpp"}:
                continue
            if "bsp_io_expander1_init()" in strip_comments(
                    path.read_text(encoding="utf-8", errors="ignore")):
                unleased.append(path.name)
    require_all(failures, not unleased,
                f"only the guarded singleton and its known lease holders may initialize expander B: {unleased}")
    require_all(failures, "cyberdeck_expander_broker::lease" in adapter and
                "cyberdeck_expander_broker::lease" in poweroff,
                "battery and poweroff transactions must use the shared I2C broker")
    feature_en = strip_comments(read(BSP_FEATURE_EN, failures))
    wifi_case = feature_en[feature_en.find("case BSP_FEATURE_WIFI"):]
    usb_case = feature_en[feature_en.find("case BSP_FEATURE_USB"):feature_en.find("case BSP_FEATURE_WIFI")]
    require_all(failures, feature_en.find("case BSP_FEATURE_WIFI") >= 0 and
                feature_en.find("case BSP_FEATURE_USB") >= 0,
                "BSP feature switch must keep its WIFI and USB cases")
    require_all(failures, "cyberdeck_expander_broker::lease" in wifi_case and
                "cyberdeck_expander_broker::lease" in usb_case,
                "BSP expander-B feature transactions must hold the shared broker lease")
    require_all(failures,
                wifi_case.find("cyberdeck_expander_broker::lease") < wifi_case.find("esp_io_expander_set_") and
                usb_case.find("cyberdeck_expander_broker::lease") < usb_case.find("esp_io_expander_set_"),
                "the broker lease must be acquired before any expander-B register write")
    require_all(failures, "src/bsp_feature_en.cpp" in read(BSP_CMAKE, failures) and
                not (BSP_SRC_DIR / "bsp_feature_en.c").exists(),
                "the lease-carrying BSP feature file must be built as C++")
    require_all(failures, "constexpr uint8_t regs[] = {0x03, 0x05, 0x07}" in adapter and
                "0x13" not in diag and "i2c_master_transmit_receive" in diag,
                "diagnostics must read only 03/05/07 and never clear-on-read 13")


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
