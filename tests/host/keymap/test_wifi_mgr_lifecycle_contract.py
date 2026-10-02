#!/usr/bin/env python3
"""Structural contract for the complete Wi-Fi manager lifecycle."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "components/cyberdeck/src/apps/wifi/wifi_mgr.cpp"
SYSTEM = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
HEADER = ROOT / "components/cyberdeck/include/apps/wifi/wifi_mgr.h"


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    system = SYSTEM.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    stop = source[source.index("esp_err_t wifi_mgr_stop") : source.index("esp_err_t wifi_mgr_start")]
    worker = source[source.index("static void net_worker") : source.index("bool wifi_mgr_is_enabled")]

    assert "wifi_mgr_stop(uint32_t timeout_ms)" in header
    for state in ("stopped", "starting", "running", "stopping", "quarantined"):
        assert state in source
    for name in ("net_worker_wake", "net_worker_quiesced", "net_stop_requested"):
        assert name in source
    assert "s_wifi_event_stop_requested.store(true" in stop
    assert "s_net_stop_requested.store(true" in stop
    assert "s_connection_token.fetch_add" in stop
    assert "++s_scan_generation" in stop
    assert stop.index("xTimerStop(s_retry_timer") < stop.index("join_worker")
    assert stop.index("join_worker(s_wifi_event_worker") < stop.index("wipe_sensitive_state(true)")
    assert stop.index("wipe_sensitive_state(true)") < stop.index("vSemaphoreDelete(s_net_mutex)")
    assert "s_wifi_event_worker_quarantined = true" in stop
    assert "return ESP_ERR_TIMEOUT" in stop
    assert "vTaskDelete(s_net_worker)" not in source
    assert "vTaskDelete(s_wifi_event_worker)" not in source
    assert "xSemaphoreGive(s_net_worker_quiesced)" in worker
    assert worker.index("xSemaphoreGive(s_net_worker_quiesced)") < worker.index("vTaskDelete(NULL)")
    assert "wifi_mgr_stop(k_wifi_lifecycle_timeout_ms)" in system
    assert "start_wifi, stop_wifi, true" in system
    assert "k_wifi_lifecycle_timeout_ms = 8000" in system
    print("PASS: Wi-Fi manager lifecycle contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
