#!/usr/bin/env python3
"""Structural contract for the LAN HTTP command channel.

`screenshot_server.cpp` and `cyberdeck_control_server.cpp` need ESP-IDF, NVS and
FreeRTOS, so they cannot be linked in a host binary. The pure protocol is
covered behaviourally by test_control_protocol.cpp and the client by
test_control_cli.py; this companion proves the parts that only source
inspection can prove:

  * the three /control routes are registered by the existing LAN server with
    the right methods and the handler table keeps its fail-closed rollback;
  * every handler refuses non-local peers and wrong methods;
  * the command is never executed inside the HTTP handler: it is authenticated,
    bounded and enqueued for the dedicated worker task;
  * the worker injects through the same keyboard entry point as `ui.type` and
    refuses to inject while the screen protection is active;
  * the dedicated key lives in NVS, is generated with esp_fill_random, is never
    logged, and is only readable through the USB bridge (`control.key`);
  * the OTA key and provisioning route are not reused;
  * production files, the host targets, coverage policy, CLI and documentation
    stay wired.

Traceability:
  REQ-CTL-HTTP-01 -> routes, methods, peer filter, fail-closed registration
  REQ-CTL-HTTP-02 -> dedicated key in NVS, USB-only provisioning, no reuse of OTA
  REQ-CTL-HTTP-03 -> authenticated request: nonce, digest, MAC, single use
  REQ-CTL-HTTP-04 -> bounded enqueue, worker injection, screen-off refusal
  REQ-CTL-HTTP-05 -> terminal snapshot bounded and read-only
  AC-CTL-HTTP-01 -> test_control_protocol.cpp
  AC-CTL-HTTP-02 -> test_control_cli.py
  AC-CTL-HTTP-03 -> this contract (wiring) + Tab5 validation
"""
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SERVER = ROOT / "components/cyberdeck/src/apps/control/cyberdeck_control_server.cpp"
SERVER_HDR = ROOT / "components/cyberdeck/include/apps/control/cyberdeck_control_server.h"
PROTOCOL = ROOT / "components/cyberdeck/src/apps/control/cyberdeck_control_protocol.cpp"
PROTOCOL_HDR = ROOT / "components/cyberdeck/include/apps/control/cyberdeck_control_protocol.h"
PEER_HDR = ROOT / "components/cyberdeck/include/platform/net/cyberdeck_local_peer.h"
HTTP_SERVER = ROOT / "components/cyberdeck/src/apps/screenshot/screenshot_server.cpp"
BRIDGE = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"
BRIDGE_HDR = ROOT / "components/cyberdeck/include/apps/serial/cyberdeck_serial_bridge.h"
APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
CMAKE = ROOT / "components/cyberdeck/CMakeLists.txt"
SCOPE_GUARD = ROOT / "tools/coverage_scope_guard.py"
CLI = ROOT / "tools/cyberdeck_http_cli.py"
CODEMAP = ROOT / "code-map.md"
VERSION = ROOT / "version.txt"
MAKEFILE = HERE / "Makefile"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def strip_comments(source):
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source, name):
    match = re.search(rf"\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{", source)
    require(match is not None, f"source must define {name}()")
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


def check_routes(server):
    start = function_body(server, "static esp_err_t start_server")
    require("config.max_uri_handlers = 7;" in start,
            "the LAN server must reserve seven handler slots")
    for uri, method, handler in (
        ("/screenshot", "HTTP_GET", "screenshot_handler"),
        ("/events.txt", "HTTP_GET", "events_handler"),
        ("/battery", "HTTP_GET", "battery_handler"),
        ("/usb/audio", "HTTP_GET", "usb_audio_handler"),
        ("/control/challenge", "HTTP_GET", "cyberdeck_control::challenge_handler"),
        ("/control/type", "HTTP_POST", "cyberdeck_control::type_handler"),
        ("/control/terminal", "HTTP_GET", "cyberdeck_control::terminal_handler"),
    ):
        pattern = (rf"\.uri\s*=\s*\"{re.escape(uri)}\"\s*,\s*\.method\s*=\s*{method}\s*,"
                   rf"\s*\.handler\s*=\s*{re.escape(handler)}")
        require(re.search(pattern, start) is not None, f"missing route {method} {uri}")
    require("httpd_stop(s_server)" in start and "return ESP_FAIL" in start,
            "route registration must keep its fail-closed rollback")
    require("cyberdeck_control::challenge_handler" in server, "control handlers must be declared")
    start_call = function_body(server, "esp_err_t screenshot_server_start")
    require("cyberdeck_control::start()" in start_call,
            "the service must start the control channel")
    require("ESP_LOGW" in start_call,
            "a control start failure must be logged without aborting boot")
    stop_call = function_body(server, "esp_err_t screenshot_server_stop")
    require("cyberdeck_control::stop(control_budget)" in stop_call,
            "the service must stop the control channel with a bounded budget")
    require("timeout_ms / 2" in stop_call,
            "control and HTTP joins must share the caller budget")


