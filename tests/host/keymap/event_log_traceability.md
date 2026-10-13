# Event log runtime harness — behavior characterization and M2 quiesce

The host executable links the real `event_log.cpp` TU. It characterizes existing
behavior, including deterministic caller timeout/lifetime and durable queue
backpressure runtime cases. Independent argv scenarios preserve process isolation.

| Requirement / AC | TEST | Baseline evidence |
|---|---|---|
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-01 | TEST-EVENT-QUIESCE-PREINIT | `preinit` process verifies quiesce returns `ESP_ERR_INVALID_STATE` before initialization. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-01/03/04 | TEST-QUIESCE-M2-PARTIAL-INIT | `partial-init` injects failure of binary worker creation after the text worker starts. Real init restores the serial hook, rejects admission/reinit, preserves queue handles, and quiesce stops the sole worker. Bounded completion and join precede idempotent CLOSED/no-reopen assertions. No log file is created. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-02/03 | TEST-QUIESCE-M2-READERS / TEST-QUIESCE-M2-TIMEOUT-RETRY | `quiesce-fence` blocks real TU text snapshot at wrapped `fread`; zero-budget quiesce times out, release drains the reader, retry succeeds, and writers/readers remain fenced. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-03/04 | TEST-QUIESCE-M2-DRAIN / TEST-QUIESCE-M2-OWNERSHIP-CLOSE / TEST-QUIESCE-M2-CLOSED-IDEMPOTENCE / TEST-QUIESCE-M2-NO-REOPEN | Same isolated scenario observes binary owner close, successful drain, idempotent quiesce, and rejected init after CLOSED. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-04 | TEST-QUIESCE-M2-FAILURE-STICKY-CLOSE | `sticky-close` injects failure only for binary log FILE close; repeated quiesce returns `ESP_FAIL`, init remains rejected. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-02 | TEST-QUIESCE-M2-DURABLE-ACK / TEST-QUIESCE-M2-CALLER-TIMEOUT | Existing real-TU default verifies ACK after binary fsync and accepted caller timeout does not cancel persistence (exactly once). |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-02 | TEST-QUIESCE-M2-DURABLE-TIMEOUT-RACE | Isolated `durable-timeout-quiesce` submits an accepted durable request, holds its binary fsync, witnesses caller timed-wait registration, advances fake ticks to caller timeout, and starts quiesce before releasing fsync. Quiesce must time out without releasing/closing the owner; after release, the caller remains timed out, the token persists once, the owner drains/closes once, and retry quiesce succeeds. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-04 | TEST-QUIESCE-M2-FAILURE-STICKY-WRITE / TEST-QUIESCE-M2-FAILURE-STICKY-FSYNC | Isolated `sticky-write` injects the ordinary void API's binary fwrite failure and waits for the wrapper failure witness; `sticky-fsync` does the same after accepted ordinary write. Both assert initial quiesce and retry return `ESP_FAIL`, and init cannot reopen. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-05 | TEST-QUIESCE-M2-PROJECTION | Three isolated runtime processes inject events.txt `fwrite`, `fsync`, or `fclose` failure while the real text worker drains on quiesce. Each requires successful/idempotent quiesce and CLOSED/no-reopen, the binary event exactly once, and binary `fsync` plus owner close; text failure is best-effort and does not become sticky binary failure. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-06 | TEST-QUIESCE-M2-WORKER-TIMEOUT-RETRY | Isolated `worker-timeout-retry` holds the real worker in wrapped binary fsync, asserts a bounded quiesce timeout, releases the accepted write, then succeeds on an independent 1000 ms budget and checks the record occurs exactly once. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-07/08 | TEST-QUIESCE-M2-WORKER-COMPLETION | `worker-completion` enqueues an identifiable event, blocks the real `events.txt` fwrite wrapper after projection work reaches it, starts quiesce concurrently and observes it cannot have succeeded before release; successful quiesce follows release/drain. Direct startup-name-to-`std::thread::id` correlation (never hashes) maps the actual `s_quiesce_changed` handle, requires text/binary owner close before each give and no file/queue I/O by either worker afterwards. It proves one accepted projection drains; it does not prove queue saturation/drain of multiple projections. Race and partial-init remain open. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-07/08 | TEST-QUIESCE-M2-RACE-BOUNDARIES | `quiesce-race` holds the real binary worker in wrapped fsync; registers two independent real-TU callers at 20 ms and 200 ms fake-tick budgets before advancing 30 ms. A must time out while B remains pending and binary owner remains open; release permits B success, one close, and exactly-once record persistence. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-03 | TEST-EVENT-VPRINTF-FENCE | Invokes the real callback installed by init with a shim-captured original serial vprintf; after fence, serial output contains the formatted message while binary file size remains unchanged. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-01 | TEST-HARNESS-INIT-01 | Real init creates both workers against a host SD directory; normal append reaches the binary log. Admission/fence is not implemented in this phase. |
| AC-GLOBAL-LOG-02 | TEST-HARNESS-DURABLE-ACK-01 | Real durable API returns success only after worker completion; wrapped fwrite/fsync audit asserts write → fsync → caller ACK, and a test-only observer confirms accepted-request release after ACK. Binary log contains normal and durable records. |
| AC-GLOBAL-LOG-03 | TEST-HARNESS-FAILURE-01 | Reserved for later quiesce lifecycle; this phase does not claim drain/timeout behavior. |
| AC-GLOBAL-LOG-04 | TEST-HARNESS-PROJECTION-01 | Existing structural contracts retain projection best-effort, rotation/read, durable timeout and queue ordering; runtime target starts projection worker. |
| TEST-HARNESS-FSYNC-ISOLATION-01 | TEST-HARNESS-FSYNC-ISOLATION-01 | Runtime wrapper canonicalizes `/proc/self/fd/<n>` and compares only the unique root's `logs/events.log`; the binary fsync blocks until release while projection fsync is observed to complete without consuming the barrier. Failure injection remains binary-only. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-02 | TEST-EVENT-DURABLE-TIMEOUT-LIFETIME | Real TU request blocks in binary fsync; advancing fake ticks by 250 ms must return `ESP_ERR_TIMEOUT` while release observer remains unchanged. After releasing fsync, observer must increment once, caller result must remain timeout, and raw `events.log` must contain the unique message exactly once. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-02 | TEST-EVENT-DURABLE-BACKPRESSURE | Real TU durable queue discovered by unique capacity-4/pointer-size descriptor; one in-flight and four queued accepted requests time out while fsync is held, each caller must record `ESP_ERR_TIMEOUT` within five seconds before join, a sixth request returns `ESP_ERR_NO_MEM`, queue depth remains four, observer is unchanged until release, and one binary-log snapshot contains the timeout message and each accepted token exactly once with no overflow token. |
| REQ-TEST-TASK-LIFECYCLE / AC-TEST-TASK-01 | TEST-SHIM-TASK-COMPLETION-JOIN | Shim task callback calling `vTaskDelete(NULL)` returns naturally; bounded completion is observable, self-delete request is observable, repeated join is safe, and cleanup reclaims joined handles. |
| REQ-TEST-TASK-LIFECYCLE / AC-TEST-TASK-02 | TEST-SHIM-TASK-TIMEOUT | A gated live callback times out without reporting completion or joining; its handle remains usable, then completes and joins after deterministic release. |

