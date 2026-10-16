# App grant traceability

| Requirement / acceptance | Scenario | Evidence |
| --- | --- | --- |
| REQ-GRANT-01 / AC-GRANT-01 | TEST-GRANT-01: `main()` checks `grant app stops`, `grant revocation linearizes before teardown and invalidates saved copies`; retained runtime and app snapshots both become invalid. | `test_app_runtime.cpp`, marked TEST-GRANT-01; executable assertions named above |
| REQ-GRANT-02 / AC-GRANT-02 | TEST-GRANT-02: `main()` checks `teardown barrier completes within bound`, `grant app stops`, and the invalid grant observed by teardown. | `test_app_runtime.cpp`, marked TEST-GRANT-02; condition-variable predicate is bounded by `wait_for` |
| REQ-GRANT-03 / AC-GRANT-03 | TEST-GRANT-03: `main()` checks `init failure invalidates grant published during init` and `start failure invalidates grant published during start`. | `test_app_runtime.cpp`, marked TEST-GRANT-03; both captured hook grants and app grants are asserted invalid |
| REQ-GRANT-04 / AC-GRANT-04 | TEST-GRANT-04: `main()` checks `restart publishes a fresh grant without reviving stale generation` and `second generation is invalid after stop`. | `test_app_runtime.cpp`, marked TEST-GRANT-04 |
| REQ-GRANT-05 / AC-GRANT-05 | TEST-GRANT-05: `main()` checks `application returns a usable by-value grant snapshot`. TEST-GRANT-06: `concurrent grant reads remain safe and stale copies deny after stop` checks 150 start/stop generations while a reader calls `valid`/`allows` through app and runtime APIs. TEST-GRANT-07: reader-start and teardown condition-variable barriers use bounded `wait_for`. TEST-GRANT-08: `default grant is invalid and denies resources` and `running grant preserves exactly the declared resource mask`. | `test_app_runtime.cpp`, each case tagged with TEST-GRANT marker and exact assertion label; `make test_app_runtime_tsan` runs the same behavioral test under ThreadSanitizer |

The host test links the production runtime translation unit. The focused normal
and ThreadSanitizer targets are required checks; synchronization waits are
bounded, and sanitizer execution complements rather than replaces explicit
post-revocation assertions.
