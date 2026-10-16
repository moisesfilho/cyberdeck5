# Network diagnostics traceability (controller, adapters, runtime app)

| Requirement / acceptance | Test | Evidence |
| --- | --- | --- |
| REQ-ND-01 / AC-ND-01 explicit operation, admission, validation, grant, busy/rate boundary | TEST-ND-01 `admission_and_validation`, `operation_specific_validation`, `test_netdiag_app` | Real controller and app TUs; independent discriminant validation, invalid input has no network calls, no-grant rejection, concurrent busy rejection, reject at 29,999 ms and admit at exact 30,000 ms |
| REQ-ND-02 / AC-ND-02 DNS deadline and cancellation lifetime | TEST-ND-02 controller and `test_netdiag_dns_adapter` | Real controller/DNS adapter TUs; inline/cache/async completion, malformed/family/enqueue failures, single-slot busy rejection, timeout slot retention, late callback release and deadline-boundary race |
| REQ-ND-03 / AC-ND-03 TCP dispatch/failure | TEST-ND-03 controller and `test_netdiag_tcp_adapter` | DNS-for-connect then TCP only; real TCP TU covers immediate/nonblocking success, refusal, SO_ERROR, deadline/select failures, input validation and descriptor ownership; no payload/HTTP behavior implied |
| REQ-ND-04 / AC-ND-04 HTTPS HEAD result/deadline | TEST-ND-04 `test_netdiag_https_adapter` | Real HTTPS TU checks URL/authority, HEAD, async sequential EAGAIN with exactly one scheduler delay before each retry, EAGAIN-to-success and EAGAIN-to-deadline without another perform, CRT bundle, redirect disabled, body rejection, deadline and cleanup; API shims do not prove TLS/network behavior |
| REQ-ND-05 / AC-ND-05 HTTP 405 final/no GET fallback | TEST-ND-05 `test_netdiag_https_adapter`, `test_netdiag_app` | Exactly one HEAD; redirect disabled, no GET fallback, final 405 preserved; perform/TLS errors fail closed |
| REQ-ND-06 / AC-ND-06 grant/cancel/stale-safe publication | TEST-ND-06 controller and `test_netdiag_app` | Controller cancellation/stale-token rejection; app stop revokes copied grant before worker completion and revoked worker cannot enter DNS adapter |
| REQ-ND-07 / AC-ND-07 app/runtime/adapters lifecycle integration | TEST-ND-07 `test_netdiag_app` | Real app, controller, runtime, DNS/TCP/HTTPS adapters linked: dependency registration/start, SSH command coexistence, status, invalid input, operation dispatch, concurrent admission, completion/result, cancellation, worker-create failure, stop timeout, grant revocation, cleanup and restart quarantine. Current focused rerun: PASS, 47 checks. |
| REQ-ND-08 / AC-ND-08 actual TU coverage and fail-closed guard | TEST-ND-08 `test_coverage_scope_guard.py` and CI-equivalent aggregate | App and all four controller/adapter TUs are required covered, never allowlisted; the counterfactual missing-app fixture fails. Focused tests contribute real TU coverage to the aggregate. With GCC/gcov 13.4: 86.5% lines, 94.9% functions, 59.3% branches (thresholds 80/94/59). Quality Gate passed for `dbe3a94` (`38916411814`) and `8e41338` (run `38918012957`, attempt 2); later commits require a new run. |

## TEST-ND-07 timeout quarantine regression

The deterministic scheduler lets the harness retain a real app worker. On
stop-timeout, its copied runtime grant is revoked immediately, the worker and
controller remain owned, and new operations/restart are rejected. Only after
the worker callback completes may stop cleanup and restart proceed. The focused
app integration target passes 47 checks against the current implementation,
including retained callback grant revocation and restart quarantine.

Controller dispatch assertions require exact port-call counts: DNS calls DNS
only; TCP calls DNS-for-connect then TCP, never HTTPS; HTTPS calls only HTTPS,
not separate DNS/TCP. They do not prove platform network behavior.

The DNS adapter harness links its production TU and shims only tcpip/DNS/
FreeRTOS/clock primitives. It verifies callback lifetime and slot ownership,
not lwIP scheduling or a live network. The TCP harness links its production TU
and shims socket/fcntl/select/getsockopt/close/clock; it does not prove OS socket
semantics or reachability. The HTTPS harness links its production TU and shims
HTTP-client/CRT-bundle/clock; it does not prove TLS validation or real HTTP.
ESP-IDF HTTP header metadata has no guaranteed hard cap; this accepted risk is
not represented as bounded by these tests.

