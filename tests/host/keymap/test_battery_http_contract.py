#!/usr/bin/env python3
"""Structural contract for the read-only GET /battery endpoint.

`screenshot_server.cpp` needs ESP-IDF's HTTP server, so the route, guards and
registration cannot be linked in a host binary.  The JSON document itself is
covered by the linked test_battery_json.cpp; this companion proves the parts
that only source inspection can prove:

  * the route is registered from a table in start_server() with
    max_uri_handlers raised to 3 and a stop-on-failure rollback;
  * the handler keeps the same guards as the existing endpoints (405 for
    non-GET, 403 through is_local_peer, 503 when the request mutex is busy);
  * the handler is defined after stop_server(), outside the block
    test_event_projection_contract.py extracts for events_handler;
  * the endpoint is read-only and cheap: it consumes the cached policy
    snapshot, never samples the INA226/I2C, never takes the display lock and
    never writes;
  * the response is a single application/json body with Cache-Control
    no-store, and a formatter failure maps to 500 instead of a partial body;
  * /screenshot and /events.txt keep their handlers, guards and wiring;
  * the pure formatter stays allocation-free, bounded by kMaxJsonBytes and
    fail-closed, and every touched file is registered/documented.

Traceability:
  REQ-BAT-HTTP-01 -> route table, max_uri_handlers, rollback (LAN-only/read-only)
  REQ-BAT-HTTP-02 -> JSON schema 1, fixed order, integers only, <=384 B, null, state unavailable
  REQ-BAT-HTTP-03 -> cached snapshot, no I2C/reader/display lock/write
  REQ-BAT-HTTP-04 -> /screenshot and /events.txt unchanged (serial bridge untouched)
  REQ-BAT-HTTP-05 -> guards 405/403/503/500, single body, Cache-Control no-store
  AC-BAT-HTTP-01 -> test_battery_json.cpp (document shape and limits)
  AC-BAT-HTTP-02 -> this contract (lan-only, read-only, bounded)
"""
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SERVER = ROOT / "components/cyberdeck/src/apps/screenshot/screenshot_server.cpp"
ADAPTER_HDR = ROOT / "components/cyberdeck/include/platform/sensors/battery_protection.h"
ADAPTER_SRC = ROOT / "components/cyberdeck/src/platform/sensors/battery_protection.cpp"
POLICY_HDR = ROOT / "components/cyberdeck/include/platform/sensors/cyberdeck_battery_protection.h"
JSON_HDR = ROOT / "components/cyberdeck/include/platform/sensors/cyberdeck_battery_json.h"
JSON_SRC = ROOT / "components/cyberdeck/src/platform/sensors/cyberdeck_battery_json.cpp"
JSON_TEST = HERE / "test_battery_json.cpp"
CMAKE = ROOT / "components/cyberdeck/CMakeLists.txt"
MAKEFILE_PATH = HERE / "Makefile"
CODEMAP = ROOT / "code-map.md"
ARCHITECTURE = ROOT / "docs/ARCHITECTURE.md"
ARCHITECTURE_PT = ROOT / "docs/ARCHITECTURE.pt-BR.md"
README = ROOT / "README.md"
README_PT = ROOT / "README.pt-BR.md"
MANUAL = ROOT / "tests/manual/battery-http-validation.pt-BR.md"
VERSION = ROOT / "version.txt"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def version_components(text):
    """Firmware version as a numeric tuple, or None when the format is wrong.

    Zero-padded segments are rejected (the file is stored without padding) and
    the tuple comparison keeps 1.10.0 newer than 1.9.0.
    """
    match = re.fullmatch(r"([0-9]+)\.([0-9]+)\.([0-9]+)", text)
    if match is None:
        return None
    raw = match.groups()
    if any(part != str(int(part)) for part in raw):
        return None
    major, minor, patch = (int(part) for part in raw)
    return (major, minor, patch)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{", source)
    require(match is not None, f"source must define {name}()")
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


def check_route_and_rollback(server: str) -> None:
    """REQ-BAT-HTTP-01: table registration, three handlers, fail-closed stop."""
    start = function_body(server, "static esp_err_t start_server")
    require(re.search(r"max_uri_handlers\s*=\s*3\s*;", start) is not None,
            "start_server must raise max_uri_handlers to 3")
    routes = re.findall(r"\.uri\s*=\s*\"([^\"]+)\"", start)
    require(routes == ["/screenshot", "/events.txt", "/battery"],
            f"the registration table must be exactly screenshot/events/battery, found {routes}")
    require(len(re.findall(r"\{\s*\.uri\s*=", start)) == 3,
            "the registration table must hold exactly three entries")
    for handler in ("screenshot_handler", "events_handler", "battery_handler"):
        require(re.search(rf"\.handler\s*=\s*{handler}", start) is not None,
                f"the registration table must wire {handler}")
    require(re.search(r"for\s*\([^)]*httpd_uri_t", start) is not None,
            "handlers must be registered from the table in a loop")
    require(re.search(r"httpd_register_uri_handler\s*\(\s*s_server\s*,\s*&uri\s*\)", start)
            is not None,
            "the loop must register each table entry")
    require(re.search(r"httpd_stop\s*\(\s*s_server\s*\)", start) is not None,
            "a failed registration must stop the partially registered server")
    require("return ESP_FAIL;" in start,
            "a failed registration must fail closed")
    require(re.search(r"if\s*\(\s*s_server\s*!=\s*NULL\s*\)\s*return ESP_OK", start)
            is not None,
            "start_server must stay idempotent")
    require(re.search(r"server_port\s*=\s*80\s*;", start) is not None,
            "the endpoint must stay on the existing HTTP server port")
    require(re.search(r"stack_size\s*=\s*6144\s*;", start) is not None,
            "the endpoint must not add a task/stack of its own")


def check_handler_guards(server: str) -> None:
    """REQ-BAT-HTTP-02: identical guards and no extra allowlist."""
    handler = function_body(server, "static esp_err_t battery_handler")
    require("req->method != HTTP_GET" in handler and "HTTPD_405_METHOD_NOT_ALLOWED" in handler,
            "the battery endpoint must reject non-GET methods")
    require("is_local_peer(req)" in handler and "HTTPD_403_FORBIDDEN" in handler,
            "the battery endpoint must enforce the shared local-peer allowlist")
    require(re.search(r"xSemaphoreTake\s*\(\s*s_request_mutex\s*,\s*pdMS_TO_TICKS\(1000\)\s*\)",
                      handler) is not None,
            "the battery endpoint must reuse the bounded request mutex")
    require("send_service_unavailable" in handler,
            "a busy request mutex must answer 503")
    require(handler.count("xSemaphoreGive(s_request_mutex)") == 1,
            "the handler must release the request mutex exactly once")
    # The allowlist itself stays the single implementation shared by all routes.
    require(server.count("static bool is_local_peer") == 1,
            "is_local_peer must remain the single allowlist implementation")
    require("is_local_ipv4" in server and "0xac100000u" in server and "169" in server,
            "the private/link-local allowlist must remain intact")


def check_handler_is_outside_the_events_block(server: str) -> None:
    """Keep test_event_projection_contract.py's extracted block intact."""
    events_index = server.index("static esp_err_t events_handler")
    start_index = server.index("static esp_err_t start_server")
    definition = re.search(r"static esp_err_t battery_handler\s*\(httpd_req_t \*req\)\s*\{", server)
    require(definition is not None, "the battery handler definition must exist")
    assert definition is not None
    require(definition.start() > server.index("static void stop_server"),
            "the battery handler must be defined after stop_server()")
    require(not (events_index < definition.start() < start_index),
            "the battery handler must not be inserted between events_handler and start_server")


def check_readonly_cached_path(server: str) -> None:
    """REQ-BAT-HTTP-03: cached policy snapshot only, nothing sampled or written."""
    handler = function_body(server, "static esp_err_t battery_handler")
    require("battery_protection_get_cached_policy_snapshot" in handler,
            "the endpoint must read the cached policy snapshot")
    require("battery_protection_get_cached_policy_sampled_at_us" in handler,
            "the endpoint must read the cached sample timestamp")
    for banned in ("battery_protection_get_snapshot(", "battery_protection_get_policy_snapshot(",
                   "ina226", "i2c_", "nvs_", "bsp_display_lock", "cyberdeck_lvgl_dispatch",
                   "xQueueSend", "fwrite", "fopen", "remove(", "rename("):
        require(banned not in handler,
                f"the battery endpoint must not call {banned!r}")
    require("esp_app_get_description()" in handler and "version" in handler,
            "fw_version must come from the app description, not a duplicated literal")
    require("esp_timer_get_time()" in handler,
            "uptime_s must come from esp_timer_get_time()")
    require("battery_protection_get_cached_policy_snapshot(&value.policy)" in handler,
            "the snapshot must be projected into the pure JSON view")
    require("cyberdeck_battery_json::format_json" in handler,
            "the body must be produced by the pure formatter")

    getter = function_body(strip_comments(ADAPTER_SRC.read_text(encoding="utf-8")),
                           "battery_protection_get_cached_policy_sampled_at_us")
    require("s_policy_sampled_at_us" in getter,
            "the timestamp getter must read the policy sample timestamp")
    require("s_snapshot_mutex" in getter,
            "the timestamp getter must lock the snapshot mutex that owns the timestamp")
    require("s_policy_mutex" not in getter,
            "the timestamp getter must not nest inside the policy mutex")
    require("battery_protection_get_cached_policy_sampled_at_us" in ADAPTER_HDR.read_text(encoding="utf-8"),
            "the adapter header must declare the timestamp getter")

    # sampled_age_ms is only meaningful while both sides share one monotonic
    # clock: the adapter stamps the sample with esp_timer_get_time() and the
    # handler computes now the same way.  A wall-clock on either side would
    # silently turn the age into null (or nonsense) without failing any other
    # test, because the formatter receives injected values.
    require(re.search(r"value\.now_us\s*=\s*esp_timer_get_time\s*\(\s*\)", handler) is not None,
            "the handler must derive now_us from esp_timer_get_time()")
    adapter_plain = strip_comments(ADAPTER_SRC.read_text(encoding="utf-8"))
    require(re.search(r"s_policy_sampled_at_us\s*=\s*esp_timer_get_time\s*\(\s*\)",
                      adapter_plain) is not None,
            "the policy sample timestamp must come from the same monotonic clock")


def check_response_contract(server: str) -> None:
    """REQ-BAT-HTTP-04: single JSON body, no-store, 500 only on formatter failure."""
    handler = function_body(server, "static esp_err_t battery_handler")
    require(re.search(r"httpd_resp_set_type\s*\(\s*req\s*,\s*\"application/json\"\s*\)", handler)
            is not None,
            "the battery response must declare application/json")
    require(re.search(r"httpd_resp_set_hdr\s*\(\s*req\s*,\s*\"Cache-Control\"\s*,\s*\"no-store\"\s*\)",
                      handler) is not None,
            "the battery response must be non-cacheable")
    require(re.search(r"httpd_resp_send\s*\(\s*req\s*,\s*body\s*,\s*length\s*\)", handler)
            is not None,
            "the body must be sent as one bounded response")
    require("httpd_resp_send_chunk" not in handler,
            "the battery endpoint must not stream chunks")
    require("length <= 0" in handler and "HTTPD_500_INTERNAL_SERVER_ERROR" in handler,
            "a formatter that does not fit must answer 500")
    require("kMaxJsonBytes + 1" in handler,
            "the body buffer must be sized from kMaxJsonBytes plus the NUL")


def check_other_routes_unchanged(server: str) -> None:
    """REQ-BAT-HTTP-05: /screenshot and /events.txt stay as they are."""
    screenshot = function_body(server, "static esp_err_t screenshot_handler")
    require("is_local_peer(req)" in screenshot and "HTTPD_405_METHOD_NOT_ALLOWED" in screenshot,
            "/screenshot guards must remain")
    require(re.search(r"send_snapshot\s*\(req\)", screenshot) is not None,
            "/screenshot must keep using send_snapshot")
    events = function_body(server, "static esp_err_t events_handler")
    require("event_log_text_read" in events and "httpd_resp_send_chunk" in events,
            "/events.txt must keep its bounded chunked projection")
    require('"text/plain; charset=utf-8"' in events,
            "/events.txt content type must remain")
    require("screenshot_bmp_fill_header" in server and "screenshot_bmp_rgb565_to_bgr888" in server,
            "/screenshot BMP serialization must remain")
    require("control_kind::stop" in server and "s_quiesced" in server,
            "the server lifecycle (stop/join) must remain")


def check_json_module(json_hdr: str, json_src: str) -> None:
    """AC-BAT-HTTP-01: bounded, allocation-free, fixed-order formatter."""
    combined = json_hdr + "\n" + json_src
    require(re.search(r"kMaxJsonBytes\s*=\s*384\s*;", json_hdr) is not None,
            "the JSON body limit must stay 384 bytes")
    require(re.search(r"int\s+format_json\s*\(\s*char\s*\*\s*buffer\s*,\s*std::size_t\s+capacity\s*,\s*"
                      r"const\s+view\s*&\s*value\s*\)", json_hdr) is not None,
            "the formatter seam must be format_json(char*, size_t, const view&)")
    for banned in ("malloc", "calloc", "realloc", "std::string", "std::vector", "new ", "snprintf(buffer"):
        require(banned not in json_src,
                f"the formatter must not allocate or write unbounded ({banned!r})")
    order = ["schema", "state", "charge", "available", "bus_voltage_mv", "current_ma",
             "percentage", "protection_enabled", "protection_active", "charger_enabled",
             "sampled_at_us", "sampled_age_ms", "fw_version", "uptime_s"]
    positions = []
    for field in order:
        index = json_src.find(f'\\"{field}\\":')
        require(index >= 0, f"the formatter must emit the {field!r} field")
        positions.append(index)
    require(positions == sorted(positions),
            "the JSON fields must keep the documented fixed order")
    require('"{\\"schema\\":1,\\"state\\":' in json_src,
            "the schema field must be the versioned literal 1")
    require("\\u%04x" in json_src,
            "the firmware version must escape control bytes as \\u00XX")
    require('"unavailable"' in json_src,
            "a failed snapshot must render state/charge unavailable")
    require('"null"' in json_src,
            "unknown values must render null instead of a fabricated number")
    require("snprintf(digits" in json_src,
            "integers must be formatted into a bounded scratch buffer")
    require("state_name(value.policy.state)" in json_src,
            "the state token must reuse the canonical policy helper")
    require("charge_signal_name(value.policy.charge)" in json_src,
            "the charge token must reuse the canonical policy helper")
    require("cyberdeck_battery_protection::snapshot policy" in json_hdr,
            "the view must carry the real policy snapshot")


def check_wiring(cmake: str, makefile: str, codemap: str) -> None:
    require("src/platform/sensors/cyberdeck_battery_json.cpp" in cmake,
            "the firmware build must compile the JSON formatter")
    for target in ("test_battery_json", "test_battery_http_contract"):
        require(target in makefile, f"the Makefile must register {target}")
    require("BATTERY_JSON_BIN" in makefile and "$(BATTERY_JSON_BIN)" in makefile,
            "the JSON binary must join BINS so the aggregate builds it")
    require("test_battery_json/test.o" in makefile,
            "the JSON target must use per-TU objects")
    require("$(BATTERY_JSON_OBJS)" in makefile,
            "the JSON objects must be declared incrementally")
    clean_lines = [line for line in makefile.splitlines() if "rm -rf" in line]
    require(any("test_battery_json" in line for line in clean_lines),
            "clean must remove the JSON object directory")
    require("$(BATTERY_JSON_OBJS)" in makefile.split("MAKE_DEPFILES", 1)[-1],
            "the JSON depfiles must join MAKE_DEPFILES")
    require(JSON_TEST.exists(), "test_battery_json.cpp must exist")
    json_test = JSON_TEST.read_text(encoding="utf-8")
    for scenario in ("TEST-BAT-JSON-01", "TEST-BAT-JSON-02", "TEST-BAT-JSON-03",
                     "TEST-BAT-JSON-04", "TEST-BAT-JSON-05", "TEST-BAT-JSON-06",
                     "TEST-BAT-JSON-07", "TEST-BAT-JSON-08"):
        require(scenario in json_test,
                f"{scenario} must be documented in the linked formatter test")
    for token in ("cyberdeck_battery_json", "GET /battery", "test_battery_json",
                  "test_battery_http_contract"):
        require(token in codemap, f"code-map.md must document {token!r}")


def check_docs_and_version() -> None:
    for path in (ARCHITECTURE, ARCHITECTURE_PT):
        text = path.read_text(encoding="utf-8")
        require("GET /battery" in text, f"{path.name} must document GET /battery")
        require("no-store" in text, f"{path.name} must document the no-store policy")
        require("GET /screenshot" in text, f"{path.name} must keep the screenshot route")
    for path in (README, README_PT):
        text = path.read_text(encoding="utf-8")
        require("/battery" in text, f"{path.name} must document the battery route")
        require("curl" in text, f"{path.name} must show the curl usage")
    require(MANUAL.exists(), "a manual battery-http validation route must exist")
    manual = MANUAL.read_text(encoding="utf-8")
    require("curl" in manual and "LAN" in manual,
            "the manual route must validate the endpoint from another LAN host")
    require("USB" in manual,
            "the manual route must state that no USB cable is required")
    # Feature floor, not an exact pin: a later MINOR/PATCH release must keep the
    # endpoint contract green while the file stays MAJOR.MINOR.PATCH.
    version_text = VERSION.read_text(encoding="utf-8").strip()
    version = version_components(version_text)
    require(version is not None,
            "firmware version must use MAJOR.MINOR.PATCH without zero padding, "
            f"got {version_text!r}")
    require(version >= (1, 4, 0),
            f"firmware version must be at least 1.4.0 for GET /battery, got {version_text!r}")
    policy = POLICY_HDR.read_text(encoding="utf-8")
    require("struct snapshot" in policy,
            "the endpoint must not introduce a second battery snapshot type")


def main() -> int:
    server = strip_comments(SERVER.read_text(encoding="utf-8"))
    json_hdr = strip_comments(JSON_HDR.read_text(encoding="utf-8"))
    json_src = strip_comments(JSON_SRC.read_text(encoding="utf-8"))
    cmake = strip_comments(CMAKE.read_text(encoding="utf-8"))
    makefile = strip_comments(MAKEFILE_PATH.read_text(encoding="utf-8"))
    codemap = strip_comments(CODEMAP.read_text(encoding="utf-8"))

    check_route_and_rollback(server)
    check_handler_guards(server)
    check_handler_is_outside_the_events_block(server)
    check_readonly_cached_path(server)
    check_response_contract(server)
    check_other_routes_unchanged(server)
    check_json_module(json_hdr, json_src)
    check_wiring(cmake, makefile, codemap)
    check_docs_and_version()

    print("PASS: battery HTTP structural contract "
          "(REQ-BAT-HTTP-01..05; route/guards/read-only/bounded)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