def check_handlers(server):
    for name, method in (("challenge_handler", "GET"), ("type_handler", "POST"),
                         ("terminal_handler", "GET")):
        body = function_body(server, f"esp_err_t {name}")
        require(f'method_reply(req, "{method}")' in body,
                f"{name} must reject the wrong HTTP method")
        require("socket_is_local_peer" in body,
                f"{name} must keep the LAN peer filter")
    ce = strip_comments(server)
    type_body = function_body(ce, "esp_err_t type_handler")
    require("cyberdeck_keyboard_input" not in type_body,
            "the HTTP handler must not inject input itself")
    require("authorize(req" in type_body, "the handler must authenticate the request")
    require(type_body.index("authorize(req") < type_body.index("xQueueSend"),
            "authentication must precede the enqueue")
    require("std::malloc(sizeof(job))" in type_body,
            "the command body must be heap-backed and bounded by the job limit")
    require("xQueueSend(s_queue, &item, 0)" in type_body,
            "the enqueue must never block the HTTP task")
    require("req->content_len > k_max_text_bytes" in type_body,
            "the handler must bound the request body")
    require('"Connection", "close"' in type_body,
            "an oversized body must not leave the connection reusable")
    require("plan_text" in type_body, "the handler must validate and plan the text")
    require("screen_off_is_active()" in type_body,
            "the type route must refuse an injection with the protection overlay")

    terminal_body = function_body(ce, "esp_err_t terminal_handler")
    require("cyberdeck_ui_term_dump" in terminal_body,
            "the terminal route must reuse the bounded UI dump")
    require("bsp_display_lock" not in ce,
            "the control channel must not take the display lock directly")
    require("screen_off_is_active()" in terminal_body,
            "the terminal route must refuse the opaque protection overlay")

    challenge_body = function_body(ce, "esp_err_t challenge_handler")
    require("s_nonces.issue(" in challenge_body, "the challenge must issue a nonce")
    require("esp_fill_random(random, sizeof(random))" in challenge_body,
            "the nonce must come from esp_fill_random")

    worker = function_body(ce, "void control_task")
    require("inject_event" in worker,
            "the worker must deliver events through the retrying helper")
    deliver = function_body(ce, "bool inject_event")
    require("cyberdeck_keyboard_submit_reporting" in deliver,
            "injection must submit with consumption reporting")
    require("cyberdeck_keyboard_wait_delivered" in deliver,
            "each event must be confirmed before the next is submitted")
    require("k_inject_attempts" in deliver,
            "a rejected submission must be retried a bounded number of times")
    require("screen_off_is_active()" in worker,
            "the worker must refuse to inject over the protection overlay")
    require("vTaskDelay" in worker, "the worker must pace the injection")
    require("xQueueReceive(s_queue" in worker, "the worker must own the bounded queue")


def check_authentication(server, protocol, protocol_header):
    require("mbedtls_md_info_from_type(MBEDTLS_MD_SHA256)" in server,
            "the device must pin SHA-256 from mbedtls")
    hmac_body = function_body(server, "bool hmac_sha256")
    require("^ 0x36" in hmac_body and "^ 0x5c" in hmac_body,
            "the device must build HMAC-SHA256 with the ipad/opad construction")
    require("mbedtls_md_setup" in hmac_body and "mbedtls_md_finish" in hmac_body,
            "HMAC must use the mbedtls digest context")
    require("mbedtls_md_hmac_starts" not in server,
            "the platform does not expose the private HMAC context API")
    require("verify(s_nonces, s_key, sizeof(s_key)" in server,
            "the device must verify with the dedicated key")
    require("esp_timer_get_time()" in server, "the nonce deadline must use the monotonic clock")
    require("constant_time_equal" not in server,
            "the constant-time comparison belongs to the pure protocol module")
    for token in ("k_max_text_bytes = 1024", "k_max_lines = 32", "k_nonce_ttl_us = 60'000'000",
                  "k_key_bytes = 32"):
        require(token in protocol_header, f"protocol bound missing: {token}")
    for forbidden in ("esp_err.h", "lvgl.h", "freertos/", "esp_log.h", "nvs.h", "mbedtls/"):
        require(forbidden not in protocol_header, f"pure protocol header must not include {forbidden}")
    for forbidden in ("esp_err.h", "lvgl.h", "freertos/", "esp_log.h", "nvs.h", "mbedtls/"):
        require(forbidden not in protocol, f"pure protocol source must not include {forbidden}")
    verify_body = function_body(protocol, "error verify")
    require("store.pending(" in verify_body, "verify must require a pending nonce")
    require("store.consume(" in verify_body, "verify must consume the nonce")
    require("constant_time_equal(" in verify_body, "verify must compare in constant time")
    require(verify_body.index("constant_time_equal(") < verify_body.index("store.consume("),
            "the nonce must only be consumed after a valid MAC")


