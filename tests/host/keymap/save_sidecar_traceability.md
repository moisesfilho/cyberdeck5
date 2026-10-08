# Save sidecar recovery traceability

| Requirement / acceptance criterion | Test |
| --- | --- |
| REQ-SIDECAR-01 / AC-SIDECAR-01 (B01): absent destination restores an actual `<root>/data` rollback; repeat call is idempotent | `TEST-SIDECAR-API::test_rollback_and_idempotency` |
| REQ-SIDECAR-02 / AC-SIDECAR-02 (B02): existing destination is ambiguous and preserves both versions | `TEST-SIDECAR-API::test_ambiguous_and_tmp_preserved` |
| REQ-SIDECAR-03 / AC-SIDECAR-03 (B03): `.tmp` is never published or modified | `TEST-SIDECAR-API::test_ambiguous_and_tmp_preserved` |
| REQ-SIDECAR-04 / AC-SIDECAR-04 (B04): invalid target and symlink sidecars are preserved; root-level sidecars outside `/data` are untouched; inaccessible roots fail closed | `TEST-SIDECAR-API::test_rejections_and_failures`, `test_rollback_and_idempotency` |
| REQ-SIDECAR-05 / AC-SIDECAR-05: every failure reports action, stage, and errno | `TEST-SIDECAR-API` |
| REQ-SIDECAR-06 / AC-SIDECAR-06: recovery is invoked only after mount and handle validation | `TEST-BOOT-SEQUENCE` |

Host API coverage uses temporary directories and the actual local-shell translation unit. Recovery fixtures mirror the mounted SD layout: sidecars and destinations are under `<root>/data`; a rollback fixture at `<root>` proves the recovery is confined to `/data`. This is the regression for a recovery scan that inspects only the root. B05 covers failure diagnostics for invalid API/root input and enumeration. Boot order is checked structurally against `app_main.cpp`.
