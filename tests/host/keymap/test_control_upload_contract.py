#!/usr/bin/env python3
"""Structural contract for the authenticated BMP upload (`POST /control/upload`).

`cyberdeck_control_server.cpp` needs ESP-IDF/NVS/FreeRTOS and cannot be linked
on the host; the pure protocol, the stream/publication state machine and the
client are covered behaviourally by test_control_upload_protocol.cpp and
test_control_cli.py. This companion proves what only source inspection can:

  * the new route is registered with the derived handler budget (`max_uri_handlers`
    must equal the table size) and the fail-closed rollback is preserved;
  * the upload handler authenticates the metadata *before* opening anything,
    never reads the body under the key mutex, uses a fixed-size buffer and a
    try-lock single-flight gate, and never consults the screen-off overlay;
  * a WRITE lease is held for the whole transaction and gate-closed answers 503;
  * publication uses an exclusive same-directory temp, fsync/close, never
    clobbers an existing target and only removes its own temp on failure;
  * the upload MAC message is domain-separated from `/control/type` and binds
    method + URI + declared size + SHA-256 + the target basename (approved
    plan: the name travels authenticated together with the size and digest);
  * CMake/coverage/Makefile/CLI/docs wiring stays in place and the CLI limit and
    domain are derived from the firmware source, not duplicated literals.

Traceability: REQ-UPLOAD-01..05 / AC-UPLOAD-01 (host), TEST-UPLOAD-
AUTH/STREAM/INTEG/PATH/CLI/ROUTE/REG.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SERVER = ROOT / "components/cyberdeck/src/apps/control/cyberdeck_control_server.cpp"
SERVER_HDR = ROOT / "components/cyberdeck/include/apps/control/cyberdeck_control_server.h"
PROTOCOL = ROOT / "components/cyberdeck/src/apps/control/cyberdeck_control_protocol.cpp"
PROTOCOL_HDR = ROOT / "components/cyberdeck/include/apps/control/cyberdeck_control_protocol.h"
UPLOAD = ROOT / "components/cyberdeck/src/apps/control/cyberdeck_control_upload.cpp"
UPLOAD_HDR = ROOT / "components/cyberdeck/include/apps/control/cyberdeck_control_upload.h"
HTTP_SERVER = ROOT / "components/cyberdeck/src/apps/screenshot/screenshot_server.cpp"
SYSTEM_APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
CMAKE = ROOT / "components/cyberdeck/CMakeLists.txt"
SCOPE_GUARD = ROOT / "tools/coverage_scope_guard.py"
CLI = ROOT / "tools/cyberdeck_http_cli.py"
CODEMAP = ROOT / "code-map.md"
README = ROOT / "README.md"
README_PT = ROOT / "README.pt-BR.md"
ARCH = ROOT / "docs/ARCHITECTURE.md"
ARCH_PT = ROOT / "docs/ARCHITECTURE.pt-BR.md"
MANUAL_HTTP = ROOT / "tests/manual/http-control-validation.pt-BR.md"
MANUAL_IMAGE = ROOT / "tests/manual/image-viewer-validation.pt-BR.md"
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


def test_route_budget(http_server):
    start = function_body(http_server, "static esp_err_t start_server")
    table_start = start.find("const httpd_uri_t uris[]")
    require(table_start != -1, "the LAN server must declare its route table")
    handlers = len(re.findall(r"\{\.uri\s*=", start[table_start:]))
    require(f"config.max_uri_handlers = {handlers};" in start,
            "max_uri_handlers must equal the number of registered routes")
    pattern = (r"\.uri\s*=\s*\"/control/upload\"\s*,\s*\.method\s*=\s*HTTP_POST\s*,"
               r"\s*\.handler\s*=\s*cyberdeck_control::upload_handler")
    require(re.search(pattern, start) is not None, "POST /control/upload must be registered")
    require("httpd_stop(s_server)" in start and "return ESP_FAIL" in start,
            "route registration must keep its fail-closed rollback")
    require("upload_handler" in SERVER_HDR.read_text(encoding="utf-8"),
            "the upload handler must be declared in the control server header")


def test_handler_order(server):
    body = function_body(strip_comments(server), "esp_err_t upload_handler")
    require('method_reply(req, "POST")' in body, "upload must reject non-POST methods")
    require("socket_is_local_peer" in body, "upload must keep the LAN peer filter")
    require("if (!ready())" in body, "upload must refuse a channel that is not ready")
    require("req->content_len == 0" in body, "upload must reject an empty body")
    require("req->content_len > k_max_upload_bytes" in body,
            "upload must bound the declared body")
    require('"Connection", "close"' in body,
            "rejected/aborted uploads must not leave the connection reusable")
    require("std::try_to_lock" in body and "s_upload_mutex" in body,
            "upload must be single-flight with a non-blocking try-lock")
    require("validate_upload_name(name" in body, "upload must validate the basename")
    require("parse_upload_size" in body, "upload must parse the declared size strictly")
    require("hex_decode(digest_text" in body, "upload must parse the declared digest")
    require("if (declared != req->content_len)" in body,
            "the declared size must match Content-Length")

    auth = body.index("authorize_upload(req")
    sanitize = body.index("validate_upload_name(")
    open_temp = body.index("upload_files_open(")
    receive = body.index("receive_upload(")
    require(sanitize < auth, "the basename must be validated before authentication")
    require(auth < open_temp, "authentication must precede opening the temporary")
    require(auth < receive, "authentication must precede the upload transaction")
    # TEST-UPLOAD-AUTH-12: o MESMO basename validado e autenticado e publicado.
    require(re.search(r"authorize_upload\(req,\s*name\s*,", body) is not None,
            "the handler must authorize the same validated basename it publishes")
    # TEST-UPLOAD-AUTH-12: o buffer validado e autenticado e o MESMO que vai
    # para o sink de publicacao (nao uma copia nem outro literal).
    require(re.search(r"receive_upload\([^;]*,\s*name\s*,", body, re.DOTALL) is not None,
            "TEST-UPLOAD-AUTH-12: the handler must publish the authenticated basename")
    for forbidden in ("::open(", "::fopen(", "::openat(", "::rename(", "::unlink("):
        require(forbidden not in body, f"the handler must not perform {forbidden} directly")

    require("cyberdeck_sd_io::acquire_global(cyberdeck_sd_io::access::write)" in body,
            "upload must hold a WRITE lease on the SD coordinator")
    require("error::storage_unavailable" in body,
            "a closed/fenced SD gate must answer storage_unavailable")
    require("record_global_persistence_failure" in body,
            "publication/fsync failure must record the persistence failure")
    require("format_upload_json" in body, "upload must return the deterministic JSON")
    require("screen_off_is_active" not in body,
            "upload is FS-only and must not copy the screen-off refusal of /control/type")
    require("X-CTL-Name" in server and "X-CTL-Size" in server and "X-CTL-Digest" in server,
            "upload metadata headers must be named")
    require("s_upload_scratch" in body and "k_upload_chunk_bytes" in server,
            "the body must stream through the fixed scratch buffer")
    for forbidden in ("malloc(req->content_len", "new char[req->content_len",
                      "resize(req->content_len", "malloc(declared", "new std::uint8_t[declared"):
        require(forbidden not in server, f"no body-sized allocation is allowed: {forbidden}")

    auth_body = function_body(strip_comments(server), "error authorize_upload")
    require("verify_upload(s_nonces, s_key, sizeof(s_key)" in auth_body,
            "upload must verify with the dedicated control key")
    require(re.search(r"error authorize_upload\(([^)]*)\)", strip_comments(server)) is not None
            and "const char *name" in
            re.search(r"error authorize_upload\(([^)]*)\)", strip_comments(server)).group(1),
            "TEST-UPLOAD-AUTH-12: authorize_upload must take the validated basename")
    auth_call = re.search(r"verify_upload\(([^;]*)\);", auth_body, re.DOTALL)
    require(auth_call is not None and "name" in auth_call.group(1),
            "TEST-UPLOAD-AUTH-12: the metadata authentication must include the basename")
    require("std::lock_guard<std::mutex> lock(s_mutex)" in auth_body,
            "metadata authentication must take the key mutex")
    require("httpd_req_recv" not in auth_body,
            "the upload body must never be read under the key mutex")

    worker_read = function_body(strip_comments(server), "int http_upload_read")
    require("httpd_req_recv" in worker_read, "the stream reader must use httpd_req_recv")
    require("HTTPD_SOCK_ERR_TIMEOUT" in worker_read and "return -1" in worker_read,
            "a socket timeout must surface as -1 for the pure secondary budget")
    require("timeouts" not in worker_read,
            "the consecutive budget lives in the pure module, not in the adapter")

    # TEST-UPLOAD-STREAM-11: orcamento absoluto por request, fonte de tempo
    # injetada no modulo puro e unica leitura de relogio no adapter.
    deadline_now = function_body(strip_comments(server), "std::int64_t upload_stream_now")
    require(deadline_now.count("esp_timer_get_time()") == 1,
            "the upload stream clock must be the single esp_timer_get_time() site")
    require("upload_clock" in body and "upload_stream_now" in body,
            "the upload handler must inject the monotonic clock into the transaction")
    require(re.search(r"receive_upload\([^;]*clock", body, re.DOTALL) is not None,
            "receive_upload must receive the injected clock")
    require("k_max_recv_timeouts" in function_body(strip_comments(server), "bool read_body"),
            "the text channel keeps its own consecutive timeout budget")


def test_protocol(protocol, header):
    header_text = header
    require('k_upload_domain[] = "cyberdeck-control-upload/v1"' in header_text,
            "the upload MAC domain must be declared")
    require("k_upload_uri[] = \"/control/upload\"" in header_text,
            "the upload URI constant must be declared")
    require("build_upload_message(" in header_text and "verify_upload(" in header_text,
            "the upload message and verifier must be declared")

    # TEST-UPLOAD-AUTH-12: o basename e autenticado (plano aprovado 17:29)
    build_decl = re.search(r"bool build_upload_message\(([^;]*)\);", header_text, re.DOTALL)
    require(build_decl is not None, "build_upload_message must be declared in the header")
    require(re.search(r"const char \*name", build_decl.group(1)) is not None,
            "TEST-UPLOAD-AUTH-12: the upload MAC message must carry the target basename")
    verify_decl = re.search(r"error verify_upload\(([^;]*)\);", header_text, re.DOTALL)
    require(verify_decl is not None, "verify_upload must be declared in the header")
    require(re.search(r"const char \*name", verify_decl.group(1)) is not None,
            "TEST-UPLOAD-AUTH-12: verify_upload must receive the authenticated basename")

    build = function_body(strip_comments(protocol), "bool build_upload_message")
    recipe = re.search(r'snprintf\(out, capacity,\s*"((?:[^"\\]|\\.)*)"', build)
    require(recipe is not None, "the upload message must be formatted in one place")
    require(recipe.group(1).count("%") == 7,
            "TEST-UPLOAD-AUTH-12: the upload message must bind seven values "
            "(domain, nonce, method, URI, name, size, digest)")
    require(recipe.group(1).count("\\n") == 6,
            "TEST-UPLOAD-AUTH-12: the upload message fields must stay separated")
    require('"%s\\n%s\\n%s\\n%s\\n%llu\\n%s"' not in build,
            "TEST-UPLOAD-AUTH-12: the five-value message layout is not enough")
    require("k_upload_domain" in build, "the upload message must start with the domain")
    verify = function_body(strip_comments(protocol), "error verify_upload")
    require("build_upload_message(" in verify, "verify_upload must use the upload message")
    verify_call = re.search(r"build_upload_message\(([^;]*)\);", verify, re.DOTALL)
    require(verify_call is not None, "verify_upload must build the upload message")
    require("name" in verify_call.group(1),
            "TEST-UPLOAD-AUTH-12: verify_upload must forward the basename into the message")
    require("store.pending(" in verify and "store.consume(" in verify,
            "verify_upload must keep the single-use nonce rules")
    require(verify.index("constant_time_equal(") < verify.index("store.consume("),
            "the upload nonce must only be consumed after a valid MAC")
    require("hex_decode(body_digest_hex" in verify,
            "the declared digest must be canonical lowercase hex")
    text_build = function_body(strip_comments(protocol), "bool build_message")
    require("k_upload_domain" not in text_build,
            "the text channel must not share the upload domain")

    status = function_body(strip_comments(protocol), "int http_status")
    for label, code in (("invalid_path", 400), ("conflict", 409), ("checksum", 422),
                        ("storage_unavailable", 503), ("io_error", 500)):
        block = re.search(rf"case error::{label}:\s*return (\d+);", status)
        require(block is not None and int(block.group(1)) == code,
                f"error::{label} must map to HTTP {code}")


def test_deadline(upload, upload_header):
    """TEST-UPLOAD-STREAM-11: absolute per-request budget in the pure module."""
    require("k_max_upload_duration_us = 120'000'000" in upload_header,
            "the total upload duration must be a header constant")
    require("k_upload_max_consecutive_timeouts = 3" in upload_header,
            "the consecutive recv budget must stay as a secondary defense")
    for token in ("std::int64_t (*now_us)(void *context)", "struct upload_clock"):
        require(token in upload_header, f"the clock seam must be declared: {token}")

    receive = function_body(strip_comments(upload), "upload_status receive_upload")
    require("clock.now_us == nullptr" in receive, "a missing clock must fail closed")
    require("clock.now_us(clock.context) - started_us >= k_max_upload_duration_us" in receive,
            "the deadline must be absolute and compared inclusively at the boundary")
    require("declared_size" not in receive.split("k_max_upload_duration_us", 1)[1].split(";", 1)[0],
            "the duration must not be derived from the declared size")
    loop = receive[receive.index("while ("):]
    require(loop.count("clock.now_us(") == 1,
            "at most one clock read per loop iteration")
    require("consecutive_timeouts" in loop and "k_upload_max_consecutive_timeouts" in loop,
            "the consecutive timeout budget must be enforced while streaming")
    for banned in (r"\btime\s*\(", "clock_gettime", "gettimeofday", r"\blocaltime\s*\(",
                   r"\bgmtime\s*\(", "std::chrono", "esp_timer", r"\busleep\s*\(",
                   r"\bvTaskDelay\s*\("):
        require(re.search(banned, upload) is None,
                f"the pure upload module must not use a wall clock or sleep: {banned}")


def test_publication(upload, upload_header):
    require("k_max_upload_bytes = 3u * 1024u * 1024u" in upload_header,
            "the upload cap must stay a header constant")
    require("k_max_upload_bytes < cyberdeck_image::k_max_file_bytes" in upload_header,
            "the upload cap must stay below the viewer file cap")
    require("k_max_upload_name_bytes = cyberdeck_image::k_max_name_bytes" in upload_header,
            "the basename limit must derive from the viewer")
    require('k_upload_directory[] = "/sdcard/screenshots"' in upload_header,
            "the upload directory must be fixed and confined")
    require("k_upload_chunk_bytes = 4096" in upload_header,
            "the streaming chunk must be a fixed compile-time constant")

    receive = function_body(strip_comments(upload), "upload_status receive_upload")
    require("files.target_exists(" in receive and "open_temp(" in receive,
            "receive_upload must check the target and open the temp")
    require("files.close_temp(files.context, total)" in receive,
            "receive_upload must fsync/close before publishing")
    require("constant_time_equal(actual, expected_digest" in receive,
            "receive_upload must compare the incremental digest")
    require("files.publish(files.context, temp_name, name)" in receive,
            "receive_upload must publish through the sink")
    require("files.discard(files.context, temp_name)" in receive,
            "every failure must clean its own temp")

    open_temp = function_body(strip_comments(upload), "bool hook_open_temp")
    require("O_CREAT | O_EXCL" in open_temp, "the temp must be created exclusively")
    require("O_NOFOLLOW" in open_temp, "the temp must not follow a symlink")
    require("k_upload_temp_attempts" in open_temp, "temp collisions must be bounded")
    require(re.search(r"if\s*\(\s*errno\s*!=\s*EEXIST\s*\)\s*return\s+false\s*;", open_temp) is not None,
            "an occupied temp name must never be removed or reused")

    close_temp = function_body(strip_comments(upload), "bool hook_close_temp")
    require("::fsync(" in close_temp, "the temp must be fsynced before publication")
    require("fstat" in close_temp and "expected_bytes" in close_temp,
            "close must verify the written size")

    publish = function_body(strip_comments(upload), "int hook_publish")
    require("::link(" in publish and "::unlink(" in publish,
            "host publication must link atomically without clobber")
    require("::rename(" in publish and "::stat(" in publish,
            "ESP publication must reject an observed target before rename")
    require("k_upload_publish_exists" in publish, "publication must report a conflict")
    require("if (!files->owns_temp || files->temp_name != temp_name)" in publish,
            "publication must refuse a temp this instance does not own")

    discard = function_body(strip_comments(upload), "void hook_discard")
    require("files->temp_name != temp_name" in discard,
            "discard must only remove the owned temp")
    require("::unlink(" in function_body(strip_comments(upload), "void discard_owned"),
            "discard must remove exactly the owned path")


def test_wiring(cmake, scope_guard, cli, version, makefile, system_apps):
    require("src/apps/control/cyberdeck_control_upload.cpp" in cmake,
            "the upload TU must be registered in CMake")
    upload_tu = "components/cyberdeck/src/apps/control/cyberdeck_control_upload.cpp"
    allowlist_block = scope_guard.split("ALLOWLIST = {", 1)[1].split("FORBIDDEN_ALLOWLIST", 1)[0]
    require(f'"{upload_tu}"' not in allowlist_block,
            "the pure upload TU must be covered by gcovr, not allowlisted")
    required_block = scope_guard.split("REQUIRED_COVERED = {", 1)[1].split("}", 1)[0]
    require(f'"{upload_tu}"' in required_block,
            "the pure upload TU must stay in the guard's required-covered list")

    require("upload" in cli and "--name" in cli, "the CLI must expose upload with --name")
    require("X-CTL-Size" in cli and "X-CTL-Digest" in cli and "X-CTL-Name" in cli,
            "the CLI must send the upload metadata headers")
    require("UPLOAD_URI = \"/control/upload\"" in cli, "the CLI must target the upload route")
    domain = re.search(r'UPLOAD_DOMAIN = "([^"]+)"', cli)
    firmware_domain = re.search(r'k_upload_domain\[\] = "([^"]+)"',
                                PROTOCOL_HDR.read_text())
    require(domain is not None and firmware_domain is not None
            and domain.group(1) == firmware_domain.group(1),
            "the CLI and firmware upload domains must be identical")
    cli_limit = re.search(r"UPLOAD_MAX_BYTES = (\d+) \* (\d+) \* (\d+)", cli)
    firmware_limit = re.search(r"k_max_upload_bytes = (\d+)u \* (\d+)u \* (\d+)u",
                               UPLOAD_HDR.read_text())
    require(cli_limit is not None and firmware_limit is not None, "upload limits must parse")
    cli_value = int(cli_limit.group(1)) * int(cli_limit.group(2)) * int(cli_limit.group(3))
    firmware_value = int(firmware_limit.group(1)) * int(firmware_limit.group(2)) * int(
        firmware_limit.group(3))
    require(cli_value == firmware_value, "the CLI limit must equal the firmware limit")
    require("upload_name_problem" in cli, "the CLI must validate the basename locally")
    require("not a regular file" in cli and "the device limit is" in cli,
            "the CLI must refuse invalid files before opening the network")
    # TEST-UPLOAD-AUTH-12: o cliente assina o basename junto com tamanho e digest.
    signer = re.search(r"def upload_signed_headers\(key, nonce, (\w+), size, digest_hex\)", cli)
    require(signer is not None, "the CLI must sign the upload basename")
    signer_body = cli.split("def upload_signed_headers", 1)[1].split("\nclass ", 1)[0]
    require(f"{{{signer.group(1)}}}" in signer_body,
            "the CLI upload MAC message must include the basename")
    require(re.search(r"upload_signed_headers\(self\.key, nonce, target", cli) is not None,
            "the CLI must pass the validated target name to the signer")

    screenshot = re.search(r"s_screenshot\{.*?start_screenshot", system_apps, re.DOTALL)
    require(screenshot is not None, "the screenshot/control service must stay declared")
    versions = re.findall(r'"([0-9]+\.[0-9]+\.[0-9]+)"', screenshot.group(0))
    require(bool(versions) and tuple(int(p) for p in versions[-1].split(".")) >= (1, 1, 1),
            "the screenshot/control service version must reflect the upload route")

    match = re.fullmatch(r"([0-9]+)\.([0-9]+)\.([0-9]+)", version)
    require(match is not None, "version.txt must be MAJOR.MINOR.PATCH")
    require(tuple(int(part) for part in match.groups()) >= (1, 16, 0),
            "the firmware MINOR must reflect the new transport")

    require("test_control_upload_protocol_bin" in makefile, "the pure upload test must be built")
    require("test_control_upload/" in makefile, "the upload test needs incremental objects")
    require("test_control_upload_protocol:" in makefile, "the pure upload target must exist")
    require("test_control_upload_contract:" in makefile, "the upload contract target must exist")
    for target in ("test_control_upload_protocol", "test_control_upload_contract"):
        require(f"--no-print-directory {target}" in makefile,
                f"{target} must run in the aggregate")
    require(re.search(r"rm -rf[^\n]*test_control_upload", makefile) is not None,
            "clean must remove the upload test artifacts")
    phony_line = next((line for line in makefile.splitlines()
                       if line.startswith(".PHONY:") and "test_control_protocol" in line), "")
    for target in ("test_control_upload_protocol", "test_control_upload_contract"):
        require(target in phony_line, f"{target} must be phony")


def test_docs(codemap, readme, readme_pt, arch, arch_pt, manual_http, manual_image):
    for name, text in (("code-map.md", codemap), ("README.md", readme),
                       ("README.pt-BR.md", readme_pt)):
        require("cyberdeck_control_upload" in text or "upload" in text,
                f"{name} must document the upload transport")
    require("REQ-UPLOAD" in codemap and "AC-UPLOAD" in codemap,
            "code-map must carry the upload traceability matrix")
    require("/control/upload" in arch and "/control/upload" in arch_pt,
            "both architecture docs must list the upload route")
    require("upload" in manual_http, "the HTTP manual must cover the upload scenarios")
    require("upload" in manual_image, "the image-viewer manual must cover the large fixture")


def main():
    server = SERVER.read_text(encoding="utf-8")
    protocol = PROTOCOL.read_text(encoding="utf-8")
    upload = UPLOAD.read_text(encoding="utf-8")
    test_route_budget(HTTP_SERVER.read_text(encoding="utf-8"))
    test_handler_order(server)
    test_deadline(upload, UPLOAD_HDR.read_text(encoding="utf-8"))
    test_protocol(protocol, PROTOCOL_HDR.read_text(encoding="utf-8"))
    test_publication(upload, UPLOAD_HDR.read_text(encoding="utf-8"))
    test_wiring(CMAKE.read_text(encoding="utf-8"), SCOPE_GUARD.read_text(encoding="utf-8"),
                CLI.read_text(encoding="utf-8"), VERSION.read_text(encoding="utf-8").strip(),
                MAKEFILE.read_text(encoding="utf-8"), SYSTEM_APPS.read_text(encoding="utf-8"))
    test_docs(CODEMAP.read_text(encoding="utf-8"), README.read_text(encoding="utf-8"),
              README_PT.read_text(encoding="utf-8"), ARCH.read_text(encoding="utf-8"),
              ARCH_PT.read_text(encoding="utf-8"), MANUAL_HTTP.read_text(encoding="utf-8"),
              MANUAL_IMAGE.read_text(encoding="utf-8"))
    print("PASS: authenticated upload contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
