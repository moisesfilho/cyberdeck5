# App grant requirements and traceability

## Normative requirements and acceptance criteria

| ID | Requirement / acceptance criterion |
| --- | --- |
| REQ-GRANT-01 / AC-GRANT-01 | Revoking a running app's grant invalidates every retained by-value snapshot; snapshots cannot authorize resources after stop. |
| REQ-GRANT-02 / AC-GRANT-02 | Revocation linearizes before teardown begins, so the teardown hook observes an invalid grant; teardown completes within the test's bounded watchdog. |
| REQ-GRANT-03 / AC-GRANT-03 | A grant captured during init or start is invalid if that lifecycle operation fails. |
| REQ-GRANT-04 / AC-GRANT-04 | Restart issues a valid fresh generation without reviving prior snapshots; stopping that generation invalidates it. |
| REQ-GRANT-05 / AC-GRANT-05 | Grant reads return usable by-value snapshots, remain safe during concurrent lifecycle changes, and authorize only the declared resource mask; default/unknown grants deny access. |

## Test evidence

| TEST ID | Exact assertions / evidence |
| --- | --- |
| TEST-GRANT-01 | In `main()`, `grant revocation linearizes before teardown and invalidates saved copies`: teardown's captured grant, runtime snapshot, and app snapshot are invalid after stop; saved snapshot denies display. |
| TEST-GRANT-02 | In `main()`, `teardown barrier completes within bound` asserts the teardown callback arrives before the watchdog; `grant app stops` asserts stop succeeds. Watchdog expiry exits the test process immediately rather than joining a potentially blocked worker. The callback reads the grant before signalling the barrier; TEST-GRANT-01 then asserts it was already invalid. |
| TEST-GRANT-03 | In `main()`, `init failure invalidates grant published during init` and `start failure invalidates grant published during start` assert both captured and app-held grants are invalid. |
| TEST-GRANT-04 | In `main()`, `restart publishes a fresh grant without reviving stale generation` and `second generation is invalid after stop` assert fresh-generation behavior. |
| TEST-GRANT-05 | In `main()`, `application returns a usable by-value grant snapshot` verifies app access to a valid grant snapshot. |
| TEST-GRANT-06 | In `main()`, `concurrent grant reads remain safe and stale copies deny after stop` exercises 150 start/stop generations while runtime/app reads run concurrently. |
| TEST-GRANT-07 | In `main()`, `concurrent reader reaches start barrier` asserts the reader signals startup within the bounded `wait_for`. The teardown watchdog is separately TEST-GRANT-02. |
| TEST-GRANT-08 | In `main()`, `default grant is invalid and denies resources` and `running grant preserves exactly the declared resource mask` verify default-deny, unknown-app denial, and exact authorization. |

`test_app_runtime.cpp` links the production runtime translation unit. The normal
and ThreadSanitizer targets execute this behavioral test; TSAN supplements, but
does not replace, its explicit revocation and bounded-teardown assertions.
