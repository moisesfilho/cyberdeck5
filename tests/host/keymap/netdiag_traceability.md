# Netdiag test traceability

Scope: core and DNS-adapter host slice. `test_netdiag_runtime` links only the
production `cyberdeck_netdiag_core.cpp`; `test_netdiag_dns` links only the
production `cyberdeck_netdiag_dns.cpp`, substituting lwIP/tcpip API boundaries.
Neither binary links both TUs. Adapter logic is not reimplemented. The core
backend spy remains an enqueue/logical-cancel seam. No real network or sleeps
are used.

| Requirement / acceptance | Test | Evidence |
| --- | --- | --- |
| REQ-ND-01 / AC-ND-02; TEST-ND-01 | `test_netdiag_runtime.cpp::parser_cases()` | DNS label boundary 63 accepted/64 rejected, hostname boundary 253 accepted/254 rejected; leading/trailing dot and hyphen, empty labels, malformed DNS/HTTPS authorities rejected. Invalid DNS/HTTPS requests are denied by core admission without backend calls. |
| REQ-ND-02 / AC-ND-01; TEST-ND-02 | `test_netdiag_runtime.cpp::admission_and_lifecycle_cases()` | Idle, grant/invalid/missing-backend rejection; exactly-one enqueue and queued/running/completion/failure states. Status/result/cancel admission returns typed `invalid_command` without backend or controller-state side effects; rate limit, backward time, cancellation and stale generations are asserted. Backend spy is an enqueue/cancel seam only; this does not establish UI latency or backend nonblocking behavior. Includes submit failure terminal backend-unavailable, no interval consumed on failed submit, initial time zero, 999/1000 ms boundary, and backward-time rejection without mutation/backend call. Runtime executable total: 79 checks; `test_netdiag_runtime_asan` runs the same production-core behavior under ASan/UBSan. |
| REQ-ND-03 / AC-ND-03 (singleton ownership, callback lifetime, cancellation/quarantine); TEST-ND-03 | `test_netdiag_dns.cpp::callback_poll_concurrency_cases()` plus delayed/cancel cases | Actual DNS adapter TU with lwIP boundary shim and real `std::mutex` critical section: callback thread races owner-thread `poll`/`busy`; bounded condition-variable gates prove pending before publication, no slot reuse before callback, eventual exactly-once completion with intact typed IPv4 bytes, one-shot consumption, logical cancellation, and stale-result suppression. The owner may observe a logical cancelled result while the callback retains the slot. DNS normal binary runs under ASan/UBSan; `test_netdiag_dns_tsan` is a required Ubuntu CI gate. |
| REQ-ND-03 / AC-ND-03; TEST-ND-03 | `test_netdiag_dns.cpp` adapter lifecycle and callback scenarios (138 checks) | Actual DNS adapter TU with lwIP/tcpip boundary shim: reservation/order/arguments, IPv4/IPv6 bytes, immediate `ERR_OK`/error and pending resolution, null address, polling/one-shot consumption, cancellation/quarantine/release, scheduling failure and omitted callback. Hostname boundaries include null/empty, 63/64-byte labels, 253/254-byte names, leading/trailing dot/hyphen and empty labels; `example.com.` specifically returns `invalid_target` without slot reservation or tcpip scheduling. Direct inline-callback mode is a defensive seam scenario only: lwIP literal/cache resolution returns `ERR_OK` with an address, while only a pending resolver invokes its callback on the tcpip thread. Callback/poll concurrency uses a controlled host thread and proves adapter synchronization, not lwIP callback timing. A queued trampoline ID mismatch cannot be induced through the public API; duplicate callback delivery or a scheduler returning failure after invoking its callback would violate API ownership contracts and is not simulated. |
| REQ-ND-03..08 / AC-ND-03..08; TEST-ND-04..08 | pending | TCP socket/timeout cleanup, HTTPS/TLS certificate/hostname/SNI/redirect/response behavior, app lifecycle/grant revocation, SSH coexistence/privacy, integration and device behavior remain pending. DNS API duplicate-callback behavior is not synthesized after a callback has released/reused singleton context: lwIP's one-callback ownership contract is a limitation. |

Run focused targets only: `make -B -C tests/host/keymap test_netdiag_runtime`
and `make -B -C tests/host/keymap test_netdiag_dns`; these build and execute.
The DNS normal target is ASan/UBSan-instrumented. Also run
`make -B -C tests/host/keymap test_netdiag_runtime_asan` for core ASan/UBSan
and `make -B -C tests/host/keymap test_netdiag_dns_tsan` (required CI target)
for DNS ThreadSanitizer.
The aggregate host target also builds/runs both normal binaries, so the DNS TU
contributes gcov data for coverage scope (confirmed present in focused gcovr JSON).
The shim verifies adapter ownership synchronization,
not the platform FreeRTOS critical-section implementation or actual lwIP scheduling.
