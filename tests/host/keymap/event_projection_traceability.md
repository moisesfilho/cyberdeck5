# Projeção de eventos (`events.txt`) — contrato e rastreabilidade host

Escopo: recuperação de eventos legíveis para hosts. O binário `events.log`
permanece a **fonte de verdade**; `events.txt` é uma projeção best-effort
somente leitura, exposta por dois transportes de recuperação (Serial-JTAG e
HTTP local).

## Arquivos e rotações

- Caminho canônico fixo: `CYBERDECK_TEXT_LOG_PATH` =
  `/sdcard/.cyberdeck5/logs/events.txt` (`cyberdeck_paths.h`). Não existe
  caminho controlado pelo chamador.
- Geração `0` é o arquivo ativo `events.txt`; gerações arquivadas são
  `events.txt.1` … `events.txt.7` (`TEXT_ROTATION_COUNT = 7`).
- Limite por arquivo: `TEXT_FILE_LIMIT = 1 MiB`. Pior caso: **8 arquivos ×
  1 MiB = 8 MiB** (`events.txt` + 7 arquivados).
- Rotação (`rotate_text_if_needed`) só ocorre quando
  `st_size + incoming > 1 MiB`: desloca `.6→.7` … `.1→.2` com `unlink` do
  destino e `rename`, depois `events.txt→.1`. Arquivo ativo é **movido, nunca
  truncado**. O `.7` mais antigo é descartado (perda por desenho).
- Leitura/`event_log_text_size` percorrem as gerações da mais antiga (`.7`) para
  a mais nova (`events.txt`), nessa ordem, de modo que o offset é estável entre
  chamadas para um mesmo conjunto de arquivos.
- Cada registro projetado é uma linha física única:
  `"%Y-%m-%d %H:%M:%S <level> <tag>: <message>\n"`. Timestamp em UTC quando
  `unix_us >= 1577836800` e `gmtime_r` tem sucesso; caso contrário, fallback
  determinístico `up:<uptime_ms>ms`. `CR` e `LF` em tag/mensagem viram espaço
  e a linha termina em exatamente um `LF`, portanto nenhum campo injeta linha.

## Relação com o binário fonte de verdade

- `events.log`: anel binário de 16 MiB em registros de 256 bytes
  (`RECORD_MAGIC "GOL5"`, `sequence`, checksum CRC32), reconstruído por
  `rebuild_state` na abertura.
- Ordem de gravação: o `log_task` grava primeiro o registro binário
  (`write_record`, checksum) e só então enfileira a mesma `LogRecord` em
  `s_text_queue` com `xQueueSend(..., 0)` — **não bloqueante, sem retry**. O
  `write_record` nunca executa I/O de projeção.
- O `text_log_task` (stack 4096, prioridade 1) recebe da fila, toma
  `s_text_mutex`, aplica `append_text_projection` (limita/rotaciona, `fwrite`,
  `fflush`, `fsync` antes de publicar sucesso) e libera o lock.
- Consequência: a projeção pode perder registros (fila cheia, SD lento/cheio)
  sem afetar o binário; o binário nunca é regenerado a partir do texto.
- Distinção de contrato: `event_log_write_durable` usa a fila durável e confirma
  somente após `write_record`/`fsync` em `events.log`; o eventual `xQueueSend`
  para `s_text_queue` é apenas uma cópia best-effort. Os cenários
  `TEST-TIMELOG-03/04/06` e `test_durable_append_is_not_projection_io` não
  classificam essa persistência binária como I/O da projeção.
- Coordenação separada: `s_text_mutex` (projeção) e `s_recent_mutex` (estado
  recente) são mutex distintos, com `TEXT_MUTEX_TIMEOUT = pdMS_TO_TICKS(100)`
  tipado (timeout → `ESP_ERR_TIMEOUT`).
- `event_log_init` cria mutex, fila e task de projeção; falha na task de
  projeção remove os handles, e falha na `log_task` restaura
  `esp_log_set_vprintf`, executa `vTaskDelete(s_text_task)` e limpa handles.
  `s_initialized = true` só é publicado depois das duas tasks.

## Leitura bounded (`event_log_text_read`)

- Contrato: `capacity` em `1..1024`, senão `ESP_ERR_INVALID_ARG`; `out_read`
  zerado na entrada.
- Abertura de snapshot: `open_text_snapshot` mantém
  `FILE *files[TEXT_ROTATION_COUNT + 1]` abertos e `fstat` por geração, **libera
  `s_text_mutex` antes de qualquer `fseek`/`fread`** e só então faz o I/O SD.
  O `rename` da rotação pode prosseguir sem invalidar as páginas já abertas.
