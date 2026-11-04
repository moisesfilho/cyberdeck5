# Poweroff test traceability

| Test ID | Requirement / acceptance | Evidence |
| --- | --- | --- |
| TEST-POWER-ADMISSION | REQ-POWER / AC-POWER-ADMISSION | `test_shell_session.cpp`: only exact `poweroff` followed by `y` invokes typed `admit_poweroff`; admitted result reports asynchronous quiescence. `test_poweroff_contract.py`: UI host delegates to `cyberdeck_poweroff_request()`. |
| TEST-POWER-FAILURE | REQ-POWER / FAIL-CLOSED | `test_shell_session.cpp`: unavailable coordinator, consumed one-shot, fence, watchdog, and task failures each produce a cause-specific reboot-required diagnostic. |
| TEST-POWER-ONESHOT | REQ-POWER | `test_shell_session.cpp`: success and each typed admission failure cannot be retried; cancellation/help/non-exact input do not admit. |
| TEST-POWER-TASK-CREATE-DIAG | REQ-POWER / AC-POWER-ADMISSION / FAIL-CLOSED | `test_poweroff.cpp` executes the production observer seam and verifies one probe/record preserving all four global/internal free/largest metrics; `test_poweroff_contract.py` checks PSRAM task-stack allocation, 8 KiB stack size, external-stack sdkconfig, observer placement and fail-closed admission ordering. |
| TEST-POWER-NO-CUT | REQ-POWER / REQ-GLOBAL-QUIESCE / FAIL-CLOSED | `test_poweroff.cpp` verifies an expired controller remains failed, rejects second admission, performs no stages, and cannot execute the signal stage. `test_poweroff_contract.py` covers ESP-only admission failure handling. Host tests do not prove a physical rail transition. |
| TEST-I2C-BROKER-01..03 | REQ-I2C-BROKER | `test_expander_broker.cpp` proves exclusive cross-thread leases, bounded timeout, and release/reacquisition. ESP contract/build confirm battery and poweroff use the shared broker. |
| TEST-I2C-DIAG-01..07 | REQ-I2C-DIAG-RO | `test_battery_protection_contract.py` and `test_shell_session.cpp` cover exact register set/order, origin, partial failure invalidation, latch/input semantics, sequential sample label, and prohibition of 0x13/writes. Latch labels are pinned by a table derived from register 0x05 (0x80/0x20/0x00/0xA0/0xFF) that also checks the `expander_raw: 05=..` byte, so `CHG_QC_EN_latch` cannot invert again. No device poweroff is invoked. |

Mapping note: the spec IDs handled here are TEST-I2C-BROKER-01..03 (broker
exclusion/ownership) and TEST-I2C-DIAG-01..07 (read-only diagnostics). The
`TEST-EXP-08..14` labels used in the delivery handoff have no counterpart in this
repository, so they are mapped to those two groups: broker singleton/lease
coverage -> TEST-I2C-BROKER-01..03, diagnosis registers/labels -> TEST-I2C-DIAG-01..07.

The host seam validates shell responses and UI wiring, not execution of the ESP
hardware shutdown sequence or the physical rail transition.