TEST-ND-08 status: the scope guard requires the real app/controller/DNS/TCP/HTTPS
TUs as covered and rejects allowlisting; its negative fixture proves a missing
app TU fails. The aggregate runs the app and focused controller/adapter targets
with `--coverage` before gcovr, so the JSON includes their executed production
paths. GCC/gcov 13.4 passes the local aggregate gate (59.3% branches); GCC/gcov
15.2 reports a different branch count (57.4% on the previous snapshot), so the
two toolchains are not interchangeable. The remote gate passed for `8e41338`
(run `38918012957`, attempt 2). Attempt 1 failed in `test_time_sync.cpp:494`
before coverage due to an asynchronous event assertion; no netdiag test failed.

## Targeted device validation (2026-10-16, `/dev/ttyACM0`)

Flashed `8e41338` using `idf.py flash`, hash verified; `sys.info` returned
`fw_version=8e41338`, `idf_version=v5.5.5`, `chip=esp32p4`, no safe mode, and
continuous uptime through the scenarios. The Serial-JTAG CLI accepted commands
via `ui.type`; every observation below was verified in `term.dump`:

- `netdiag status` returned `interface=WIFI_STA_DEF`, IP, gateway, DNS and RSSI;
  initial `netdiag result` returned `state=idle`.
- `netdiag dns example.com` completed in 14 ms with IPv4 `172.66.147.243`.
- Immediate `netdiag tcp example.com 443` was rejected by rate limiting;
  after 30 seconds the same explicit command completed in 59 ms with IPv4.
- `netdiag https https://example.com` completed in 1210 ms with HTTP 200.
  `netdiag https http://example.com` returned usage without starting an operation.
- A second explicit HTTPS request followed by `netdiag cancel` produced
  `state=cancelled operation=https duration_ms=191 error=cancelled`.
- In a second live session with the same flashed firmware, a HEAD to
  `https://httpbingo.org/status/405` completed with `http_status=405` (1849 ms).
  The `/head` endpoint completed with `http_status=200` (1755 ms), whereas the
  host's GET to the same endpoint returned 405. This distinguishes the method
  functionally, but it is not a packet capture.
- `https://httpbingo.org/redirect-to?url=/get&status_code=302` ended in
  `state=failed error=https_failed` (1791 ms); it did not publish a followed
  response. The host confirmed that endpoint's HEAD returns 302.
- `https://expired.badssl.com/` and `https://self-signed.badssl.com/` both ended
  in `state=failed error=timeout` after ~5055 ms in the first run. Those results
  alone were inconclusive about certificate rejection.
  On-device DNS for `self-signed.badssl.com` completed in 2 ms (104.154.89.105)
  and TCP to port 443 completed in 278 ms, narrowing the inconclusive behavior
  to the HTTPS stage. In a subsequent single-port serial capture, the same
  self-signed URL logged `esp-x509-crt-bundle: No matching trusted root
  certificate found`, `Failed to verify certificate`, and
  `esp-tls-mbedtls: mbedtls_ssl_handshake returned -0x3000`. The corresponding
  `netdiag result` was `state=failed operation=https duration_ms=576
  error=https_failed`, with no HTTP status. The host also rejected this
  certificate. `sys.info` afterward reported continuous uptime, no safe mode
  and `fw_version=8e41338`.

This is a targeted live-network smoke test, not a packet capture. The host
harnesses establish HEAD/405/no-GET logic; the device functionally distinguished
HEAD from GET, observed a final 405, and logged certificate verification failure
for a self-signed endpoint. It did not capture the wire-level method or exhaust
all teardown races. A failed
parallel invocation of two CLI processes on the same port yielded a transport
error; repeating `term.dump` and `sys.info` sequentially succeeded.

Remaining TEST-ND-07/acceptance gaps: harnesses use deterministic scheduler and
API shims, not the ESP-IDF runtime, live sockets, DNS, TLS, or an HTTP server.
An expired certificate was not conclusively characterized, but the self-signed
certificate rejection was demonstrated by the TLS log and typed failure. A
packet-level method capture and the full serial bridge validation procedure
remain outside this targeted device validation. The tested
restart path covers the worker
timeout/reap quarantine; it does not establish exhaustive races for every
platform task/runtime failure path. Header metadata has no guaranteed hard cap.