- `offset >= total` → `ESP_OK` com `*out_read = 0` (EOF explícito, não erro).
- `fseek`/`fread` com 0 bytes em posição não vazia → `ESP_FAIL`: uma posição
  válida nunca pode parecer EOF bem-sucedido.
- Cada chamada entrega **um único chunk de um único arquivo**; o chamador
  reemite até `offset >= size`. Não há leitura que atravesse geração.

## Recuperação Serial-JTAG — `events.read`

- Comando NDJSON registrado em `k_known` na ponte; sem `path` e sem
  `../` (nenhum caminho controlado pelo chamador).
- Enquadramento: `start` (`size` = `event_log_text_size()`), zero ou mais
  `chunk` (`offset`, `size`, `b64`) e `end`
  (`send_frame(frame_event(req, "end"))`).
- `constexpr std::size_t k_chunk = 1024` com `base64_encode(bytes, count)`;
  `offset += count` até `offset < total`.
- Falha: `event_log_text_read` != `ESP_OK` ou `count == 0` →
  `dispatch_error::internal` com `"events.txt indisponivel"` e quebra do laço;
  `stop_requested()` também encerra a leitura. O frame `end` é enviado nos
  dois casos, então o cliente sempre consegue correlacionar por `rid`.

## Recuperação HTTP — endpoint fixo

- `GET /events.txt` registrado no mesmo servidor de `/screenshot`
  (`config.max_uri_handlers` 1 → 2); falha ao registrar o segundo handler
  derruba o servidor (`httpd_stop`), sem servidor pela metade.
- Não-GET → `405` (`HTTPD_405_METHOD_NOT_ALLOWED`); peer não local →
  `403` (`HTTPD_403_FORBIDDEN`) via `is_local_peer`.
- Compartilha `s_request_mutex` com o screenshot; timeout de 1000 ms →
  `503` (`"another recovery is in progress"`).
- Resposta `text/plain; charset=utf-8`, enviada em `httpd_resp_send_chunk` de
  até 1024 bytes; o terminador (`httpd_resp_send_chunk(req, NULL, 0)`) só é
  enviado em caso de sucesso — em falha o retorno é `ESP_FAIL` e a conexão é
  encerrada sem corpo completo.
- O handler não lê `req->uri` nem query string: o path é fixo por registro de
  URI, sem superfície de path traversal.

## Falhas e limitações conhecidas

- Projeção best-effort: entrada descartada silenciosamente quando a fila
  textual está cheia ou o caminho do SD falha; não há retry nem métrica.
- `event_log_text_size()` devolve `0` tanto para arquivo vazio quanto em timeout
  de mutex — o consumidor não distingue os dois casos.
- `/events.txt` sobre HTTP segura `s_request_mutex` durante toda a resposta:
  até 8 MiB de stream serializam com o endpoint de screenshot.
- Sem `Range`/resume no HTTP: uma interrupção reinicia a leitura do zero. O
  transporte Serial-JTAG recomeça do offset informado pelo cliente.
- Rotação descarta a geração `.7`; o total em SD pode chegar a 8 MiB sem
  qualquer sinal para o host.
- Chunk nunca cruza fronteira de geração: um mesmo offset lógico pode mapear
  para arquivos diferentes entre chamadas se a rotação ocorrer no meio.
- O alvo `make test_event_projection_contract` não lista
  `cyberdeck_paths.h` como pré-requisito; alterar apenas esse header não
  dispara reexecução do contrato.
- O gate host é **inspeção estrutural + modelos bounded determinísticos**: não
  compila nem executa `ESP_PLATFORM`, não fala com SD, rede ou hardware.

## Matriz REQ/AC → TEST → evidência

