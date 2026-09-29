#!/usr/bin/env python3
"""Host structural contract for Phase 1 BLE authentication hand-off.

This intentionally inspects the non-host-linkable UI/ESP-IDF seam.  It does not
start NimBLE, FreeRTOS, a simulator, hardware, or the Serial Automation Bridge.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp").read_text()
# Key dispatch (passkey entry/backspace/Enter) moved to the extracted session.
SESSION = (ROOT / "components/cyberdeck/src/features/shell/cyberdeck_shell_session.cpp").read_text()
AUTH_PATH = UI + SESSION
MGR = (ROOT / "components/cyberdeck/src/features/bluetooth/ble_mgr.cpp").read_text()
MGR_H = (ROOT / "components/cyberdeck/include/features/bluetooth/ble_mgr.h").read_text()
STATE = (ROOT / "components/cyberdeck/src/features/bluetooth/cyberdeck_ble_state_machine.cpp").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    # NimBLE 5.5.5 has one numeric-comparison action. The user's decision is
    # carried by numcmp_accept; there is no BLE_SM_IOACT_CONFIRM. NUMCMP's
    # number is presentation data, not a host-side passkey assertion.
    for name, value in (("BLE_MGR_AUTH_IO_DISP", "0"),
                        ("BLE_MGR_AUTH_IO_INPUT", "1"),
                        ("BLE_MGR_AUTH_IO_NUMCMP", "2")):
        require(re.search(rf"{name}\s*=\s*{value}", MGR_H),
                f"missing {name}")
    require("BLE_MGR_AUTH_IO_CONFIRM" not in MGR_H,
            "the public ABI must not invent BLE_MGR_AUTH_IO_CONFIRM")
    mapping = re.search(r"if \(action\.kind == cyberdeck_ble::action_kind::submit_auth\).*?\n        }\n", UI, re.S)
    require(mapping is not None, "submit_auth command mapping missing")
    for token in ("auth_io_action::display", "BLE_MGR_AUTH_IO_DISP",
                  "auth_io_action::input", "BLE_MGR_AUTH_IO_INPUT",
                  "auth_io_action::numeric_compare", "BLE_MGR_AUTH_IO_NUMCMP"):
        require(token in mapping.group(0), f"auth action lost at UI seam: {token}")
    require("auth_io_action::confirm" not in mapping.group(0) and
            "BLE_MGR_AUTH_IO_CONFIRM" not in mapping.group(0),
            "UI must represent confirmation as NUMCMP, not a new action")
    require("cmd.passkey.addr_type = addr_type" in mapping.group(0),
            "auth command lost addr_type")

    task_start = MGR.index("static void ble_mgr_task(void *arg)\n{")
    task = MGR[task_start:]
    gate = task.index("if (!s_host_synced && cmd.kind != BLE_MGR_CMD_STOP)")
    switch = task.index("switch (cmd.kind)")
    require(gate < switch and "reject_pre_sync_command(cmd)" in task[gate:switch],
            "PASSKEY_REPLY is not covered by the central host_synced gate")
    inject = task[task.index("case BLE_MGR_CMD_PASSKEY_REPLY:"):task.index("case BLE_MGR_CMD_PAIR_CANCEL:")]
    require("cmd.token != s_auth_token" in inject and
            "s_auth_conn == BLE_HS_CONN_HANDLE_NONE" in inject and
            "cmd.passkey.io_action != s_auth_action" in inject,
            "stale/duplicate/invalid auth commands are not rejected")
    require("ble_sm_inject_io(auth_conn, &pkey)" in inject,
            "accepted auth command does not reach ble_sm_inject_io")
    require(re.search(r"case BLE_MGR_AUTH_IO_NUMCMP:.*?BLE_SM_IOACT_NUMCMP",
                      inject, re.S),
            "numeric comparison must remain BLE_SM_IOACT_NUMCMP")
    require("pkey.numcmp_accept = 1" in inject,
            "NUMCMP confirmation must set the documented acceptance flag")
    numcmp = re.search(r"case BLE_MGR_AUTH_IO_NUMCMP:.*?break;", inject, re.S)
    require(numcmp is not None and "pkey.passkey" not in numcmp.group(0),
            "NUMCMP must not be treated as a passkey reply")
    require("BLE_SM_IOACT_CONFIRM" not in inject,
            "production must not reference nonexistent BLE_SM_IOACT_CONFIRM")
    require("s_auth_conn = BLE_HS_CONN_HANDLE_NONE" in inject and
            "s_auth_token = 0" in inject and
            "s_auth_action = BLE_MGR_AUTH_IO_INPUT" in inject,
            "auth challenge is not consumed before injection")

    # UI input is exactly six decimal digits, incrementally bounded, supports
    # backspace and Enter, and clears the transient buffer on every exit path.
    require("ble_auth_input_.size() >= cyberdeck_ble::k_passkey_digits" in SESSION and
            "append_ble_auth_digit" in SESSION,
            "auth input is not bounded to six digits")
    require("if (digit < '0' || digit > '9') return;" in SESSION,
            "auth input accepts non-digits")
    # A bare pop_back() only shrinks the string: the removed digit would stay
    # resident in the capacity.  The byte must be wiped before the shrink.
    backspace = re.search(
        r"if \(!ble_auth_input_\.empty\(\)\).*?\}", SESSION, re.S)
    require(backspace is not None and "ble_auth_input_.pop_back()" in backspace.group(0),
            "auth backspace is missing")
    require(backspace is not None and "wipe_string(ble_auth_input_)" in backspace.group(0),
            "auth backspace must wipe the removed digit instead of only shrinking")
    require("ble_auth_input_.size() == cyberdeck_ble::k_passkey_digits" in SESSION,
            "Enter does not reject incomplete passkeys")
    require("clear_ble_auth_input()" in SESSION and
            "s_shell_session.clear_ble_auth_input()" in UI,
            "transient auth input is not cleared across lifecycle paths")
    require("s_ble_auth_input" not in UI,
            "the passkey buffer must be owned by the session, not the UI")
    require("pending_auth_action() == cyberdeck_ble::auth_io_action::input" in AUTH_PATH,
            "only INPUT may open passkey entry")
    require("auth_request_kind::numeric_compare" in STATE and
            "auth_request_kind::confirm" not in STATE,
            "the pure model must expose NUMCMP, not a synthetic confirm action")

    # Unsupported OOB is rejected closed at the real GAP callback boundary;
    # this is distinct from a user decision and must never become a prompt.
    oob = re.search(r"BLE_SM_IOACT_OOB.*?(?=} else if|\n\s*}\n\s*drain_dispatch_events)",
                   inject + "\n" + MGR, re.S)
    require(oob is not None and "ble_gap_terminate" in oob.group(0) and
            "BLE_ERR_AUTH_FAIL" in oob.group(0),
            "OOB authentication must fail closed")
    require("k_passkey_digits" in AUTH_PATH and "k_passkey_modulus" in MGR,
            "passkey format bounds are missing")

    # Existing navigation/deadline paths remain model-owned. HID is a separate
    # post-auth phase and is covered by test_ble_hid_contract.py.
    require("advance_time" in STATE and "key::up" not in MGR,
            "BLE deadline/navigation boundary was unexpectedly moved")
    require("ble_gap_security_initiate(s_pair_conn)" in MGR,
            "security initiation must remain in the synced GAP callback path")
    reject_start = MGR.index("static void reject_pre_sync_command(const ble_mgr_cmd_t &cmd)\n{")
    reject_end = MGR.index("static void start_scan_command(", reject_start)
    require("BLE_MGR_CMD_PASSKEY_REPLY" in MGR[reject_start:reject_end],
            "pre-sync rejection lost PASSKEY_REPLY")
    print("ble auth structural contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
