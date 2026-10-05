# Backlog restante — rastreabilidade host

Contrato host-only; deve ser atualizado com evidência real após a
implementação e não substitui validação ESP-IDF ou no hardware.
Marcador CASE_ID aprovado: somente o escopo de recursos/SD/quotas.

| Requisito/AC | Cenários determinísticos | Teste |
|---|---|---|
| REQ-SDK-01 / AC-SDK-01 | EOF, erro, capacidade zero/exata, ownership e ausência de handles | `test_os_transformation_backlog_contract.py::storage_contract` |
| REQ-LOG-01 / AC-LOG-01 | níveis/eventos, payload bounded, limite e segredo redigido | `test_os_transformation_backlog_contract.py::logger_contract` |
| REQ-CMD-01 / AC-CMD-01 | manifesto bounded, dispatch e legados sem interceptação | `test_os_transformation_backlog_contract.py::command_catalog_contract` |
| REQ-REPLY-01 / AC-REPLY-01 | output existente e SSH byte-a-byte, sem framing universal | `test_os_transformation_backlog_contract.py::reply_contract` |
| REQ-BOOT-01 / AC-BOOT-01 | ordem, supervisor, checkpoint, recovery e safe mode | `test_os_transformation_backlog_contract.py::boot_contract` |
| REQ-DOC-01 / AC-DOC-01 | IDs e evidências navegáveis no plano e no mapa | `test_os_transformation_backlog_contract.py::documentation_contract` |
| REQ-RES-01 / AC-RES-01 | catálogo de assets compilados bounded/readonly, ausência sem falha de boot | `test_resource_sd_quota_contract.py::resource_catalog_contract` |
| REQ-SD-01 / AC-SD-01 | metadata/version/checksum para dados/assets, ausente/corrompido fail-closed, sem instalação/execução | `test_resource_sd_quota_contract.py::sd_package_contract` |
| REQ-QUOTA-01 / AC-QUOTA-01 | limites explícitos de recursos/grants, stack, fila, bounded_read, logger/output e lifecycle; revogação | `test_resource_sd_quota_contract.py::quota_contract` |
| REQ-GUARD-01 / AC-GUARD-01 | event bus global, reply universal, loader/ELF/dinâmico, multi-device e Tactility documentados como guardrails | `test_resource_sd_quota_contract.py::guardrail_contract` |
| REQ-TRACE-01 / AC-TRACE-01 | matriz/plano com os casos aprovados `case identifier` e `refletem`, com multi-device fora de escopo e sem ampliar escopo | `test_resource_sd_quota_contract.py::traceability_contract` |
