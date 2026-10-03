# Fase 10 — rastreabilidade host

| Requisito/AC | Cenário | Teste |
|---|---|---|
| REQ-10-01/AC schema, checksum, limites, corrupção, NVS fail-closed | blob versionado/tamanho inválido, campos bounded, ausência de erase | `test_phase10_contract.py::test_recovery_blob_schema_checksum_limits_and_fail_closed` |
| REQ-10-02/AC boot attempt/checkpoint/commit/recovery | boot pendente, checkpoint, três interrupções e latch | `test_recovery_policy.cpp::interrupted_boots_latch_after_three_attempts`; `test_phase10_contract.py::test_boot_attempt_checkpoint_and_three_interruptions` |
| REQ-10-03/AC último erro por app/app info | sobrescrita por app, limite de 16, diagnóstico | `test_recovery_policy.cpp::errors_are_per_app_and_bounded`; `test_phase10_contract.py::test_sys_info_and_app_info_expose_recovery_diagnostics` |
| REQ-10-04/AC safe mode/latch/serviços/reset explícito | somente event log/shell/serial e `sys.safe_mode.clear` | `test_phase10_contract.py::test_safe_mode_latch_and_explicit_reset_services` |
| REQ-10-05/AC lifecycle logs bounded | logger injetado, orçamento e fila bounded | `test_phase10_contract.py::test_lifecycle_logs_are_bounded_and_emitted` |
| REQ-10-06/AC sys.info/app info | campos de recovery e último erro | `test_phase10_contract.py::test_sys_info_and_app_info_expose_recovery_diagnostics` |

Os testes são host-only e não substituem a validação NVS/Serial-JTAG no alvo.
