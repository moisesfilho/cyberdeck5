# Event log runtime harness — baseline characterization

Phase 1 only: the host executable links the real `event_log.cpp` TU. It characterizes
existing behavior and does not test or imply a quiesce/fence API.

| Requirement / AC | TEST | Baseline evidence |
|---|---|---|
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-01 | TEST-HARNESS-INIT-01 | Real init creates both workers against a host SD directory; normal append reaches the binary log. Admission/fence is not implemented in this phase. |
| AC-GLOBAL-LOG-02 | TEST-HARNESS-DURABLE-ACK-01 | Real durable API returns success only after worker completion; wrapped fwrite/fsync audit asserts write → fsync → caller ACK, and a test-only observer confirms accepted-request release after ACK. Binary log contains normal and durable records. |
| AC-GLOBAL-LOG-03 | TEST-HARNESS-FAILURE-01 | Reserved for later quiesce lifecycle; this phase does not claim drain/timeout behavior. |
| AC-GLOBAL-LOG-04 | TEST-HARNESS-PROJECTION-01 | Existing structural contracts retain projection best-effort, rotation/read, durable timeout and queue ordering; runtime target starts projection worker. |
| TEST-HARNESS-FSYNC-ISOLATION-01 | TEST-HARNESS-FSYNC-ISOLATION-01 | Runtime wrapper canonicalizes `/proc/self/fd/<n>` and compares only the unique root's `logs/events.log`; the binary fsync blocks until release while projection fsync is observed to complete without consuming the barrier. Failure injection remains binary-only. |
| REQ-GLOBAL-QUIESCE / AC-GLOBAL-LOG-02 | TEST-EVENT-DURABLE-TIMEOUT-LIFETIME | Real TU request blocks in binary fsync; advancing fake ticks by 250 ms must return `ESP_ERR_TIMEOUT` while release observer remains unchanged. After releasing fsync, observer must increment once, caller result must remain timeout, and raw `events.log` must contain the unique message exactly once. |

## Harness limits

The custom FreeRTOS shim copies queue items by configured item size, bounds queue
capacity, provides counting/binary semaphore behavior and starts real detached
host threads for tasks. It is intentionally isolated from `shim/full/freertos`.
The test links the production TU and `cyberdeck_clock.cpp`; BSP, ESP logging,
timer, sdkconfig and filesystem path are host adapters. `--wrap=fsync` observes
real host fsync calls. Write/fsync failures are injected and asserted below.
`event_log_shim/event_log_shim.cpp` owns the extracted FreeRTOS/BSP/time
adapters. Its unit target verifies copied FIFO bounded queue semantics,
semaphore empty/give/take and mutex availability, deterministic monotonic tick
advancement, deadline-pending behavior and wake-before-deadline. It does not
yet test TU timeout/lifetime/backpressure. Test-task, notification, timed-wait,
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