def check_key_handling(server, bridge, bridge_header):
    require('constexpr const char *k_namespace = "ctl_auth";' in server,
            "the control key must use its own NVS namespace")
    require("nvs_set_blob(handle, k_key_name" in server, "the key must be stored in NVS")
    require("esp_fill_random(s_key" in server, "the key must be generated with esp_fill_random")
    for line in server.splitlines():
        if "ESP_LOG" in line:
            require("hex_encode" not in line and "secret" not in line,
                    "the control key must never be logged")
    require(strip_comments(server).count("hex_encode(s_key") == 1,
            "the raw key may only be encoded by the USB provisioning call")
    require("hex_encode(s_key" in function_body(strip_comments(server), "esp_err_t serial_key"),
            "provisioning must encode the dedicated key")
    require('if (req.type == "control.key")' in bridge,
            "the USB bridge must expose the control key provisioning")
    require("cyberdeck_control::serial_key(" in bridge,
            "provisioning must read the dedicated key")
    require('"control.key"' in bridge, "control.key must be a known bridge type")
    require("control.key" in bridge_header, "the bridge header must document control.key")
    require("USB" in bridge_header, "the bridge header must keep the USB-only contract")
    for path, text in (("control server", server), ("LAN server", Path(
            HTTP_SERVER).read_text(encoding="utf-8"))):
        require("ota.key" not in text, f"{path} must never expose the OTA key")
        require('"control.key"' not in text, f"{path} must not expose control.key over HTTP")


def check_wiring(apps, cmake, scope_guard, cli, codemap, version):
    require('"Local-network telemetry and control HTTP endpoints"' in apps,
            "the service description must reflect the added control routes")
    require('"1.1.0"' in apps, "the screenshot/control service version must be bumped")
    for source in ("src/apps/control/cyberdeck_control_protocol.cpp",
                   "src/apps/control/cyberdeck_control_server.cpp"):
        require(source in cmake, f"{source} must be registered in CMake")
    require("components/cyberdeck/src/apps/control/cyberdeck_control_protocol.cpp"
            in scope_guard, "the pure protocol must be required in coverage")
    require("components/cyberdeck/src/apps/control/cyberdeck_control_server.cpp"
            in scope_guard, "the device adapter must be allowlisted in coverage")
    for command in ('"provision"', '"type"', '"terminal"', '"status"'):
        require(command in cli, f"CLI must expose the {command} command")
    require("O_EXCL" in cli, "the CLI must never overwrite an existing key")
    require("X-CTL-MAC" in cli and "X-CTL-Nonce" in cli,
            "the CLI must send the authenticated headers")
    for name in ("control/cyberdeck_control_protocol", "control/cyberdeck_control_server",
                 "platform/net/cyberdeck_local_peer", "cyberdeck_http_cli.py"):
        require(name in codemap, f"code-map must document {name}")
    match = re.fullmatch(r"([0-9]+)\.([0-9]+)\.([0-9]+)", version)
    require(match is not None, "version.txt must be MAJOR.MINOR.PATCH")
    require(tuple(int(part) for part in match.groups()) >= (1, 15, 0),
            "this delivery must keep the firmware version floor")


def check_makefile(makefile):
    require("test_control_protocol_bin" in makefile, "the protocol test must be built")
    require("test_control_protocol/" in makefile, "the protocol test needs incremental objects")
    require("test_control_http_contract" in makefile, "the structural contract must be a target")
    require("test_control_cli" in makefile, "the CLI test must be a target")
    for target in ("test_control_protocol", "test_control_http_contract", "test_control_cli"):
        require(f"--no-print-directory {target}" in makefile,
                f"{target} must run in the aggregate")
    require(re.search(r"rm -rf[^\n]*test_control_protocol", makefile) is not None,
            "clean must remove the protocol test artifacts")


def main():
    server = SERVER.read_text(encoding="utf-8")
    server_header = SERVER_HDR.read_text(encoding="utf-8")
    protocol = PROTOCOL.read_text(encoding="utf-8")
    protocol_header = PROTOCOL_HDR.read_text(encoding="utf-8")
    peer_header = PEER_HDR.read_text(encoding="utf-8")
    http_server = HTTP_SERVER.read_text(encoding="utf-8")
    bridge = BRIDGE.read_text(encoding="utf-8")
    bridge_header = BRIDGE_HDR.read_text(encoding="utf-8")

    check_routes(http_server)
    check_handlers(server)
    check_authentication(server, protocol, protocol_header)
    check_key_handling(server, bridge, bridge_header)
    check_wiring(APPS.read_text(encoding="utf-8"), CMAKE.read_text(encoding="utf-8"),
                 SCOPE_GUARD.read_text(encoding="utf-8"), CLI.read_text(encoding="utf-8"),
                 CODEMAP.read_text(encoding="utf-8"), VERSION.read_text(encoding="utf-8").strip())
    check_makefile(MAKEFILE.read_text(encoding="utf-8"))

    require("k_input_queue_depth = 4" in server_header,
            "the input queue must stay bounded at four jobs")
    require("k_terminal_max_bytes = 2048" in server_header,
            "the terminal snapshot must stay bounded")
    require("192.168" not in server and "10.0.0.0/8" not in server,
            "the peer allowlist must stay in the shared header")
    for token in ("127", "172.16", "192.168", "169.254", "0xfc", "0x80"):
        require(token in peer_header, f"peer allowlist must cover {token}")
    require("getpeername" in peer_header, "the peer filter must read the socket peer")

    print("PASS: LAN HTTP command channel contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
