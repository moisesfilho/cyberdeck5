# Especificação de testes — sincronização UTC

Escopo: `REQ-TIME-01..05` / `AC-TIME-01..05`. O contrato de produção tenta
primeiro `GET` HTTPS para `https://timeapi.io/api/time/current/zone?timeZone=UTC`
e usa `https://worldtimeapi.org/api/timezone/Etc/UTC` somente no fallback. Ambos
usam CA bundle oficial do ESP-IDF, timeout HTTP de 4000 ms e corpo limitado a
2048 bytes. Os testes host usam seams fake de HTTP/clock/logger/Wi-Fi; nenhum
caso acessa a rede pública. A validação manual confirma a costura no firmware real.

| ID | Tipo | Cenário e asserções mínimas | Rastreabilidade |
| --- | --- | --- | --- |
| TEST-TIME-01 | positivo/aceitação/regressão | Com boot concluído e Wi‑Fi conectado, uma resposta HTTPS 200 de `worldtimeapi` com `utc_datetime` real contendo frações (`.123456+00:00`) atualiza o clock uma vez; método GET, URL HTTPS/UTC, payload aceito e epoch de segundos inteiros são observados. Se a implementação usar `unixtime` em vez de `utc_datetime`, este cenário deve usar fixture equivalente desse campo. | REQ-TIME-01,02; AC-TIME-01,02 |
| TEST-TIME-02 | negativo/regressão | Boot sem Wi‑Fi, Wi‑Fi desconectado e conexão que ainda não recebeu IP não fazem GET nem alteram o clock; conectar depois permite exatamente uma sincronização. | REQ-TIME-02; AC-TIME-02 |
| TEST-TIME-03 | negativo/aceitação | Timeout de 4000 ms, DNS, TLS/certificado/CA, HTTP 4xx/5xx, corpo vazio, JSON ausente/malformado e UTC inválida falham silenciosamente: preservam o último valor, não propagam erro fatal e deixam o runtime/UI continuar. | REQ-TIME-03; AC-TIME-03 |
| TEST-TIME-04 | limite/regressão | Exercita `utc_datetime` com frações no limite aceito, virada de dia/ano bissexto, campos ausentes, tipos errados, timezone divergente e payload JSON acima do limite de 2048 bytes; cada rejeição preserva o último clock. | REQ-TIME-01,03; AC-TIME-01,03 |
| TEST-TIME-05 | não bloqueante | Enquanto a requisição está pendente, eventos do runtime/UI continuam sendo processados; nenhuma espera síncrona ocorre no boot, task LVGL ou callback de Wi‑Fi, e o timeout de 4000 ms encerra a operação dentro do orçamento. | REQ-TIME-03,04; AC-TIME-03,04 |
| TEST-TIME-06 | concorrência | Oito eventos `got IP` do mesmo ciclo são entregues em threads reais; há no máximo uma requisição ativa e uma atualização, sem double completion. | REQ-TIME-02,04; AC-TIME-02,04 |
| TEST-TIME-07 | idempotência/regressão | `start`/`stop` repetidos e um segundo ciclo após restart criam exatamente um worker/callback por ciclo e uma requisição por token; o shim observa exatamente um `vTaskDeleteWithCaps(NULL)` por worker e nenhum segundo teardown; após restart, o estado boot-ready e o status Wi‑Fi/IP atual são reavaliados. | REQ-TIME-04; AC-TIME-04 |
| TEST-TIME-08 | teardown/regressão | `stop(9000)` entre a falha da tentativa primária e o início do fallback libera a barreira, quiesce o worker sem iniciar o fallback, observa um único `vTaskDeleteWithCaps(NULL)`, e permite um novo `start` com sincronização válida; nenhuma escrita ocorre após o stop nem há delete duplicado em conclusão tardia. | REQ-TIME-04; AC-TIME-04 |
| TEST-TIME-09 | runtime verificável | O contrato estrutural confere manifesto, dependências, tipo, recursos, stack/fila, registro, `start_all`, checkpoint boot-ready e callback lifecycle do supervisor. | REQ-TIME-04; AC-TIME-04 |
| TEST-TIME-10 | manual/aceitação | No dispositivo, após boot+Wi‑Fi, registrar `sys.info`, `wifi.status`, hora antes/depois e logs; confirmar hora UTC convertida/renderizada, ausência de reboot/panic e continuidade após desconectar/reconectar Wi‑Fi. Repetir com endpoint indisponível/sem IP. | REQ-TIME-01..05; AC-TIME-01..05 |
| TEST-TIME-11 | positivo/aceitação | No harness host, com logger fake instalado, uma resposta válida que realmente conclui `settimeofday` gera exatamente um evento bounded fixo: severidade `info`, tag `time_sync` e payload `success`, sem segundo evento. | REQ-TIME-05; AC-TIME-05 |
| TEST-TIME-12 | negativo/aceitação/regressão | HTTP, TLS/DNS, JSON inválido, `settimeofday` com erro e logger/runtime sem logger não geram evento; o worker permanece vivo, não aborta e o clock só muda quando `settimeofday` conclui com sucesso. | REQ-TIME-03,05; AC-TIME-03,05 |
| TEST-TIME-13 | positivo/aceitação | TimeAPI válida (`timeZone == UTC` e campos numéricos) atualiza/loga uma vez e não consulta WorldTimeAPI. | REQ-TIME-01,05; AC-TIME-01,05 |
| TEST-TIME-14 | fallback/aceitação | TimeAPI falha e WorldTimeAPI válida atualiza/loga uma vez; fallback consultada uma vez. | REQ-TIME-03,05; AC-TIME-03,05 |
| TEST-TIME-15 | falha/limite | Ambos endpoints falham: clock preservado, sem `settimeofday` e sem log de sucesso. | REQ-TIME-03,05; AC-TIME-03,05 |
| TEST-TIME-16 | rejeição/regressão | Schemas/tipos inválidos, timezone/data inválidos e corpos oversized de ambos endpoints são rejeitados sem mutação parcial. | REQ-TIME-01,03,05; AC-TIME-01,03,05 |
| TEST-TIME-17 | teardown/concorrência/regressão | Stop entre tentativa primária e fallback quiesce sem fallback nem ciclo ativo observável por um novo start, com um único `vTaskDeleteWithCaps(NULL)`; depois, oito notificações concorrentes do mesmo token produzem uma requisição, atualização e evento sem double teardown. | REQ-TIME-02,04,05; AC-TIME-02,04,05 |

