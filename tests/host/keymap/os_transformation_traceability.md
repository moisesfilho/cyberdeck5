# Backlog restante — rastreabilidade host

Contrato host-only; deve ser atualizado com evidência real após a
implementação e não substitui validação ESP-IDF ou no hardware.

| Requisito/AC | Cenários determinísticos | Teste |
|---|---|---|
| REQ-SDK-01 / AC-SDK-01 | EOF, erro, capacidade zero/exata, ownership e ausência de handles | `test_os_transformation_backlog_contract.py::storage_contract` |
| REQ-LOG-01 / AC-LOG-01 | níveis/eventos, payload bounded, limite e segredo redigido | `test_os_transformation_backlog_contract.py::logger_contract` |
| REQ-CMD-01 / AC-CMD-01 | manifesto bounded, dispatch e legados sem interceptação | `test_os_transformation_backlog_contract.py::command_catalog_contract` |
| REQ-REPLY-01 / AC-REPLY-01 | output existente e SSH byte-a-byte, sem framing universal | `test_os_transformation_backlog_contract.py::reply_contract` |
| REQ-BOOT-01 / AC-BOOT-01 | ordem, supervisor, checkpoint, recovery e safe mode | `test_os_transformation_backlog_contract.py::boot_contract` |
| REQ-DOC-01 / AC-DOC-01 | IDs e evidências navegáveis no plano e no mapa | `test_os_transformation_backlog_contract.py::documentation_contract` |
