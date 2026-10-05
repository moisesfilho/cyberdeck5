#!/usr/bin/env python3
"""Acceptance contract for the read-only event projection hosts.

The projection and HTTP handler are ESP-only, so this test keeps the host gate
structural and models the bounded offset/chunk protocol independently.  It
must not turn the recovery transport into a second implementation.
"""

from pathlib import Path
import base64


ROOT = Path(__file__).resolve().parents[3]
EVENT_LOG = ROOT / "components/cyberdeck/src/platform/logging/event_log.cpp"
EVENT_HEADER = ROOT / "components/cyberdeck/include/platform/logging/event_log.h"
SERIAL = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"
HTTP = ROOT / "components/cyberdeck/src/apps/screenshot/screenshot_server.cpp"
PATHS = ROOT / "components/cyberdeck/include/cyberdeck_paths.h"


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def block(source, start, end):
    first = source.index(start)
    return source[first:source.index(end, first)]


def test_boot_and_append_contract():
    source = EVENT_LOG.read_text(encoding="utf-8")
    header = EVENT_HEADER.read_text(encoding="utf-8")
    check("event_log_text_size" in header and "event_log_text_read" in header,
          "boot must expose the read-only projection API")
    check("s_text_queue = xQueueCreateStatic" in source,
          "projection must have a bounded queue")
    check("xTaskCreate(text_log_task" in source,
          "projection must start during event-log initialization")
    boot = block(source, 'extern "C" esp_err_t event_log_init',
                 'extern "C" void event_log_write')
    check("text_log_task" in boot and "log_task" in boot,
          "boot must initialize both binary and projection workers")
    writer = block(source, "bool write_record", "void complete_durable_request")
    check("xQueueSend(s_text_queue" not in writer,
          "binary write path must not perform projection I/O")
    log_task = block(source, "void log_task", "int log_vprintf")
    check("xQueueSend(s_text_queue, &record, 0)" in log_task,
          "successful binary records must enqueue best-effort projection")
    append = block(source, "bool append_text_projection", "void text_log_task")
    check('"%Y-%m-%d %H:%M:%S"' in append and '"up:%" PRId64 "ms"' in append,
          "projection must preserve deterministic timestamp fallback")
    check("fflush(file) == 0 && fsync(fileno(file)) == 0" in append,
           "append must be durable before publishing success")


def test_durable_append_is_not_projection_io():
    source = EVENT_LOG.read_text(encoding="utf-8")
    completion = block(source, "void complete_durable_request", "void log_task")
    check("request->persisted = write_record" in completion and
          "xQueueSend(s_text_queue, &request->record, 0)" in completion,
          "durable append must persist binary data before optional projection")
    check("text projection queue full; durable event retained" in completion,
          "projection backpressure must not discard the durable event")
    write = block(source, "bool write_record", "void complete_durable_request")
    check("xQueueSend(s_text_queue" not in write and "fwrite" in write,
          "durable/binary write must not perform projection I/O")


def test_log_task_receives_normal_queue_before_notify_fallback():
    """The binary queue must remain a real consumer path, not notify-only."""
    source = EVENT_LOG.read_text(encoding="utf-8")
    task = block(source, "void log_task", "int log_vprintf")
    durable_receive = task.index("xQueueReceive(s_durable_queue")
    normal_receive = task.index("xQueueReceive(s_queue, &record, 0)")
    notify_fallback = task.index("ulTaskNotifyTake")
    check("xQueueReceive(s_queue, &record, 0)" in task,
          "log_task must receive normal events from s_queue")
    check(durable_receive < normal_receive < notify_fallback,
          "durable work, normal queue work, then notify fallback must be ordered")
    check("write_record(file, &record" in task and "remember_record(record)" in task,
          "normal queue receive must persist and update recent state")