## Gate e evidência

- Host: `test_time_sync` (`test_time_sync.cpp`) é compilado com `-O0 --coverage` (e sanitizers) e
  executa os cenários comportamentais; `test_time_sync_contract` valida o
  wiring de runtime por inspeção de fonte. Esses alvos não alegam cobertura do
  TEST-TIME-10 manual nem da seção ESP_PLATFORM.
- Manual: usar o roteiro Serial-JTAG; guardar comandos, respostas, logs,
  `sys.info` antes/depois e confirmar ausência de bloqueio/reboot.
- O gate falha se qualquer cenário não for implementado, se houver acesso de
  rede no host ou se erro de sincronização escapar como falha fatal.

## Matriz REQ/AC → TEST

| Requisito/aceitação | Testes host | Evidência manual |
| --- | --- | --- |
| `REQ-TIME-01` / `AC-TIME-01` | TEST-TIME-01, TEST-TIME-04, TEST-TIME-13, TEST-TIME-16 | TEST-TIME-10 |
| `REQ-TIME-02` / `AC-TIME-02` | TEST-TIME-01, TEST-TIME-02, TEST-TIME-06 | TEST-TIME-10 |
| `REQ-TIME-03` / `AC-TIME-03` | TEST-TIME-03, TEST-TIME-04, TEST-TIME-05, TEST-TIME-14, TEST-TIME-15, TEST-TIME-16 | TEST-TIME-10 |
| `REQ-TIME-04` / `AC-TIME-04` | TEST-TIME-05, TEST-TIME-06, TEST-TIME-07, TEST-TIME-08, TEST-TIME-09, TEST-TIME-17 | TEST-TIME-10 |
| `REQ-TIME-05` / `AC-TIME-05` | TEST-TIME-03, TEST-TIME-04, TEST-TIME-11, TEST-TIME-12, TEST-TIME-13, TEST-TIME-14, TEST-TIME-15, TEST-TIME-16, TEST-TIME-17 | TEST-TIME-10 |

O gating é cumulativo: `cyberdeck_time_sync_boot_ready()` precisa ter sido
observado e o estado Wi‑Fi precisa estar conectado **e com IP** antes de
enfileirar uma requisição. O worker é assíncrono, usa stack de 6144 bytes e
processa no máximo uma requisição por token de conexão; `start`, `stop` e
teardown são idempotentes/quiescentes.
