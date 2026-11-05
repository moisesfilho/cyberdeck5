#!/usr/bin/env python3
"""Guard phase-2 resource ownership against source-level lifecycle regressions."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "components/cyberdeck/src"
DOC = ROOT / "docs/OS-RESOURCE-CONTRACTS.pt-BR.md"


def source(path: str) -> str:
    return (SRC / path).read_text(encoding="utf-8")


def ordered(text: str, *markers: str) -> None:
    positions = [text.index(marker) for marker in markers]
    assert positions == sorted(positions), f"incorrect ownership/boot order: {markers}"


def main() -> None:
    doc = DOC.read_text(encoding="utf-8")
    assert "## Fronteiras de acesso" in doc and "## Inventário dos contratos críticos" in doc
    assert "## Riscos de concorrência e limites da evidência" in doc
    for path in (
        "apps/runtime/cyberdeck_app_runtime.cpp",
        "apps/shell/cyberdeck_cat_worker.cpp",
        "apps/serial/cyberdeck_serial_bridge.cpp",
        "apps/wifi/wifi_mgr.cpp",
        "apps/bluetooth/ble_mgr.cpp",
        "apps/ssh/ssh_client.cpp",
        "apps/screenshot/screenshot_server.cpp",
        "apps/system/cyberdeck_time_sync.cpp",
        "apps/music/cyberdeck_music_player.cpp",
        "platform/logging/event_log.cpp",
        "platform/storage/cyberdeck_sd_io_coordinator.cpp",
        "platform/power/cyberdeck_poweroff.cpp",
    ):
        assert Path(path).stem in doc, f"missing critical resource owner: {path}"

    boot = (ROOT / "main/app_main.cpp").read_text(encoding="utf-8")
    ordered(boot, "nvs_flash_init()", "cyberdeck_recovery::init()", "wifi_mgr_hosted_transport_prepare()",
            "bsp_sdcard_mount_hosted()", "ESP_ERROR_CHECK(sd_mount_err)", "recover_save_sidecars(",
            "cyberdeck_system_apps_register()", "bsp_display_start()", "battery_protection_start()",
            "tab5_keyboard_init()", "cyberdeck_system_apps_start()", "cyberdeck_system_apps_commit_ready()")

    runtime = source("apps/runtime/cyberdeck_app_runtime.cpp")
    stop = runtime[runtime.index("bool runtime::stop_index("):runtime.index("app_state runtime::state(")]
    ordered(stop, "!stop_index(i)", "applications_[index]->teardown()", "if (!stopped)",
            "applications_[index]->grant_ = {};", "quotas_.revoke(index)")
    assert "stops_console_owner(id)" in runtime
    assert "lifecycle_timeout_ms" in stop[stop.index("quotas_.revoke(index)"):], (
        "the manifest budget must remain post-return diagnostics"
    )

    sd = source("platform/storage/cyberdeck_sd_io_coordinator.cpp")
    ordered(sd[sd.index("drain_result coordinator::drain"):],
            "if (!drained_.wait_until(", "return drain_result::timed_out", "state_ = gate_state::closed")

    # Derive queue depths from the actual definitions; do not pin incidental
    # values in the contract. A terminal lane must remain separate from BLE ads.
    for path, declarations, creations in (
        ("apps/bluetooth/ble_mgr.cpp",
         ("BLE_MGR_QUEUE_SIZE", "BLE_MGR_GAP_EVENT_QUEUE_SIZE", "BLE_MGR_GAP_TERMINAL_QUEUE_SIZE"),
         ("s_ble_queue", "s_gap_event_queue", "s_gap_terminal_queue")),
        ("apps/shell/cyberdeck_cat_worker.cpp", ("k_cat_work_queue_capacity",), ("s_queue",)),
        ("apps/screenshot/screenshot_server.cpp", ("k_control_queue_depth",), ("s_control_queue",)),
        ("apps/music/cyberdeck_music_player.cpp", ("k_queue_depth",), ("impl_->queue",)),
    ):
        content = source(path)
        for depth, queue in zip(declarations, creations):
            assert (re.search(rf"\b{depth}\s*=\s*[1-9]\d*\s*;", content) or
                    re.search(rf"^#define\s+{depth}\s+[1-9]\d*\s*$", content, re.M)), path
            assert re.search(rf"{re.escape(queue)}\s*=\s*xQueueCreate(?:WithCaps)?\(\s*{depth}\b", content), path

    screenshot = source("apps/screenshot/screenshot_server.cpp")
    assert "s_stop_requested.store(true" in screenshot
    assert "s_quarantined = true" in screenshot[screenshot.index("esp_err_t screenshot_server_stop("):]
    print("PASS: OS phase-2 ownership, boot, bounded queues and teardown contracts")


if __name__ == "__main__":
    main()