| Requisito/AC | Cenário | Teste | Evidência |
|---|---|---|---|
| REQ-EVENT-05/AC boot e append durável best-effort | init sobe task binária e de projeção; `write_record` não faz I/O de projeção; `log_task` enfileira; timestamp determinístico; `fflush`+`fsync` antes de publicar | `test_event_projection_contract.py::test_boot_and_append_contract` | host (PASS) |
| REQ-EVENT-05/AC consumo da fila normal | `log_task` recebe `s_queue` depois da fila durável e antes do fallback de notify, persistindo e atualizando recentes | `test_event_projection_contract.py::test_log_task_receives_normal_queue_before_notify_fallback` | host (PASS) |
| REQ-EVENT-11/AC ACK/timeout sem duplicação por token | enqueue aceito espera ACK bounded; timeout retorna `ESP_ERR_TIMEOUT`, libera a posse do chamador e não reenfileira o mesmo token; ACK libera a posse do writer | `test_event_log_contract.py::test_durable_ack_timeout_has_single_token_ownership` | host (PASS) |
| REQ-EVENT-06/AC rotação bounded 1 MiB x 8 | limites explícitos de tamanho e geração; desloca só as gerações bounded; `rename` do ativo; modelo retém no máximo 7 arquivados | `test_event_projection_contract.py::test_rotation_is_bounded_and_model_preserves_order` | host (PASS) |
| REQ-EVENT-07/AC leitura bounded somente leitura e EOF seguro | rejeita capacidade `>1024`/inválida; sem `fwrite`/`rename`; EOF é zero-byte bem-sucedido; offset inclui rotacionados; Base64 bounded | `test_event_projection_contract.py::test_read_is_bounded_readonly_and_eof_safe` | host (PASS) |
| REQ-EVENT-01/AC mutex-I/O e timeout tipado | locks separados, timeout bounded e leitura fora do lock | `test_event_projection_contract.py::test_mutex_io_timeout_and_snapshot_lifecycle_contract` | host (PASS) |
| REQ-EVENT-02/AC init/task rollback | falha na criação da task de projeção ou da task binária | `test_event_projection_contract.py::test_mutex_io_timeout_and_snapshot_lifecycle_contract` | host (PASS) |
| REQ-EVENT-03/AC snapshot paginado estável sob rotação | páginas continuam vendo descritores do snapshot após rename/rotação | `test_event_projection_contract.py::test_paged_snapshot_is_stable_under_rotation_model` | host (PASS) |
| REQ-EVENT-04/AC CR/LF | tag/mensagem não injetam linhas e cada projeção termina em LF | `test_event_projection_contract.py::test_crlf_projection_normalization_contract` | host (PASS) |
| REQ-EVENT-08/AC recuperação Serial-JTAG | `events.read` usa a projeção, sem path controlado pelo chamador, frames start/chunk/end, chunk ≤1024 Base64, erro em leitura que não avança | `test_event_projection_contract.py::test_serial_events_read_chunks_eof_and_errors` | host (PASS) / **manual pendente** |
| REQ-EVENT-09/AC endpoint HTTP fixo e seguro | só `GET /events.txt`, 405, `is_local_peer`/403, sem `req->uri` nem query string, caminho canônico fixo | `test_event_projection_contract.py::test_http_fixed_readonly_endpoint_and_security` | host (PASS) / **manual pendente** |
| REQ-EVENT-10/AC não regressão Serial-JTAG | `ping`, `screen.dump`, `fs.write` com `O_NOFOLLOW` e erros tipados seguem presentes | `test_event_projection_contract.py::test_existing_contracts_remain_present` | host (PASS) |

## Gate e evidência executada

- Host: `make -C tests/host/keymap test_event_projection_contract` →
  `PASS: event projection contract (10 scenarios;
  TEST-EVENT-BOOT/APPEND/QUEUE/ROTATE/READ/SECURITY/REGRESSION)`. O alvo também entra
  no agregado `make test`.
- Regressão do binário: `make -C tests/host/keymap test_event_log_contract` →
  PASS (7 cenários, incluindo ACK/timeout e ownership de token).
- Nenhum outro alvo host foi executado como parte deste documento; não há
  alegação de cobertura além dos alvos citados.

## Validação manual — PENDENTE (não executada)

Nada foi validado em hardware. Não houve flash, gravação, `events.read` real
nem requisição HTTP; o firmware em execução não foi conferido contra commit.
Registrado explicitamente como pendente, conforme a seção de validação de
`AGENTS.md`:

- [ ] `events.read` via Serial-JTAG: `start` com `size`, sequência de `chunk`
      `b64`, `end`; conferir remontagem idêntica ao arquivo e ausência de
      `panic`/reboot (divergência de queda de uptime em `sys.info`).
- [ ] `events.read` com `events.txt` acima de 1 MiB: observar `.1`…`.7`
      presentes, ordem de leitura e truncamento da geração mais antiga.
- [ ] `GET /events.txt` pela rede local: status 200 e corpo completo; `405`
      em outro método; `403` de peer não local; `503` com screenshot
      concorrente.
- [ ] LED/indicador de heap e degradação de latência durante o stream de até
      8 MiB, e confirmação de que `/screenshot` responde normalmente em
      seguida.
- [ ] Evidência a anexar: porta, identificação do dispositivo, commit, horário,
      comandos, respostas JSON, logs do monitor e `sys.info` antes/depois.
