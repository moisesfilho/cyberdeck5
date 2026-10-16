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
| REQ-ND-08 / AC-ND-08 actual TU coverage and fail-closed guard | TEST-ND-08 `test_coverage_scope_guard.py` and CI-equivalent aggregate | App and all four controller/adapter TUs are required covered, never allowlisted; the counterfactual missing-app fixture fails. Focused tests contribute real TU coverage to the aggregate. With GCC/gcov 13.4: 86.5% lines, 94.9% functions, 59.3% branches (thresholds 80/94/59). Quality Gate run `38916411814` passed for commit `dbe3a94`; any later commit requires a new run. |

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
two toolchains are not interchangeable. The remote gate passed for `dbe3a94`
(run `38916411814`); this does not validate any later commit or the device.

Remaining TEST-ND-07/acceptance gaps: harnesses use deterministic scheduler and
API shims, not the ESP-IDF runtime, live sockets, DNS, TLS, or an HTTP server.
Hardware network behavior, TLS certificate validation and real endpoint behavior
remain manual/device validation. The tested restart path covers the worker
timeout/reap quarantine; it does not establish exhaustive races for every
platform task/runtime failure path. Header metadata has no guaranteed hard cap.
