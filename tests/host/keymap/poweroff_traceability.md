# Poweroff test traceability

| Test ID | Requirement / acceptance | Evidence |
| --- | --- | --- |
| TEST-POWER-ADMISSION | REQ-POWER / AC-POWER-ADMISSION | `test_shell_session.cpp`: only exact `poweroff` followed by `y` invokes typed `admit_poweroff`; admitted result reports asynchronous quiescence. `test_poweroff_contract.py`: UI host delegates to `cyberdeck_poweroff_request()`. |
| TEST-POWER-FAILURE | REQ-POWER / FAIL-CLOSED | `test_shell_session.cpp`: unavailable coordinator, consumed one-shot, fence, watchdog, and task failures each produce a cause-specific reboot-required diagnostic. |
| TEST-POWER-ONESHOT | REQ-POWER | `test_shell_session.cpp`: success and each typed admission failure cannot be retried; cancellation/help/non-exact input do not admit. |
| TEST-POWER-TASK-CREATE-DIAG | REQ-POWER / AC-POWER-ADMISSION / FAIL-CLOSED | `test_poweroff.cpp` executes the production observer seam and verifies one probe/record preserving all four global/internal free/largest metrics; `test_poweroff_contract.py` checks both IDF capability masks, task stack, observer placement and failure ordering. Instantaneous heap metrics do not infer root cause. |
| TEST-POWER-NO-CUT | REQ-POWER / REQ-GLOBAL-QUIESCE / FAIL-CLOSED | `test_poweroff.cpp` verifies an expired controller remains failed, rejects second admission, performs no stages, and cannot execute the signal stage. `test_poweroff_contract.py` covers ESP-only admission failure handling. Host tests do not prove a physical rail transition. |

The host seam validates shell responses and UI wiring, not execution of the ESP
hardware shutdown sequence or the physical rail transition.
