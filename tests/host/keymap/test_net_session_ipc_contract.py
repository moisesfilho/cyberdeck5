"""Structural contract for SSH-to-Wi-Fi session IPC."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
WIFI = (ROOT / "components/cyberdeck/src/apps/wifi/wifi_mgr.cpp").read_text()
SSH = (ROOT / "components/cyberdeck/src/apps/ssh/ssh_client.cpp").read_text()

assert "enum class net_session_event" in WIFI
for name in ("SESSION_CONNECTING", "SESSION_ONLINE", "SESSION_SOCKET_ERROR"):
    assert name in WIFI
assert "NET_SESSION_EVENT_QUEUE_LENGTH 4" in WIFI
assert "xQueueCreate(NET_SESSION_EVENT_QUEUE_LENGTH" in WIFI
assert "xQueueSend(s_net_session_event_queue, &event, 0)" in WIFI
assert "vQueueDelete(s_net_session_event_queue)" in WIFI
assert "s_net_coordinator.on_session_connecting();" in WIFI
assert "s_net_coordinator.on_session_online();" in WIFI
assert "s_net_coordinator.on_socket_error();" in WIFI
assert "wifi_mgr_net_session_" not in SSH
assert "wifi_storage_mount" not in SSH
assert '"apps/wifi/wifi_mgr.h"' not in SSH
assert '"apps/wifi/wifi_storage.h"' not in SSH
print("PASS: SSH session events use bounded Wi-Fi worker IPC")
