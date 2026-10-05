# Especificação de testes — sincronização UTC

Escopo: `REQ-TIME-01..05` / `AC-TIME-01..05`. O contrato de produção tenta
primeiro `GET` HTTPS para `https://timeapi.io/api/time/current/zone?timeZone=UTC`
e usa `https://worldtimeapi.org/api/timezone/Etc/UTC` somente no fallback. Ambos
usam CA bundle oficial do ESP-IDF, timeout HTTP de 4000 ms e corpo limitado a
2048 bytes. Os testes host usam seams fake de HTTP/clock/logger/Wi-Fi; nenhum
caso acessa a rede pública. A validação manual confirma a costura no firmware real.

## Logging durável do sucesso (`REQ-TIMELOG-01..04`)

Os testes abaixo complementam TEST-TIME-11/12/17. O fake host representa a
fronteira do logger real: `event_log_write_durable` só reconhece o evento depois
de append+fsync; não há acesso a SD, rede ou Serial-JTAG no host.

| ID | Tipo | Cenário e asserções mínimas | Rastreabilidade |
| --- | --- | --- | --- |
| TEST-TIMELOG-01 | aceitação | Adaptador encaminha para `event_log_write`; sucesso usa `time_sync/success`, bounded e uma ocorrência. | REQ-TIMELOG-01; AC-TIMELOG-01 |
| TEST-TIMELOG-02 | aceitação/regressão | Sucesso só é reconhecido após append e `fsync` em `events.log`; sem confirmação durável não há sucesso observável. | REQ-TIMELOG-02; AC-TIMELOG-02 |
| TEST-TIMELOG-03 | falha/regressão | Falha HTTP/TLS/parser, `settimeofday` ou fila de projeção cheia não bloqueia o produtor; o evento crítico permanece em `events.log` e não há falso sucesso. | REQ-TIMELOG-03; AC-TIMELOG-03 |
| TEST-TIMELOG-04 | limite/concorrência/restart | Backpressure bounded é retentado pelo worker, sem bloquear callback Wi‑Fi; o evento crítico é persistido uma vez. Gating/restart/teardown ficam também em TEST-TIME-17. | REQ-TIMELOG-04; AC-TIMELOG-04 |
| TEST-TIMELOG-06 | limite/concorrência/restart | Escrita/fsync falha sem sucesso observável; cada token tem no máximo 3 tentativas, notificações concorrentes não duplicam a tentativa e restart inicia orçamento novo. | REQ-TIMELOG-02..04; AC-TIMELOG-02..04 |
| TEST-TIMELOG-07 | limite/regressão | Após ACK durável de um token, uma notificação repetida do mesmo token não gera novo evento; o contrato estrutural confirma wake explícito para filas normal/durável, ACK bounded em 250 ms e fsync antes da conclusão. | REQ-TIMELOG-02..04; AC-TIMELOG-02..04 |
| TEST-TIMELOG-08 | falha/regressão | Timeout do ACK de uma requisição durável aceita não é retentado, pois o evento pode já estar enfileirado; o token não duplica chamadas nem evento. | REQ-TIMELOG-02..04; AC-TIMELOG-02..04 |
| TEST-TIME-01 | positivo/aceitação/regressão | Com boot concluído e Wi‑Fi conectado, uma resposta HTTPS 200 de `worldtimeapi` com `utc_datetime` real contendo frações (`.123456+00:00`) atualiza o clock uma vez; método GET, URL HTTPS/UTC, payload aceito e epoch de segundos inteiros são observados. Se a implementação usar `unixtime` em vez de `utc_datetime`, este cenário deve usar fixture equivalente desse campo. | REQ-TIME-01,02; AC-TIME-01,02 |
| TEST-TIME-02 | negativo/regressão | Boot sem Wi‑Fi, Wi‑Fi desconectado e conexão que ainda não recebeu IP não fazem GET nem alteram o clock; conectar depois permite exatamente uma sincronização. | REQ-TIME-02; AC-TIME-02 |
| TEST-TIME-03 | negativo/aceitação | Timeout de 4000 ms, DNS, TLS/certificado/CA, HTTP 4xx/5xx, corpo vazio, JSON ausente/malformado e UTC inválida falham silenciosamente: preservam o último valor, não propagam erro fatal e deixam o runtime/UI continuar. | REQ-TIME-03; AC-TIME-03 |
| TEST-TIME-04 | limite/regressão | Exercita `utc_datetime` com frações no limite aceito, virada de dia/ano bissexto, campos ausentes, tipos errados, timezone divergente e payload JSON acima do limite de 2048 bytes; cada rejeição preserva o último clock. | REQ-TIME-01,03; AC-TIME-01,03 |
| TEST-TIME-05 | não bloqueante | Enquanto a requisição está pendente, eventos do runtime/UI continuam sendo processados; nenhuma espera síncrona ocorre no boot, task LVGL ou callback de Wi‑Fi, e o timeout de 4000 ms encerra a operação dentro do orçamento. | REQ-TIME-03,04; AC-TIME-03,04 |
| TEST-TIME-06 | concorrência | Oito eventos `got IP` do mesmo ciclo são entregues em threads reais; há no máximo uma requisição ativa e uma atualização, sem double completion. | REQ-TIME-02,04; AC-TIME-02,04 |
| TEST-TIME-07 | idempotência/regressão | `start`/`stop` repetidos e um segundo ciclo após restart criam exatamente um worker/callback por ciclo e uma requisição por token; o shim observa exatamente um `vTaskDeleteWithCaps(NULL)` por worker e nenhum segundo teardown; após restart, o estado boot-ready e o status Wi‑Fi/IP atual são reavaliados. | REQ-TIME-04; AC-TIME-04 |
| TEST-TIME-08 | teardown/regressão | `stop(9000)` entre a falha da tentativa primária e o início do fallback libera a barreira, quiesce o worker sem iniciar o fallback, observa um único `vTaskDeleteWithCaps(NULL)`, e permite um novo `start` com sincronização válida; nenhuma escrita ocorre após o stop nem há delete duplicado em conclusão tardia. | REQ-TIME-04; AC-TIME-04 |
| TEST-TIME-09 | runtime verificável | O contrato estrutural confere manifesto, dependências, tipo, recursos, stack/fila, registro, `start_all`, checkpoint boot-ready e callback lifecycle do supervisor. | REQ-TIME-04; AC-TIME-04 |
| TEST-TIME-STACK-01 | regressão/configuração | O manifesto de `cyberdeck.time_sync` declara stack de exatamente 16384 bytes e não contém o valor legado 6144; o contrato estrutural verifica ambos no registro do app. | REQ-TIME-04; AC-TIME-04 |
| TEST-TIME-10 | manual/aceitação | No dispositivo, após boot+Wi‑Fi, registrar `sys.info`, `wifi.status`, hora antes/depois e logs; confirmar hora UTC convertida/renderizada, ausência de reboot/panic e continuidade após desconectar/reconectar Wi‑Fi. Repetir com endpoint indisponível/sem IP. | REQ-TIME-01..05; AC-TIME-01..05 |
| TEST-TIME-11 | positivo/aceitação | No harness host, com logger fake instalado, uma resposta válida que realmente conclui `settimeofday` gera exatamente um evento bounded fixo: severidade `info`, tag `time_sync` e payload `success`, sem segundo evento. | REQ-TIME-05; AC-TIME-05 |
| TEST-TIME-12 | negativo/aceitação/regressão | HTTP, TLS/DNS, JSON inválido e `settimeofday` com erro não geram evento. Logger runtime ausente não impede o caminho aprovado: o adaptador direto `event_log_write_durable` ainda confirma sucesso somente após persistência; o worker permanece vivo e o clock só muda quando `settimeofday` conclui. | REQ-TIME-03,05; AC-TIME-03,05 |
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
| `REQ-TIME-04` / `AC-TIME-04` | TEST-TIME-05, TEST-TIME-06, TEST-TIME-07, TEST-TIME-08, TEST-TIME-09, TEST-TIME-STACK-01, TEST-TIME-17 | TEST-TIME-10 |
| `REQ-TIME-05` / `AC-TIME-05` | TEST-TIME-03, TEST-TIME-04, TEST-TIME-11, TEST-TIME-12, TEST-TIME-13, TEST-TIME-14, TEST-TIME-15, TEST-TIME-16, TEST-TIME-17 | TEST-TIME-10 |
| `REQ-TIMELOG-01` / `AC-TIMELOG-01` | TEST-TIMELOG-01, TEST-TIMELOG-03 | TEST-TIMELOG-SERIAL-01 |
| `REQ-TIMELOG-02` / `AC-TIMELOG-02` | TEST-TIMELOG-02, TEST-TIMELOG-04 | TEST-TIMELOG-SERIAL-02 |
| `REQ-TIMELOG-03` / `AC-TIMELOG-03` | TEST-TIMELOG-03, TEST-TIME-12, TEST-TIME-15 | TEST-TIMELOG-SERIAL-03 |
| `REQ-TIMELOG-04` / `AC-TIMELOG-04` | TEST-TIMELOG-04, TEST-TIME-17 | TEST-TIMELOG-SERIAL-04 |
| `REQ-TIMELOG-02..04` / `AC-TIMELOG-02..04` | TEST-TIMELOG-03, TEST-TIMELOG-06 | TEST-TIMELOG-SERIAL-03/04 |
| `REQ-TIMELOG-02..04` / `AC-TIMELOG-02..04` | TEST-TIMELOG-07 | TEST-TIMELOG-SERIAL-02/04 |
| `REQ-TIMELOG-02..04` / `AC-TIMELOG-02..04` | TEST-TIMELOG-08 | TEST-TIMELOG-SERIAL-02/04 |

O gating é cumulativo: `cyberdeck_time_sync_boot_ready()` precisa ter sido
observado e o estado Wi‑Fi precisa estar conectado **e com IP** antes de
enfileirar uma requisição. O worker é assíncrono, usa stack de 16384 bytes e
não pode reintroduzir o valor legado 6144; processa no máximo uma requisição
por token de conexão; `start`, `stop` e teardown são idempotentes/quiescentes.