## Harness limits

Current M2 evidence supersedes historical milestone notes above:
`worker-completion` holds one projection and queues four more, requires all five
to drain before success and checks exactly-once queued tokens/zero depth.
`init-text-failure` proves rollback/reinit before any worker; `partial-init`
proves quarantine/drain after the sole text worker starts. `quiesce-race` also
expires an empty text receive while binary fsync is blocked and requires late
projection persistence after release. Full 32-item projection saturation and
all individual pre-worker allocation faults are not claimed. These tests do
not prove physical poweroff; the controller/global contracts are separate.

The custom FreeRTOS shim copies queue items by configured item size, bounds queue
capacity, provides counting/binary semaphore behavior and starts joinable host
threads for tasks. Completion is observable; join is accepted only after completion,
and explicit cleanup reclaims joined handles. Self `vTaskDelete(NULL)` records a
request and allows the callback to return naturally; no host thread is forcibly
terminated. The runtime TU still uses `_exit` and its infinite workers are not
joined by this harness phase. It is intentionally isolated from `shim/full/freertos`.
The test links the production TU and `cyberdeck_clock.cpp`; BSP, ESP logging,
timer, sdkconfig and filesystem path are host adapters. `--wrap=fsync` observes
real host fsync calls. Write/fsync failures are injected and asserted below.
`event_log_shim/event_log_shim.cpp` owns the extracted FreeRTOS/BSP/time
adapters. Its unit target verifies copied FIFO bounded queue semantics,
semaphore empty/give/take and mutex availability, deterministic monotonic tick
advancement, deadline-pending behavior and wake-before-deadline. The runtime
target separately characterizes timeout/lifetime and backpressure behavior of
the real logger TU; neither target implements or proves quiesce/fence behavior.
Test-task, notification, timed-wait,
binary-fsync barrier and observer waits have finite five-second watchdogs; expiry fails
the host test rather than masquerading as an expected logger error. The queue and semaphore APIs still
use host condition-variable waits for nonzero FreeRTOS waits; the explicit
fake-clock wait primitive is deterministic and future tests can coordinate it
without sleeping. There is no production worker
shutdown API, so the process uses `_exit` after flushing coverage to avoid static
teardown races; run each scenario in its own process.
The runtime target supplies a unique `mktemp` root per run, while coverage
profiles remain beside the test binary for aggregate gcovr/scope-guard
collection. The parent removes the root only after the worker process exits.
Direct runs
without Make create their own `mkdtemp` root, intentionally left behind because
`_exit` does not join detached production workers.

## Commands

- `make -C tests/host/keymap test_event_log_runtime`
- `make -C tests/host/keymap test_event_log_shim`
- `make -C tests/host/keymap test_event_log_contract`
- `make -C tests/host/keymap test_event_projection_contract`
