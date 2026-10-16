#!/usr/bin/env python3
"""Structural integration guard for the ESP-IDF foreground app boundary.

Runtime behavior is exercised by the production core/adapters' host binaries;
this guard prevents the platform app from bypassing those tested adapters.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
APP = (ROOT / "components/cyberdeck/src/apps/network/cyberdeck_netdiag_app.cpp").read_text()
REGISTRY = (ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp").read_text()

checks = 0
failures = 0


def require(condition: bool, message: str) -> None:
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print(f"FAIL: {message}", file=sys.stderr)


def body(signature: str) -> str:
    match = re.search(signature, APP)
    require(match is not None, f"production app contains {signature}")
    if match is None:
        return ""
    start = APP.find("{", match.end())
    depth = 0
    for index in range(start, len(APP)):
        if APP[index] == "{":
            depth += 1
        elif APP[index] == "}":
            depth -= 1
            if depth == 0:
                return APP[start + 1 : index]
    require(False, f"balanced body for {signature}")
    return ""


execute = body(r"cyberdeck_apps::result application::execute\(")
start = body(r"bool application::start\(\)")
stop = body(r"bool application::stop\(\)")
tcp = body(r"outcome perform_tcp\(")
https = body(r"outcome perform_https\(")
dns = body(r"bool resolve_target\(")

require("parsed.kind == operation::status" in execute and "wifi_mgr_get_status" in execute,
        "status reads current Wi-Fi snapshot")
require("esp_netif_get_handle_from_ifkey(\"WIFI_STA_DEF\")" in execute and
        "esp_netif_get_ip_info" in execute and "esp_netif_get_dns_info" in execute and
        "esp_wifi_sta_get_ap_info" in execute,
        "status obtains interface IP, gateway, DNS and RSSI through platform APIs")
require("<missing>" in execute and "netdiag status: interface=sta" in execute,
        "unavailable status fields have explicit bounded presentation")
require("parsed.kind == operation::result" in execute and "worker.published" in execute,
        "result command reads only the published snapshot")
require("parsed.kind == operation::cancel" in execute and "cancel_requested.store(true)" in execute,
        "cancel command signals active worker without synchronous network work")
require("xQueueSend(worker.queue, &next, 0)" in execute and "worker.active.load()" in execute,
        "work admission is single-flight and nonblocking at queue boundary")
require("xTaskCreateWithCaps(worker_task, \"netdiag\", k_worker_stack_bytes" in start and
        "k_worker_stack_bytes = 8192" in APP,
        "worker uses the declared 8192-byte stack (no high-water claim)")
require("dns::start" in dns and "dns::poll" in dns and "dns::cancel" in dns,
        "DNS integration delegates to production adapter start/poll/cancel")
require("tcp::start" in tcp and "tcp::poll" in tcp and "tcp::cancel" in tcp,
        "TCP integration delegates to production adapter start/poll/cancel")
require("https::start" in https and "https::poll" in https and "https::cancel" in https,
        "HTTPS integration delegates to production adapter start/poll/cancel")
require("esp_http_client_perform" not in APP and "getaddrinfo" not in APP and
        not re.search(r"\b(socket|select)\s*\(", APP),
        "app does not duplicate adapter-owned network syscalls")
require("worker.grant.allows(cyberdeck_apps::resource::network)" in execute and
        "worker.grant.allows(cyberdeck_apps::resource::network)" in dns,
        "network grant gates dispatch and DNS polling")
stop_signal = stop.find("worker.stopping.store(true)")
stop_wait = stop.find("xSemaphoreTake(worker.done")
grant_revoke = stop.find("worker.grant = {};")
resource_delete = stop.find("vQueueDelete(worker.queue)")
require(0 <= stop_signal < stop_wait < grant_revoke < resource_delete,
        "stop signals, waits boundedly, revokes grant, then destroys resources")
require("cyberdeck_netdiag::global_application()" in REGISTRY,
        "system registry registers the app without asserting boot auto-start")

print(f"netdiag app integration contract: {checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
