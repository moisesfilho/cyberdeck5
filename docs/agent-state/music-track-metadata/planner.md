# planner - music-track-metadata

- Role: planner
- Updated: 2026-11-07 (reset de planejamento: blocker novo de gate após aceite físico)
- Code state: snapshot da revisão final informado em `reviewer.md`: `develop` HEAD `ff47b9a31c8d8a1fdb5746c60013619cf75c7481`, snapshot `b9c809234d7a00d117ba7782c089f5389a9958e6b1c9c658a3376b1f14a34cc2` (tracked patch `fc54a8ddd7a5c11c68383a72e8de800b2199ffca7e5bd83fc7ec63f732885a09`, 26 status entries com untracked). Não reconfirmado nesta sessão sem comandos.

## Dispatch objective

Replanejar apenas a falha do scope guard para `cyberdeck_usb_audio_host.cpp` e a contradição documental do aceite 1.15.2, sem reabrir o objetivo ID3 já aceito pelo usuário. Preservar REQ-MUSIC-META-01..06/AC-MUSIC-META-01..30 e a evidência física reportada. Gate local e CI devem ficar verdes antes de finalizar/commit-push; não converter aceite reportado em ensaio independente nem inferir custo de scan.

## Work done

Plano curto: corrigir a classificação do TU USB-A no gate fail-closed, preferencialmente com allowlist de **um único caminho** e justificativa explícita da dependência BSP/ESP-IDF/FreeRTOS/USB Host/UAC, mais ajuste do contrato `test_coverage_scope_guard.py` que exige conjunto exato; não excluir o TU da descoberta, não afrouxar thresholds nem ampliar allowlist por padrão. Atualizar `code-map.md` no trabalho de implementação. Separadamente corrigir somente o estado obsoleto de `tests/manual/music-player-validation.pt-BR.md:292-298` e espelhos de rastreabilidade se contraditórios: 1.15.2 compilado/flashado, aceite **reportado pelo usuário**, custo de scan sem medição independente. Alternativa mais forte: compilar o TU real com shims USB/FreeRTOS/BSP e exercitá-lo em host, se viabilidade demonstrada antes da alteração; custo e risco bem maiores. Se o TU já for gerado no host mas ausente no JSON por wiring/stale gcov, corrigir esse wiring e não allowlistar. Sem alteração no firmware ID3 ou USB, nem em regra CI de cobertura.

## Evidence

Inspeção estática somente: `tools/coverage_scope_guard.py:108-152` descobre todos os `.cpp` e exige presença no gcovr JSON ou allowlist específica; rejeita TU allowlisted que também esteja coberto. `tests/host/keymap/Makefile:1850-1857` compila `cyberdeck_usb_audio_json.cpp`, não o host TU; o último inclui BSP, esp_err/esp_timer, FreeRTOS, usb_host/uac_host (`cyberdeck_usb_audio_host.cpp:1-14`). `.github/workflows/quality-gate.yml:121-148` reproduz agregação+gcovr+scope guard. `test_coverage_scope_guard.py:24-57,163-176` fixa conjunto exato de allowlist, exigindo atualização coerente caso se escolha isenção. `reviewer.md:19-37` informa cobertura global 87.5/94.1/59.9 PASS, guard FAIL por esse TU, build PASS; CI remoto ainda não disparado no snapshot dirty. `developer.md:17-24` registra flash 1.15.2 e aceite reportado. `tests/manual/music-player-validation.pt-BR.md:292-298` afirma falsamente que ainda faltam build/flash/aceite. Não rodei comandos, testes, build ou dispositivo; observações não equivalem a prova de que a alternativa escolhida passará no CI.

## Decisions and assumptions

- Risco medium para correção do gate: allowlist restringe a exigência de cobertura para um TU de hardware; não altera execução/compatibilidade do firmware, mas muda risco aceito de validação. A abordagem ID3 anterior não falhou: o fechamento foi bloqueado por descompasso preexistente entre inventário de fontes e harness USB, revelado no gate final. Comparar custo: allowlist pequena e contrato/ensaio físico USB já existente (menor custo; permanece lacuna de comportamento host); shims com TU real (maior qualidade host, alto custo de simular callbacks/USB, potencial aumento de denominadores gcovr e fragilidade); ajuste de wiring (preferível somente se houver evidência de compilação real do TU). Não usar Jev: não há empate semântico; escolha de aceitação da exceção de cobertura pertence ao checkpoint humano se mudar o risco aprovado.
- Contingência: se inspeção do snapshot pós-correção mostrar TU no gcovr JSON, remover/evitar allowlist e corrigir cobertura host; se o usuário não aceitar exceção para driver USB, apresentar plano separado de harness host com shims e esforço de validação, sem mascarar a falha nem mover fonte para fora do source-root.

## Pending and risks

Checkpoint humano: developer deve confirmar se exceção estreita do TU USB preexistente é aceitável como dependência mínima de fechamento do gate; se exigir cobertura do driver real, reaprovar custo/escopo de harness adicional. Nenhum novo aceite físico de ID3 é necessário para ajustes apenas de gate/docs; mudança funcional no USB exigiria novo ciclo e device gate. Risco residual: USB só é testado por contrato estrutural e serializador host, com comportamento UAC validado fisicamente em entrega anterior, não por esse gate; performance da validação de padding ID3 até 1 MiB continua sem medição (`TEST-MUSIC-META-PHYS-SCAN-COST`). Após correção, tester revisa asserções, reviewer roda `make clean test`, teste de allowlist, execução CI-style com gcovr limites 80/90/50 + scope guard, verify/build conforme aplicáveis, e remote CI **no commit autorizado**. Documentar o snapshot novo; gate anterior fica stale quando arquivos mudarem. Commit/push somente pelo developer após revisão e confirmação humana aplicável.

## References

`docs/agent-state/music-track-metadata/{developer,reviewer,tester}.md`; `tests/host/keymap/music_traceability.md` (REQ/AC-MUSIC-META); `tools/coverage_scope_guard.py`; `tests/host/keymap/{Makefile,test_coverage_scope_guard.py,test_usb_audio_host_contract.py,test_usb_audio_host.cpp}`; `.github/workflows/quality-gate.yml`; `components/cyberdeck/src/platform/usb/cyberdeck_usb_audio_host.cpp`; `tests/manual/music-player-validation.pt-BR.md`; `code-map.md`.