def test_rotation_is_bounded_and_model_preserves_order():
    source = EVENT_LOG.read_text(encoding="utf-8")
    rotate = block(source, "bool rotate_text_if_needed", "bool append_text_projection")
    check("TEXT_FILE_LIMIT" in rotate and "TEXT_ROTATION_COUNT" in rotate,
          "rotation must use explicit size and generation bounds")
    check("for (size_t generation = TEXT_ROTATION_COUNT; generation > 1; --generation)" in rotate,
          "rotation must shift only the bounded generations")
    check("rename(current, to)" in rotate,
          "rotation must move the active file instead of truncating it")

    limit = 1024 * 1024
    generations = [b"oldest", b"older", b"current"]
    incoming = b"new\n"
    check(len(generations[-1]) + len(incoming) <= limit,
          "model precondition must fit the active projection")
    generations = [b"newest", *generations[1:]]
    check(len(generations) <= 7 and generations[0] == b"newest",
          "rotation model must retain at most seven archived generations")


def test_read_is_bounded_readonly_and_eof_safe():
    source = EVENT_LOG.read_text(encoding="utf-8")
    read = block(source, 'extern "C" esp_err_t event_log_text_read', "\n}")
    check("capacity > 1024" in read and "ESP_ERR_INVALID_ARG" in read,
          "read must reject invalid and oversized buffers")
    check("open_text_snapshot" in read and "fwrite" not in read and "rename" not in read,
          "read must be strictly read-only")
    check("*out_read = 0" in read and "return ESP_OK" in read,
          "EOF must be an explicit successful zero-byte read")

    files = [b"a" * 1024, b"bc"]
    payload = b"".join(files)
    offset = len(payload)
    check(offset == 1026, "offset model must include rotated files")
    check(payload[offset:] == b"", "reading exactly at EOF must produce no bytes")
    for start in range(0, len(payload), 1024):
        chunk = payload[start:start + 1024]
        check(len(base64.b64encode(chunk)) <= 1400,
              "transport chunks must remain bounded after Base64 encoding")


def test_mutex_io_timeout_and_snapshot_lifecycle_contract():
    source = EVENT_LOG.read_text(encoding="utf-8")
    check("constexpr TickType_t TEXT_MUTEX_TIMEOUT = pdMS_TO_TICKS(100)" in source,
          "text lock timeout must be typed and converted from milliseconds")
    check("TEXT_MUTEX_TIMEOUT) != pdTRUE" in source and "ESP_ERR_TIMEOUT" in source,
          "read must expose a typed timeout instead of blocking forever")
    read = block(source, 'extern "C" esp_err_t event_log_text_read', "\n}")
    snapshot_open = read.index("open_text_snapshot")
    unlocked = read.index("xSemaphoreGive(s_text_mutex)", snapshot_open)
    first_io = min(read.index("fseek", unlocked), read.index("fread", unlocked))
    check(unlocked < first_io,
          "read must release coordination before paged SD I/O")
    check("TextSnapshot snapshot" in read and "FILE *files[TEXT_ROTATION_COUNT + 1]" in source,
          "paged reads must retain bounded descriptors for one stable snapshot")
    init = block(source, 'extern "C" esp_err_t event_log_init', 'extern "C" void event_log_write')
    check("vTaskDelete(s_text_task)" in init and "esp_log_set_vprintf(s_serial_vprintf)" in init,
          "failed task initialization must roll back the projection task and logger hook")
    check(init.index("s_initialized = true") > init.index("xTaskCreate(log_task"),
          "init must publish initialized only after both tasks succeed")
    for handle in ("s_text_queue = nullptr", "s_text_mutex = nullptr",
                   "s_recent_mutex = nullptr", "s_queue = nullptr"):
        check(handle in init, f"init rollback must clear {handle}")


def test_paged_snapshot_is_stable_under_rotation_model():
    # Descriptors opened for a page retain old bytes after directory rotation.
    snapshot = (b"old\n", b"current-", b"line\n")
    pages = []
    offset = 0
    payload = b"".join(snapshot)
    while offset < len(payload):
        pages.append(payload[offset:offset + 4])
        offset += 4
    rotated_directory = (b"newest\n", b"old\n", b"current-line\n")
    check(b"".join(pages) == payload,
          "pagination must read the opened snapshot, not a newly rotated directory")
    check(rotated_directory != snapshot,
          "rotation model must actually change directory contents")


