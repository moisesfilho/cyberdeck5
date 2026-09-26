#!/usr/bin/env python3
"""RED structural contract for the BLE feature (REQ-BLE-001..011 / AC-BLE-001..011).

The BLE radio lives on the ESP32-C6 coprocessor reached through esp_hosted, and
the LVGL/NVS/FATFS/BSP adapters are not host-linkable.  The pure host contracts
(``test_ble_types``, ``test_ble_state_machine``, ``test_ble_event_dispatch``,
``test_ble_store``, ``test_ble_command_parse``) pin the logic; this contract
inspects the real firmware sources for composition, routing, threading and
scope boundaries.

The approved host is **NimBLE** on the P4 driving the C6 controller over the
ESP-Hosted HCI VHCI transport (``CONFIG_BT_NIMBLE_ENABLED=y``,
``CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y``, ``CONFIG_BT_BLUEDROID_ENABLED=n``), so
every stack check below names the real NimBLE host entry points and rejects the
Bluedroid-only ones.  Composition checks describe the *public bounded* adapter
surface (bounded queue + observer) and the invariants around it, never the
adapter's private helper names.

It never opens hardware, a simulator, a display, a radio or the Serial
Automation Bridge: it only reads files.  Every check fails closed, so this
target stays RED until the coder finishes the feature.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]

BLE_DIR = ROOT / "components/cyberdeck/src/features/bluetooth"
BLE_INCLUDE = ROOT / "components/cyberdeck/include/features/bluetooth"
BLE_TYPES_HDR = BLE_INCLUDE / "cyberdeck_ble_types.h"
BLE_TYPES_SRC = BLE_DIR / "cyberdeck_ble_types.cpp"
BLE_STATE_HDR = BLE_INCLUDE / "cyberdeck_ble_state_machine.h"
BLE_STATE_SRC = BLE_DIR / "cyberdeck_ble_state_machine.cpp"
BLE_EVENTS_HDR = BLE_INCLUDE / "cyberdeck_ble_event_dispatch.h"
BLE_EVENTS_SRC = BLE_DIR / "cyberdeck_ble_event_dispatch.cpp"
BLE_STORE_HDR = BLE_INCLUDE / "cyberdeck_ble_store.h"
BLE_STORE_SRC = BLE_DIR / "cyberdeck_ble_store.cpp"
# The ESP-IDF adapter: the only module allowed to talk to the coprocessor.
BLE_MGR_HDR = BLE_INCLUDE / "ble_mgr.h"
BLE_MGR_SRC = BLE_DIR / "ble_mgr.cpp"

SHELL_UTILS_HDR = ROOT / "components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h"
SHELL_UTILS = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp"
SHELL_HELP = ROOT / "components/cyberdeck/include/features/shell/cyberdeck_shell_help.h"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
UI_HDR = ROOT / "components/cyberdeck/include/platform/display/cyberdeck_ui.h"
SERIAL = ROOT / "components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp"
SERIAL_TEST = ROOT / "tests/host/keymap/test_serial_ndjson_dispatch.cpp"
APP = ROOT / "main/app_main.cpp"
COMPONENT = ROOT / "components/cyberdeck/CMakeLists.txt"
COMPONENT_YML = ROOT / "main/idf_component.yml"
SDKCONFIG_DEFAULTS = ROOT / "sdkconfig.defaults"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
GITIGNORE = ROOT / ".gitignore"
CODE_MAP = ROOT / "code-map.md"
ARCH_EN = ROOT / "docs/ARCHITECTURE.md"
ARCH_PT = ROOT / "docs/ARCHITECTURE.pt-BR.md"
README_EN = ROOT / "README.md"
README_PT = ROOT / "README.pt-BR.md"
HELP_FIXTURE = ROOT / "tests/host/keymap/contracts/cyberdeck_help.h"
CONTRACT_TYPES = ROOT / "tests/host/keymap/contracts/cyberdeck_ble_types.h"
CONTRACT_STATE = ROOT / "tests/host/keymap/contracts/cyberdeck_ble_state_machine.h"
CONTRACT_EVENTS = ROOT / "tests/host/keymap/contracts/cyberdeck_ble_event_dispatch.h"
CONTRACT_STORE = ROOT / "tests/host/keymap/contracts/cyberdeck_ble_store.h"

HOST_TEST_FILES = (
    "test_ble_types.cpp",
    "test_ble_state_machine.cpp",
    "test_ble_event_dispatch.cpp",
    "test_ble_store.cpp",
    "test_ble_command_parse.cpp",
    "test_ble_integration_contract.py",
)

# REQ-BLE-011: Bluetooth Classic (BR/EDR) is out of scope.  These tokens would
# only appear if the implementation pulled in a classic transport or profile.
CLASSIC_TOKENS = (
    "esp_bt_btdev",
    "esp_bt_avrc",
    "esp_avrc_api",
    "esp_bt_spp",
    "esp_bt_hci",
    "bt_hcic",
    "esp_bt_dev.h",
    "ESP_BT_MODE_CLASSIC",
    "esp_a2dp",
    "esp_avdt",
    "btclassic",
    "bluetooth_classic",
    "BT_CLASSIC",
)

# The pure host modules must never reach into the stack, the RTOS or the UI.
FORBIDDEN_IN_PURE = (
    "esp_bt",
    "nimble",
    "bluedroid",
    "esp_hosted",
    "lvgl.h",
    "bsp/esp-bsp.h",
    "freertos",
    "xTaskCreate",
    "vTaskDelay",
    "nvs.h",
    "nvs_flash.h",
    "fatfs",
    "sdmmc",
    "esp_timer",
    "esp_log.h",
    "esp_err.h",
)

# A secret may never reach a log line, a terminal line or the persisted store.
SECRET_TOKENS = (
    "passkey",
    "pass_key",
    "link_key",
    "linkkey",
    "ltk",
    "irk",
    "csrk",
    "mitm_protection",
    "btdesc",
)

# REQ-BLE-001: the approved host is NimBLE (``CONFIG_BT_BLUEDROID_ENABLED=n``).
# Bluedroid-only entry points must never appear anywhere in the feature, and the
# adapter must prove it drives the real NimBLE host over the ESP-Hosted link.
BLUEDROID_TOKENS = (
    "esp_ble_gap",
    "esp_ble_gattc",
    "esp_ble_hci",
    "esp_gap_ble",
    "esp_bt_controller",
    "esp_bt_device",
    "esp_bt.h",
)

# The NimBLE host entry points the one adapter module must own (REQ-BLE-001).
NIMBLE_STACK_TOKENS = (
    "nimble_port",
    "host/ble_hs.h",
    "ble_hs_cfg",
    "ble_gap_disc",
    "ble_gap_connect",
)

# The ESP-Hosted transport that carries the C6 radio (REQ-BLE-001).
HOSTED_LINK_TOKENS = ("esp_hosted",)

# REQ-BLE-003/008/010: the only adapter surface the UI may touch is the bounded
# public queue plus the observer callback.  Any other ``ble_mgr_`` symbol in the
# LVGL task would be a private helper or a blocking call, so the rule is written
# as a positive allow-list instead of pinning private helper names.
APPROVED_UI_BLE_API = frozenset((
    "ble_mgr_start",
    "ble_mgr_stop",
    "ble_mgr_enqueue_cmd",
    "ble_mgr_register_observer",
    "ble_mgr_unregister_observer",
    "ble_mgr_cmd_t",
    "ble_mgr_cmd_kind_t",
    "ble_mgr_event_t",
    "ble_mgr_evt_kind_t",
    "ble_mgr_observer_cb_t",
    "ble_mgr_observer_handle_t",
))


class Report:
    def __init__(self) -> None:
        self.failures: list[str] = []

    def require(self, condition: bool, message: str) -> bool:
        if not condition:
            self.failures.append(message)
        return condition

    def read(self, path: Path) -> str:
        if not self.require(path.is_file(), f"missing required file: {rel(path)}"):
            return ""
        try:
            return path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            self.require(False, f"cannot read {rel(path)}: {error}")
            return ""


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source: str, signature: str, report: Report) -> str:
    start = 0
    while True:
        marker = source.find(signature, start)
        if marker < 0:
            report.require(False, f"missing function containing {signature!r}")
            return ""
        opening = source.find("{", marker)
        semicolon = source.find(";", marker)
        if semicolon >= 0 and semicolon < opening:
            start = semicolon + 1
            continue
        if opening < 0:
            report.require(False, f"missing body for {signature!r}")
            return ""
        depth = 0
        for index in range(opening, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[opening + 1:index]
        report.require(False, f"unterminated function {signature!r}")
        return ""


def check_pure_modules_are_stack_free(report: Report) -> None:
    """REQ-BLE-010: the host-testable logic must stay off the stack/RTOS/UI."""
    modules = {
        "cyberdeck_ble_types.cpp": BLE_TYPES_SRC,
        "cyberdeck_ble_state_machine.cpp": BLE_STATE_SRC,
        "cyberdeck_ble_event_dispatch.cpp": BLE_EVENTS_SRC,
        "cyberdeck_ble_store.cpp": BLE_STORE_SRC,
    }
    for name, path in modules.items():
        source = strip_comments(report.read(path))
        if not source:
            continue
        for token in FORBIDDEN_IN_PURE:
            report.require(
                token not in source,
                f"{name} is a pure host module and must not depend on {token}",
            )
    for path in (BLE_TYPES_HDR, BLE_STATE_HDR, BLE_EVENTS_HDR, BLE_STORE_HDR):
        header = strip_comments(report.read(path))
        if not header:
            continue
        for token in FORBIDDEN_IN_PURE:
            report.require(
                token not in header,
                f"{rel(path)} must not include or reference {token}",
            )


def check_pure_abi_matches_the_contracts(report: Report) -> None:
    """The production ABI must stay the one the host contracts declare."""
    pairs = (
        (BLE_TYPES_HDR, CONTRACT_TYPES, ("device_kind", "kind_from_appearance",
                                         "kind_label", "sanitize_name",
                                         "normalize_address", "format_passkey",
                                         "mask_passkey", "device_list")),
        (BLE_STATE_HDR, CONTRACT_STATE, ("state_machine", "begin_search",
                                         "begin_paired", "scan_finished",
                                         "auth_requested", "pairing_finished",
                                         "connection_finished",
                                         "schedule_reconnect", "notice_text",
                                         "status_line", "displayed_passkey",
                                         "take_actions", "k_msg_scan_empty",
                                         "k_msg_scan_failed",
                                         "k_msg_scan_timeout",
                                         "k_msg_search_cancelled")),
        (BLE_EVENTS_HDR, CONTRACT_EVENTS, ("event_dispatch", "begin_scan",
                                           "begin_pairing", "begin_connection",
                                           "publish_scan_result", "publish_auth_request",
                                           "drop_stale", "drain", "event_summary",
                                           "k_max_pending_events")),
        (BLE_STORE_HDR, CONTRACT_STORE, ("bond_record", "bond_store", "encode_bond",
                                         "decode_bond", "serialize", "deserialize",
                                         "k_max_bonds", "k_bond_magic")),
    )
    for header, contract, tokens in pairs:
        text = strip_comments(report.read(header))
        if not text:
            continue
        for token in tokens:
            report.require(token in text,
                           f"{rel(header)} must expose the contracted {token!r}")


def check_no_classic_bluetooth(report: Report, sources: dict[str, str]) -> None:
    """REQ-BLE-011 / AC-BLE-011: BLE only; no BR/EDR anywhere in the feature."""
    for name, text in sources.items():
        if not text:
            continue
        for token in CLASSIC_TOKENS:
            report.require(
                token not in text,
                f"{name} must not use Bluetooth Classic token {token!r} (REQ-BLE-011)",
            )


def check_no_bluedroid(report: Report, sources: dict[str, str]) -> None:
    """REQ-BLE-001: the whole feature is a NimBLE host; no Bluedroid anywhere."""
    for name, text in sources.items():
        if not text:
            continue
        for token in BLUEDROID_TOKENS:
            report.require(
                token not in text,
                f"{name} must not use the Bluedroid API {token!r}: the approved "
                "host is NimBLE over ESP-Hosted (REQ-BLE-001)",
            )


def check_shell_routing(report: Report) -> None:
    """REQ-BLE-002: `bluetooth search` / `bluetooth paired` are real commands."""
    header = strip_comments(report.read(SHELL_UTILS_HDR))
    for token in ("CYBERDECK_CMD_BLUETOOTH_SEARCH", "CYBERDECK_CMD_BLUETOOTH_PAIRED"):
        report.require(token in header, f"shell command enum must expose {token}")

    shell = strip_comments(report.read(SHELL_UTILS))
    parser = function_body(shell, "cyberdeck_cmd_t cyberdeck_parse_command(", report)
    for literal in ('"bluetooth search"', '"bluetooth paired"'):
        report.require(literal in parser,
                       f"command parser must recognize {literal}")
    for token in ("CYBERDECK_CMD_BLUETOOTH_SEARCH", "CYBERDECK_CMD_BLUETOOTH_PAIRED"):
        report.require(token in parser, f"command parser must route {token}")

    # No third subcommand: the bare verb and any extra operand stay unknown.
    bluetooth_literals = set(re.findall(r'"(bluetooth[^"]*)"', parser))
    report.require(
        bluetooth_literals == {"bluetooth search", "bluetooth paired"},
        "the shell must expose exactly the two approved bluetooth subcommands, "
        f"found {sorted(bluetooth_literals)}",
    )

    catalog = strip_comments(report.read(SHELL_HELP))
    report.require('"bluetooth"' in catalog,
                   "the shared help catalog must own the bluetooth row")
    report.require("bluetooth [search|paired]" in catalog,
                   "the shared help catalog must document the bluetooth subcommands")
    entries = re.findall(r'\{"bluetooth"[^\n]*\}', catalog)
    report.require(len(entries) == 1,
                   "the bluetooth help row must exist exactly once in the catalog")
    report.require(re.search(r"std::array\s*<\s*entry\s*,\s*16\s*>\s+kCatalog", catalog)
                   is not None,
                   "the shared help catalog must grow to 16 entries")

    fixture = strip_comments(report.read(HELP_FIXTURE))
    report.require("bluetooth [search|paired]" in fixture,
                   "the host help fixture must contain the approved bluetooth row")


def check_ui_routing(report: Report) -> None:
    """REQ-BLE-002/003/005/006: the TUI hands keys and commands to the feature."""
    ui = strip_comments(report.read(UI))
    if not ui:
        return
    for token in ("CYBERDECK_CMD_BLUETOOTH_SEARCH", "CYBERDECK_CMD_BLUETOOTH_PAIRED"):
        report.require(token in ui,
                       f"cyberdeck_ui.cpp must route {token} to the BLE feature")

    # The key path: the four approved keys must reach the BLE model.  Reusing the
    # existing Wi-Fi style key enum is fine, but the BLE branch must exist.
    for token in ("cyberdeck_ble::key::up", "cyberdeck_ble::key::down",
                  "cyberdeck_ble::key::enter", "cyberdeck_ble::key::escape"):
        report.require(token in ui,
                       f"cyberdeck_ui.cpp must forward {token} to the BLE state machine")

    # Nothing scan-related may block the LVGL task: the UI may only ask the
    # adapter to start, never call a stack scan/connect API itself.  The real
    # host is NimBLE, so the guarded tokens are the NimBLE host entry points
    # (plus the Bluedroid names, which must not exist at all).
    for token in ("ble_gap_disc", "ble_gap_disc_cancel", "ble_gap_connect",
                  "ble_sm_inject_io", "ble_hs_cfg", "nimble_port_",
                  "esp_hosted_", "esp_ble_gap"):
        report.require(token not in ui,
                       f"cyberdeck_ui.cpp must not call the BLE stack directly ({token})")

    # The UI must not invent its own device list: it renders the pure model.
    for token in ("cyberdeck_ble::device_list", "cyberdeck_ble::state_machine"):
        report.require(token in ui,
                       f"cyberdeck_ui.cpp must consume {token} instead of a private list")

    # REQ-BLE-003/008: the UI reaches the adapter only through the bounded
    # public queue and the observer callback - it never blocks on the coprocessor.
    for token in ("ble_mgr_enqueue_cmd", "ble_mgr_register_observer"):
        report.require(token in ui,
                       f"cyberdeck_ui.cpp must drive the adapter through {token}")


def check_adapter_composition(report: Report) -> None:
    """REQ-BLE-001/003/008/010: a dedicated task owns the coprocessor."""
    header = strip_comments(report.read(BLE_MGR_HDR))
    source = strip_comments(report.read(BLE_MGR_SRC))
    if not header or not source:
        return

    for token in ("ble_mgr_start", "ble_mgr_stop"):
        report.require(token in header and token in source,
                       f"the BLE adapter must expose {token}")

    # The bounded public surface the UI is allowed to use.
    for token in ("ble_mgr_enqueue_cmd", "ble_mgr_cmd_t",
                  "ble_mgr_register_observer", "ble_mgr_unregister_observer"):
        report.require(token in header,
                       f"ble_mgr.h must expose the bounded public API {token}")
    report.require(re.search(r"ble_mgr_enqueue_cmd\s*\([^)]*TickType_t", header)
                   is not None,
                   "ble_mgr_enqueue_cmd must take a timeout: the UI queue write "
                   "may never block the LVGL task")

    # The adapter owns a task; the UI only enqueues.
    report.require("xTaskCreate" in source or "xTaskCreatePinnedToCore" in source,
                   "the BLE adapter must own a dedicated FreeRTOS task")
    # Every task the adapter creates must be named: an anonymous worker shows up
    # as "task" in the FreeRTOS dump and cannot be diagnosed in the field.
    task_creations = re.findall(
        r"xTaskCreate(?:PinnedToCore)?\s*\(([^;]*?)\)\s*[;)]", source, re.S)
    report.require(len(task_creations) >= 1,
                   "the BLE adapter must create at least one named task")
    for arguments in task_creations:
        report.require(
            re.search(r'^\s*[A-Za-z_][A-Za-z0-9_]*\s*,\s*"', arguments) is not None,
            "every BLE adapter task must be created with an explicit name "
            f"(got xTaskCreate({arguments.strip()[:60]}))")

    # The command queue is bounded, so a backlog can never grow without limit.
    report.require(re.search(r"xQueueCreate\s*\(\s*[A-Za-z_][A-Za-z0-9_]*", source)
                   is not None,
                   "the BLE adapter must create a bounded command queue")
    report.require(re.search(r"#\s*define\s+[A-Za-z_][A-Za-z0-9_]*QUEUE[A-Za-z0-9_]*\s+"
                             r"[0-9]+\b", source) is not None
                   or re.search(r"static\s+constexpr\s+\w+\s+\w*[Qq]ueue\w*\s*[=({]",
                                source) is not None,
                   "the bounded command queue must be sized by an explicit "
                   "compile-time bound")

    # Non-fatal boot: a failure must be logged, never fatal.
    app = strip_comments(report.read(APP))
    if app:
        report.require("ble_mgr_start" in app,
                       "app_main must start the BLE adapter")
        body = function_body(app, "void app_main(", report)
        report.require("ble_mgr_start" in body,
                       "app_main must call ble_mgr_start")
        if "ble_mgr_start" in body:
            tail = body[body.find("ble_mgr_start"):]
            for fatal in ("abort()", "esp_restart()", "while (1)"):
                # Only the few statements right after the call matter.
                report.require(fatal not in tail[:400],
                               "a ble_mgr_start failure must be non-fatal "
                               f"(found {fatal!r} right after the call)")

    # The adapter is the only BLE-stack caller in the feature, and the stack it
    # owns is the NimBLE host reached over ESP-Hosted (REQ-BLE-001).  The
    # approved plan disables Bluedroid, so those names must be absent here.
    for token in NIMBLE_STACK_TOKENS:
        report.require(token in source,
                       f"the BLE adapter must be the module that uses {token}")
    for token in HOSTED_LINK_TOKENS:
        report.require(token in source,
                       f"the BLE adapter must be the module that uses {token}")
    for token in BLUEDROID_TOKENS:
        report.require(token not in source,
                       f"the BLE adapter is a NimBLE host: {token!r} (Bluedroid) "
                       "must not appear (REQ-BLE-001)")

    # The pure modules must not be reachable from the LVGL task: the UI keeps no
    # stack handle.  The rule is an allow-list of the public bounded API, so a
    # private helper or a blocking adapter call is rejected without having to
    # name the private symbols in this test.
    ui = strip_comments(report.read(UI))
    if ui:
        used = set(re.findall(r"\bble_mgr_[A-Za-z0-9_]*", ui))
        stray = sorted(used - APPROVED_UI_BLE_API)
        report.require(not stray,
                       "cyberdeck_ui.cpp may only use the bounded public BLE API; "
                       f"found non-public adapter symbols {stray}")


def check_secrets_never_reach_a_log(report: Report) -> None:
    """REQ-BLE-010: no passkey/key material in any log or diagnostic line."""
    sources = {
        "ble_mgr.cpp": strip_comments(report.read(BLE_MGR_SRC)),
        "cyberdeck_ble_store.cpp": strip_comments(report.read(BLE_STORE_SRC)),
        "cyberdeck_ble_state_machine.cpp": strip_comments(report.read(BLE_STATE_SRC)),
        "cyberdeck_ble_event_dispatch.cpp": strip_comments(report.read(BLE_EVENTS_SRC)),
        "cyberdeck_ui.cpp": strip_comments(report.read(UI)),
    }
    for name, text in sources.items():
        if not text:
            continue
        for line in text.splitlines():
            if not re.search(r"ESP_LOG[A-Z]*\s*\(", line):
                continue
            lowered = line.lower()
            # An explicitly masked projection is allowed; a raw value is not.
            if "mask_passkey" in lowered or '"******"' in lowered:
                continue
            for token in SECRET_TOKENS:
                report.require(
                    token not in lowered,
                    f"{name} logs a secret-bearing identifier {token!r}: {line.strip()}",
                )

    # The persisted record set has no field for key material at all.
    store_header = strip_comments(report.read(BLE_STORE_HDR))
    if store_header:
        record = re.search(r"struct\s+bond_record\s*\{(.*?)\}", store_header, re.S)
        if report.require(record is not None, "bond_record must be declared"):
            fields = record.group(1)
            for token in SECRET_TOKENS:
                report.require(
                    token not in fields.lower(),
                    f"bond_record must not carry a {token!r} field",
                )


def check_configuration(report: Report) -> None:
    """REQ-BLE-001: BLE host on the P4 talking to the C6 over esp_hosted."""
    defaults = report.read(SDKCONFIG_DEFAULTS)
    if defaults:
        for token in ("CONFIG_BT_ENABLED=y",):
            report.require(token in defaults,
                           f"sdkconfig.defaults must enable the BLE host ({token})")
        # The approved host is NimBLE carrying HCI over the ESP-Hosted VHCI
        # transport; the C6 owns the controller, so the local one is disabled.
        for token in ("CONFIG_BT_NIMBLE_ENABLED=y", "CONFIG_BT_CONTROLLER_DISABLED=y",
                      "CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y"):
            report.require(token in defaults,
                           f"sdkconfig.defaults must select the approved NimBLE host "
                           f"over ESP-Hosted ({token})")
        report.require("CONFIG_BT_BLUEDROID_ENABLED=n" in defaults,
                       "sdkconfig.defaults must keep Bluedroid disabled: the "
                       "approved host is NimBLE (REQ-BLE-001)")
        # LE only: the controller must never be put in classic-only mode.
        report.require("ESP_BT_MODE_CLASSIC" not in defaults,
                       "sdkconfig.defaults must not request Bluetooth Classic")

    yml = report.read(COMPONENT_YML)
    if yml:
        report.require("esp_hosted" in yml,
                       "main/idf_component.yml must keep esp_hosted for the C6 link")

    component = report.read(COMPONENT)
    if component:
        for name in ("cyberdeck_ble_types.cpp", "cyberdeck_ble_state_machine.cpp",
                     "cyberdeck_ble_event_dispatch.cpp", "cyberdeck_ble_store.cpp",
                     "ble_mgr.cpp"):
            report.require(name in component,
                           f"the component CMake must register {name}")


def check_registration_and_traceability(report: Report) -> None:
    makefile = report.read(MAKEFILE)
    if makefile:
        for target in ("test_ble_types", "test_ble_state_machine",
                       "test_ble_event_dispatch", "test_ble_store",
                       "test_ble_command_parse", "test_ble_integration_contract"):
            report.require(target in makefile,
                           f"the host Makefile must register {target}")
        for contract in ("cyberdeck_ble_types.h", "cyberdeck_ble_state_machine.h",
                         "cyberdeck_ble_event_dispatch.h", "cyberdeck_ble_store.h"):
            report.require(contract in makefile,
                           f"the host Makefile must depend on contracts/{contract}")
        for source in ("cyberdeck_ble_types.cpp", "cyberdeck_ble_state_machine.cpp",
                       "cyberdeck_ble_event_dispatch.cpp", "cyberdeck_ble_store.cpp"):
            report.require(source in makefile,
                           f"the host Makefile must link the production {source}")

    gitignore = report.read(GITIGNORE)
    if gitignore:
        for name in HOST_TEST_FILES:
            negated = f"!tests/host/keymap/{name}"
            report.require(negated in gitignore,
                           f".gitignore must keep the test source {name} via {negated}")

    code_map = report.read(CODE_MAP)
    if code_map:
        for name in HOST_TEST_FILES:
            report.require(name in code_map,
                           f"code-map.md must map the BLE test {name}")
        for number in range(1, 12):
            requirement = f"REQ-BLE-{number:03d}"
            report.require(requirement in code_map,
                           f"code-map.md must trace {requirement}")
            criterion = f"AC-BLE-{number:03d}"
            report.require(criterion in code_map,
                           f"code-map.md must trace {criterion}")
        for token in ("bluetooth search", "bluetooth paired", "esp_hosted",
                      "ESP32-C6"):
            report.require(token in code_map,
                           f"code-map.md must document {token!r}")

    for path, label in ((ARCH_EN, "docs/ARCHITECTURE.md"),
                        (ARCH_PT, "docs/ARCHITECTURE.pt-BR.md")):
        text = report.read(path)
        if not text:
            continue
        report.require("bluetooth" in text.lower(),
                       f"{label} must document the bluetooth commands")
        report.require("REQ-BLE-001" in text,
                       f"{label} must trace REQ-BLE-001 for the BLE feature")

    for path, label in ((README_EN, "README.md"), (README_PT, "README.pt-BR.md")):
        text = report.read(path)
        if not text:
            continue
        report.require("bluetooth search" in text,
                       f"{label} must document the `bluetooth search` command")
        report.require("bluetooth paired" in text,
                       f"{label} must document the `bluetooth paired` command")


def main() -> int:
    report = Report()

    # The test-owned contract fixtures must exist before anything else.
    for contract in (CONTRACT_TYPES, CONTRACT_STATE, CONTRACT_EVENTS, CONTRACT_STORE):
        report.require(contract.is_file(),
                       f"missing host contract fixture: {rel(contract)}")

    check_pure_modules_are_stack_free(report)
    check_pure_abi_matches_the_contracts(report)
    check_shell_routing(report)
    check_ui_routing(report)
    check_adapter_composition(report)
    check_secrets_never_reach_a_log(report)
    check_configuration(report)
    check_registration_and_traceability(report)
    feature_sources = {
        "ble_mgr.cpp": strip_comments(report.read(BLE_MGR_SRC)),
        "ble_mgr.h": strip_comments(report.read(BLE_MGR_HDR)),
        "cyberdeck_ble_types.cpp": strip_comments(report.read(BLE_TYPES_SRC)),
        "cyberdeck_ble_state_machine.cpp": strip_comments(report.read(BLE_STATE_SRC)),
        "cyberdeck_ble_event_dispatch.cpp": strip_comments(report.read(BLE_EVENTS_SRC)),
        "cyberdeck_ble_store.cpp": strip_comments(report.read(BLE_STORE_SRC)),
        "cyberdeck_ui.cpp": strip_comments(report.read(UI)),
    }
    check_no_classic_bluetooth(report, feature_sources)
    check_no_bluedroid(report, feature_sources)

    if report.failures:
        for failure in report.failures:
            print(f"FAIL: {failure}")
        print(f"FAIL: {len(report.failures)} structural BLE checks")
        return 1

    print("PASS: BLE structural contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
