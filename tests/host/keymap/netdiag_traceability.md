# Netdiag test traceability

Scope: core and production DNS/TCP/HTTPS adapter host slices. Each focused binary
links its real production TU while substituting only its external ESP-IDF/lwIP
system API boundaries. No real network or sleeps are used. The TCP fake executes
the FreeRTOS worker synchronously for deterministic cases and uses a real
deferred thread plus condition-variable gate for cancel/worker interleavings;
HTTPS captures
the actual config/method/header and drives the production polling state machine.

| Requirement / acceptance | Test | Evidence |
| --- | --- | --- |
| REQ-ND-01 / AC-ND-02; TEST-ND-01 | `test_netdiag_runtime.cpp::parser_cases()` | DNS label boundary 63 accepted/64 rejected, hostname boundary 253 accepted/254 rejected; leading/trailing dot and hyphen, empty labels, malformed DNS/HTTPS authorities rejected. Invalid DNS/HTTPS requests are denied by core admission without backend calls. |
| REQ-ND-02 / AC-ND-01; TEST-ND-02 | `test_netdiag_runtime.cpp::admission_and_lifecycle_cases()` | Idle, grant/invalid/missing-backend rejection; exactly-one enqueue and queued/running/completion/failure states. Status/result/cancel admission returns typed `invalid_command` without backend or controller-state side effects; rate limit, backward time, cancellation and stale generations are asserted. Backend spy is an enqueue/cancel seam only; this does not establish UI latency or backend nonblocking behavior. Includes submit failure terminal backend-unavailable, no interval consumed on failed submit, initial time zero, 999/1000 ms boundary, and backward-time rejection without mutation/backend call. Runtime executable total: 79 checks; `test_netdiag_runtime_asan` runs the same production-core behavior under ASan/UBSan. |
| REQ-ND-03 / AC-ND-03 (singleton ownership, callback lifetime, cancellation/quarantine); TEST-ND-03 | `test_netdiag_dns.cpp::callback_poll_concurrency_cases()` plus delayed/cancel cases | Actual DNS adapter TU with lwIP boundary shim and real `std::mutex` critical section: callback thread races owner-thread `poll`/`busy`; bounded condition-variable gates prove pending before publication, no slot reuse before callback, eventual exactly-once completion with intact typed IPv4 bytes, one-shot consumption, logical cancellation, and stale-result suppression. The owner may observe a logical cancelled result while the callback retains the slot. DNS normal binary runs under ASan/UBSan; `test_netdiag_dns_tsan` is a required Ubuntu CI gate. |
| REQ-ND-03 / AC-ND-03; TEST-ND-03 | `test_netdiag_dns.cpp` adapter lifecycle and callback scenarios (138 checks) | Actual DNS adapter TU with lwIP/tcpip boundary shim: reservation/order/arguments, IPv4/IPv6 bytes, immediate `ERR_OK`/error and pending resolution, null address, polling/one-shot consumption, cancellation/quarantine/release, scheduling failure and omitted callback. Hostname boundaries include null/empty, 63/64-byte labels, 253/254-byte names, leading/trailing dot/hyphen and empty labels; `example.com.` specifically returns `invalid_target` without slot reservation or tcpip scheduling. Direct inline-callback mode is a defensive seam scenario only: lwIP literal/cache resolution returns `ERR_OK` with an address, while only a pending resolver invokes its callback on the tcpip thread. Callback/poll concurrency uses a controlled host thread and proves adapter synchronization, not lwIP callback timing. A queued trampoline ID mismatch cannot be induced through the public API; duplicate callback delivery or a scheduler returning failure after invoking its callback would violate API ownership contracts and is not simulated. |
| REQ-ND-04 / AC-ND-04; TEST-ND-04 | `test_netdiag_tcp.cpp` (51 checks) | Real TCP adapter TU: invalid/null/zero endpoint, IPv4/IPv6 family/length, immediate and EINPROGRESS success, 3000 ms timeval, timeout/refused, syscall failures and close-once. Gated threaded worker asserts cancel returns while select is blocked, pending poll, no premature close, busy retained, stale token unknown, autonomous release only after owner closes, and subsequent start acceptance. |
| REQ-ND-05 / AC-ND-05; TEST-ND-05 | `test_netdiag_https.cpp` (45 checks) | Real HTTPS adapter TU: numeric target plus invalid URL/userinfo rejected before init; original hostname for CN and Host with nondefault port; CA bundle, CN validation, async HEAD, `disable_auto_redirect`. 405/301 returned as typed statuses; EAGAIN sequence performs same client twice, then token consumed without retry/fallback. |
| REQ-ND-05 / AC-ND-05; TEST-ND-06 | `test_netdiag_https.cpp` | EAGAIN retains single client/init; TLS/transport errors distinct; cancel during perform cleans up exactly once after return; busy/stale and partial setup cleanup. No request-cancel/body API. ESP-IDF header metadata unbounded risk accepted and not covered; no body/header/total-response bound claimed. |
| REQ-ND-06 / AC-ND-05; TEST-ND-07/08 | `test_netdiag_app_contract.py` (TEST-ND-08) | Structural guard checks app command routing, status snapshot fields, explicit missing values, single-flight nonblocking enqueue, 8192-byte configured stack, grant gates, stop/join ordering, registration, and that DNS/TCP/HTTPS calls delegate to the production adapters. This is not behavioral proof of the ESP-IDF app, actual worker scheduling, revocation races, or coexistence with SSH; those remain device/integration validation gaps. |

Run focused targets only: `make -B -C tests/host/keymap test_netdiag_runtime`
and `make -B -C tests/host/keymap test_netdiag_dns`; these build and execute.
The app-level structural guard is `make -B -C tests/host/keymap test_netdiag_app_contract`.
TCP and HTTPS adapters are independently exercised with
`make -B -C tests/host/keymap test_netdiag_tcp test_netdiag_https`; focused
sanitizer variants are `test_netdiag_tcp_asan` and `test_netdiag_https_asan`.
The TCP/HTTPS normal focused targets and both sanitizer targets are also run
explicitly in CI; their binaries compile the production adapters against host
boundary shims, not ESP-IDF networking or hardware.
The DNS normal target is ASan/UBSan-instrumented. Also run
`make -B -C tests/host/keymap test_netdiag_runtime_asan` for core ASan/UBSan
and `make -B -C tests/host/keymap test_netdiag_dns_tsan` (required CI target)
for DNS ThreadSanitizer, and `make -B -C tests/host/keymap test_netdiag_tcp_tsan`
for TCP worker/caller ThreadSanitizer coverage.
The aggregate host target also builds/runs both normal binaries, so the DNS TU
contributes gcov data for coverage scope (confirmed present in focused gcovr JSON).
The shim verifies adapter ownership synchronization,
not the platform FreeRTOS critical-section implementation or actual lwIP scheduling.