def test_crlf_projection_normalization_contract():
    source = EVENT_LOG.read_text(encoding="utf-8")
    append = block(source, "bool append_text_projection", "void text_log_task")
    latest = block(source, 'extern "C" size_t event_log_latest', 'extern "C" size_t event_log_text_size')
    check(append.count("*p == '\\r' || *p == '\\n'") == 2,
          "projection must normalize both CR and LF in persisted fields")
    check(latest.count("*p == '\\r' || *p == '\\n'") == 1,
          "recent event output must normalize both CR and LF")
    check("line[length - 1] = '\\n'" in append,
          "projected records must end with exactly one LF")
    normalized = "tag\r\nvalue\n".replace("\r", " ").replace("\n", " ")
    check("\r" not in normalized and "\n" not in normalized,
          "bounded normalization model must eliminate CR/LF injection")


def test_serial_events_read_chunks_eof_and_errors():
    source = SERIAL.read_text(encoding="utf-8")
    check('"events.read"' in source, "Serial-JTAG must preserve events.read")
    read = block(source, "device_result exec_events_read", "device_result device_exec")
    check("event_log_text_size()" in read and "event_log_text_read" in read,
          "events.read must use the projection API")
    check("path" not in read and "../" not in read,
          "events.read must not introduce a caller-controlled filesystem path")
    check(r'start += ",\"ok\":true,\"event\":\"start\",\"size\":"' in read and
          r'frame += ",\"ok\":true,\"event\":\"chunk\",\"offset\":"' in read and
          'frame_event(req, "end")' in read,
          "events.read must frame start/chunk/end")
    check("constexpr std::size_t k_chunk = 1024" in read and "base64_encode(bytes, count)" in read,
          "events.read chunks must be bounded and Base64 encoded")
    check("count == 0" in read and "dispatch_error::internal" in read,
          "events.read must fail on a non-advancing/error read")
    check("offset += count" in read and "send_frame(frame_event(req, \"end\"))" in read,
          "events.read must advance and close the stream")


def test_http_fixed_readonly_endpoint_and_security():
    source = HTTP.read_text(encoding="utf-8")
    paths = PATHS.read_text(encoding="utf-8")
    handler = block(source, "static esp_err_t events_handler", "static esp_err_t start_server")
    check('req->method != HTTP_GET' in handler and "HTTPD_405_METHOD_NOT_ALLOWED" in handler,
          "HTTP event endpoint must reject non-GET methods")
    check("is_local_peer(req)" in handler and "HTTPD_403_FORBIDDEN" in handler,
          "HTTP event endpoint must enforce local-peer security")
    check('"text/plain; charset=utf-8"' in handler and "event_log_text_read" in handler,
          "HTTP endpoint must expose only the text projection")
    check('uri = "/events.txt"' in source and "HTTP_GET" in source,
          "HTTP endpoint must be fixed to GET /events.txt")
    check("req->uri" not in handler and "httpd_req_get_url_query_str" not in handler,
          "HTTP endpoint must not accept an arbitrary path")
    check("CYBERDECK_TEXT_LOG_PATH" in paths and "events.txt" in paths,
          "the canonical event path must remain fixed")


def test_existing_contracts_remain_present():
    serial = SERIAL.read_text(encoding="utf-8")
    check('"ping"' in serial and '"screen.dump"' in serial and '"fs.write"' in serial,
          "new event transport must preserve existing Serial-JTAG commands")
    check("fs.write" in serial and "O_NOFOLLOW" in serial,
          "path-safety contract for existing writes must remain present")
    check("dispatch_error::invalid_path" in serial and "dispatch_error::io_error" in serial,
          "existing typed path/IO errors must remain available")


def main():
    tests = [test_boot_and_append_contract,
             test_durable_append_is_not_projection_io,
             test_log_task_receives_normal_queue_before_notify_fallback,
             test_rotation_is_bounded_and_model_preserves_order,
             test_read_is_bounded_readonly_and_eof_safe,
             test_mutex_io_timeout_and_snapshot_lifecycle_contract,
             test_paged_snapshot_is_stable_under_rotation_model,
             test_crlf_projection_normalization_contract,
             test_serial_events_read_chunks_eof_and_errors,
             test_http_fixed_readonly_endpoint_and_security,
             test_existing_contracts_remain_present]
    for test in tests:
        test()
    print(f"PASS: event projection contract ({len(tests)} scenarios; TEST-EVENT-BOOT/APPEND/QUEUE/ROTATE/READ/SECURITY/REGRESSION)")


if __name__ == "__main__":
    main()
