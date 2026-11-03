# Fase 10 — rastreabilidade host

| Requisito/AC | Cenário | Teste |
|---|---|---|
| REQ-10-01/AC schema, checksum, limites, corrupção, NVS fail-closed | namespace `cyberdeck_rec` válido (<=15), persistência por `nvs_commit`, blob versionado/tamanho inválido e ausência de erase; estado NVS indisponível seleciona recovery fail-closed | `test_phase10_contract.py::test_recovery_blob_schema_checksum_limits_and_fail_closed`; `test_recovery_runtime.cpp`; `test_boot_sequence.py` |
| REQ-10-02/AC boot attempt/checkpoint/commit/recovery | boot pendente, checkpoint, três interrupções e latch | `test_recovery_policy.cpp::interrupted_boots_latch_after_three_attempts`; `test_phase10_contract.py::test_boot_attempt_checkpoint_and_three_interruptions` |
| REQ-10-03/AC último erro por app/app info | sobrescrita por app, limite de 16, diagnóstico | `test_recovery_policy.cpp::errors_are_per_app_and_bounded`; `test_phase10_contract.py::test_sys_info_and_app_info_expose_recovery_diagnostics` |
| REQ-10-04/AC safe mode/latch/serviços/reset explícito | somente event log/shell/serial e `sys.safe_mode.clear`; clear preserva checkpoint em andamento e transições NVS são serializadas | `test_phase10_contract.py::test_safe_mode_latch_and_explicit_reset_services`; `test_phase10_contract.py::test_clear_safe_mode_preserves_inflight_boot_checkpoint`; `test_recovery_runtime.cpp` |
| REQ-10-05/AC lifecycle logs bounded | logger injetado, orçamento e fila bounded | `test_phase10_contract.py::test_lifecycle_logs_are_bounded_and_emitted` |
| REQ-10-06/AC sys.info/app info | campos de recovery, último erro e diagnóstico persistido | `test_phase10_contract.py::test_sys_info_and_app_info_expose_recovery_diagnostics` |
| REQ-10-07/AC commit após superfície essencial pronta | falha essencial (shell/Serial-JTAG) preserva boot pendente; falhas opcionais são diagnosticadas e não bloqueiam checkpoint quando ambos estão ativos | `test_phase10_contract.py::test_boot_ready_is_the_only_path_to_recovery_commit`; `test_phase10_contract.py::test_optional_service_failures_do_not_block_essential_checkpoint`; validação Serial-JTAG pendente |

Os testes são host-only e não substituem a validação NVS/Serial-JTAG no alvo.
