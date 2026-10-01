#!/usr/bin/env python3
"""Structural contract for the safe Wi-Fi start rollback slice."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "components/cyberdeck/src/apps/wifi/wifi_mgr.cpp"


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    rollback = source[source.index("static esp_err_t rollback_start"):source.index("static void clear_secret", source.index("static esp_err_t rollback_start"))]
    worker = source[source.index("static void wifi_event_worker"):source.index("/* The coordinator is fed")]
    start = source[source.index("esp_err_t wifi_mgr_start"):]

    assert "wifi_event_worker_wake" in source
    assert "wifi_event_worker_quiesced" in source
    assert "wifi_event_stop_requested" in source
    assert "WIFI_EVENT_WORKER_JOIN_TIMEOUT_MS" in source
    assert "s_wifi_event_stop_requested.store(true" in rollback
    assert "xSemaphoreGive(s_wifi_event_worker_wake)" in rollback
    assert "xSemaphoreTake(s_wifi_event_worker_quiesced" in rollback
    assert "s_wifi_event_worker = NULL" in rollback
    assert rollback.index("xSemaphoreTake(s_wifi_event_worker_quiesced") < rollback.index("wipe_sensitive_state(true)")
    assert rollback.index("wipe_sensitive_state(true)") < rollback.index("vSemaphoreDelete(s_wifi_event_mutex)")
    assert "vTaskDelete(s_wifi_event_worker)" not in rollback
    assert "s_wifi_event_worker_quarantined = true" in rollback
    assert "return ESP_ERR_TIMEOUT" in rollback
    assert "vTaskDelete(s_wifi_event_worker)" not in source
    assert "if (s_wifi_event_stop_requested.load" in worker
    assert "xSemaphoreGive(s_wifi_event_worker_quiesced)" in worker
    assert worker.index("xSemaphoreGive(s_wifi_event_worker_quiesced)") < worker.index("vTaskDelete(NULL)")
    assert "s_wifi_event_worker_quarantined || s_wifi_event_worker != NULL" in start
    assert "rollback_start(false, start_err)" in start
    assert "rollback_start(true, wifi_start_err)" in start

    print("PASS: Wi-Fi event worker rollback join/quarantine contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
