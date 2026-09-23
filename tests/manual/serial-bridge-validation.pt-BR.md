# Validacao Manual: Ponte USB Serial-JTAG NDJSON

**Projeto:** cyberdeck5 (M5Stack Tab5)
**Escopo:** ponte manual NDJSON na porta USB Serial-JTAG (`bridge_start`) e a
CLI host `tools/cyberdeck_cli.py`, exercitando correlacao por `rid`, envelopes
de sucesso/erro, tolerancia a logs intercalados, comandos de UI (`ui.*`),
contratos `sys.info`/`wifi.*` e a captura `screen.shot`/`screen.dump` (BMP em
chunks Base64 com CRC32).
**Estado sob validacao:** task `serial_brg` iniciada ao fim do boot; protocolo
NDJSON com uma linha por mensagem, limite de 4096 bytes, `{"rid","ok":true,
"result":...}` no sucesso e `{"rid","ok":false,"error","error_code"}` no erro;
`screen.dump` em frames `start`/`chunk`/`end` com chunks de 1024 bytes.
**Firmware:** `components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp`,
`main/app_main.cpp` (`bridge_start`), host `tools/cyberdeck_cli.py`.
**Duracao estimada:** 30-45 min por execucao.

> Este plano NAO substitui codigo de producao nem altera firmware. Qualquer
> falha encontrada deve ser registrada na tabela abaixo e reportada ao
> developer/reviewer.

---

## 1. Pre-condicoes (reproducibilidade)

| # | Item | Procedimento |
|---|------|--------------|
| P1 | Firmware atual | `. ~/esp/esp-idf/export.sh && idf.py build && idf.py -p /dev/ttyACM0 flash monitor` |
| P2 | Porta serial | `ls -l /dev/ttyACM*`; o usuario do PC tem permissao (grupo `dialout`) |
| P3 | pyserial | `python3 -c 'import serial; print(serial.__version__)'` (ou `pip install pyserial`) |
| P4 | CLI | `python3 tools/cyberdeck_cli.py --help` responde sem erro |
| P5 | WLAN | Rede visivel para o scan (`wifi scan` no TUI funciona) |
| P6 | Monitor serial | Terminal `idf.py monitor` aberto em paralelo (valida logs intercalados) |

Registre em cada execucao: `device_id`, `data`, `commit`/`hash` do firmware,
`ssid`, `observador`.

---

## 2. Protocolo basico (rid / envelopes / limites)

> CLI: `python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 <comando>`.
> Itens P* sao validaveis tambem com um script NDJSON bruto (pty/echo).

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| P10 | `ping` | `{"rid":"...","ok":true,"result":{"pong":true}}`; rid ecoado identico ao enviado |  |  |
| P11 | `ping` com rid customizado | Envelope ecoa exatamente o rid enviado |  |  |
| P12 | Linha NDJSON invalida (ex. `{"rid":1,"type":"ping"}`) | `ok:false` com `error_code` `invalid_json`; dispositivo nao crasha e aceita a proxima linha |  |  |
| P13 | JSON sem `rid` | `error_code` `missing_rid` |  |  |
| P14 | `type` desconhecido (ex. `"foo"`) | `error_code` `unknown_type`; envelope ainda ecoa o rid |  |  |
| P15 | `type` vazio | `error_code` `missing_type` |  |  |
| P16 | Linha com 4096 bytes de payload valido | Aceita (limite inclusive); resposta normal |  |  |
| P17 | Linha > 4096 bytes | `error_code` `too_large` (ou descarte silencioso do LineAssembler do lado CLI), sessao permanece viva |  |  |
| P18 | Bytes UTF-8 invalidos na linha | `error_code` `invalid_utf8`; sessao permanece viva |  |  |
| P19 | `rid` > 64 bytes | `error_code` `too_large`; sessao permanece viva |  |  |
| P20 | Resposta `ok:false` da CLI | Exit code 1 da CLI com o envelope impresso |  |  |

## 3. Tolerancia a logs e leitura fragmentada

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| L1 | `ping` com `idf.py monitor` ativo (logs ESP_LOG rolando) | CLI ignora linhas de log (nao JSON sem `"rid"`/`"ok"`) e devolve o envelope do ping |  |  |
| L2 | Comando durante log pesado (ex. `wifi scan` + saida de boot) | Nenhum frame de log e interpretado como resposta; rid correto |  |  |
| L3 | `sys.info` com output de log intercalado entre bytes do frame | CLI re-monta a linha fragmentada (leitura parcial) e valida o envelope |  |  |
| L4 | Console compartilhado | `printf`/log do console aparece na mesma porta sem derrubar a task da ponte |  |  |

## 4. Comandos de UI

> Pre-condicao: tela TUI no menu inicial, sem sessao SSH (salvo onde indicado).

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| U1 | `ui.echo "abc"` | `ok:true` com `result.text == "abc"` |  |  |
| U2 | `ui.echo` com texto > 2048 bytes | `result.truncated == true` |  |  |
| U3 | `ui.click <x> <y>` sobre um widget clicavel | `ok:true`; `LV_EVENT_CLICKED` dispara no dispositivo (ex.: abrir `wifi search`) |  |  |
| U4 | `ui.click` fora de widgets / coordenadas vazias | `ok:true` sem efeito (ou erro interno tipado); sem crash |  |  |
| U5 | `ui.tap <alvo>` com texto visivel (ex. `help`/`wifi`) | O ancestral clicavel correspondente recebe o clique |  |  |
| U6 | `ui.tap` com alvo inexistente | Erro tipado (ou ok sem efeito conforme contrato); sem crash |  |  |
| U7 | `ui.type "hello world"` | O texto aparece na linha do terminal (ou textarea focada) |  |  |
| U8 | `ui.type` com `\\n` | Segmentos separados por ENTER; texto multilinha chega completo; segmento >1024 bytes e fatiado em fronteira UTF-8 |  |  |
| U9 | `ui.clear` | Terminal limpo apos 64 backspaces + `clear` + ENTER (paridade com `clear` manual) |  |  |
| U10 | `ui.type` rapido repetido (linha longa) | Fila bounded (8) + 30 ms entre eventos: nada perdido, sem flood |  |  |
| U11 | `ui.dump` | `result.nodes` com `class/text/x/y/w/h`; acao visivel refletida no dump (ex.: dump apos `ui.click` mostra o estado novo) |  |  |
| U12 | `ui.dump` com UI ocupada (roda de log girando) | Resposta sob `bsp_display_lock`, sem deadlock; UI permanece responsiva ao toque |  |  |

## 5. sys.info / wifi

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| W1 | `sys.info` | `fw_version`, `idf_version`, `chip=="esp32p4"`, `free_heap` (numero), `uptime` `HH:MM:SS` |  |  |
| W2 | `wifi.status` | `connected`, `has_ip`, `ssid`, `ip` coerentes com o header/icone Wi-Fi |  |  |
| W3 | `wifi.scan` | `networks` com `ssid` (<=32 bytes), `rssi`, `open`; ordenado/estavel; sem hang |  |  |
| W4 | `wifi.scan` com Wi-Fi desabilitado | Lista vazia `[]` ou erro tipado; resposta dentro do timeout (8 s) |  |  |
| W5 | `wifi.scan` repetido rapido | Nenhum resultado vazio/corrompido (semaforo drenado); o dispositivo aceita o proximo scan normalmente |  |  |

## 6. screen.shot / screen.dump

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| D1 | `screen.shot` | `result` com `width`, `height`, `bmp_bytes` coerentes com a tela (ex. 800x480) |  |  |
| D2 | `screen.dump --out screen.bmp` | Exit 0; frames `start`/`chunk`/`end` validados (contiguidade, Base64 canonico, size, CRC32 total) |  |  |
| D3 | Abrir `screen.bmp` gerado | BMP 24-bit identico a tela no momento da captura (sem deslocamento de cor/tela invertida) |  |  |
| D4 | Dump com log pesado intercalado | Chunks continuam sequenciais; validacao de CRC nao falha por causa de logs |  |  |
| D5 | Dump duas vezes seguidas | Sessao anterior encerrada corretamente; segunda captura consistente (rid novo) |  |  |
| D6 | `screen.dump` durante rotacao da tela | Sem crash; se falhar, erro tipado + frame `end` quando a sessao ja comecou |  |  |

## 7. Integracao de boot e concorrencia

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| B1 | Boot completo | `bridge_start` executa apos `wifi_mgr_start`, antes de `CYBERDECK5 iniciado`; falha (se houver) so gera aviso, boot completa |  |  |
| B2 | Monitor serial no boot | Logs de boot aparecem na mesma porta e a ponte ja responde apos o boot |  |  |
| B3 | Comando enquanto SSH online | `ui.*`/`sys.info` funcionam durante sessao SSH (sem travar a task SSH/UI) |  |  |
| B4 | Toque fisico durante `ui.type` longo | Entrada fisica nao perde eventos da injeccao (fila bounded + delay) |  |  |
| B5 | Desconectar/reconectar USB em uso | Nova sessao CLI conecta limpa (`_discard_input`); rid `cli-N` recomeca por sessao |  |  |

## 8. Criterios de aceite do plano

- P10-P20, L1-L4, U1-U12, W1-W5, D1-D6, B1-B5: **todos Pass** em pelo menos
  uma execucao com monitor serial ativo (L-series obrigatorio).
- `make -C tests/host/keymap test_serial_ndjson_dispatch test_serial_screen_dump
  test_serial_cli_tolerance test_serial_sysinfo_wifi`: todos Pass antes do
  roteiro manual (camada pura).
- Nenhum crash/panic/reboot em nenhuma etapa; sem `ESP_LOGE` da ponte.
- Se qualquer item falhar: preencher `Obs` com log serial + passo de
  reproducao e encaminhar ao reviewer via handoff (nunca corrigir em producao
  como tester).

## 9. Riscos conhecidos (escopo do plano)

| Risco | Mitigacao |
|-------|-----------|
| Console `ESP_LOG` compartilha a porta USB Serial-JTAG | L1-L4; LineAssembler descarta nao-JSON; mutex de escrita serializa frames |
| `printf` bruto pode intercalar mid-frame | Tolerancia da CLI ao re-montar linhas; se observado, registrar em L3 |
| Driver ja instalado pelo VFS do console | `bridge_start` pula a instalacao se `usb_serial_jtag_is_driver_installed()` |
| Fila de teclado bounded (8) perde eventos sob flood | 30 ms entre eventos (U10); drops nao devolvem erro |
| `wifi.scan` stale de tentativa anterior | Drena Gives antes + cancelamento no timeout (W4/W5) |
| Sessao `screen.dump` unica (slot unico) | D5 valida nova sessao; rid errado em `get_chunk` → erro tipado |
| Rotacao muda a captura mid-dump | D6: erro tipado + `end` quando a sessao ja iniciou |
| Base64 canonico / padding variante | CLI exige `b64encode(b64decode(s)) == s` |

## 10. Registro de execucao

| Data | Executor | Device | Commit | Resultado | Observacoes |
|------|----------|--------|--------|-----------|-------------|
|      |          |        |        |           |             |

---

## 11. Cobertura host-side e limitacao

**Cobertura automatizada** (`make -C tests/host/keymap`):

| Unidade de producao | Funcoes | Teste |
|---------------------|---------|-------|
| `components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp` | `parse_ndjson_line` (limites, erros tipados, rid/type/UTF-8), `dispatch_one`/`build_envelope` (envelopes, UI/sys/wifi puro) | `tests/host/keymap/test_serial_ndjson_dispatch.cpp` |
| idem | `screen_bmp_size`, `screen_chunk_bounds`, `screen_dump_init/get_chunk`, `crc32` | `tests/host/keymap/test_serial_screen_dump.cpp` |
| idem | `LineAssembler` (stream intercalado com logs, linha >4096, feeds fragmentados), `is_log_line`/`extract_envelope` | `tests/host/keymap/test_serial_cli_tolerance.cpp` |
| idem | `sys_info_to_json/from_json`, `wifi_scan_to_json` | `tests/host/keymap/test_serial_sysinfo_wifi.cpp` |

A camada pura e autocontida: fora de `ESP_PLATFORM` nao ha <PII type="CASE_ID" id="99"/> a
`screenshot_bmp_*` (stride/size/header/conversao espelhados localmente), por
isso tres dos quatro testes linkam somente `SERIAL_BRIDGE_SRC`.

**Limitacao (nao testavel host-side):** a secao `#ifdef ESP_PLATFORM` —
task `serial_brg`, driver USB Serial-JTAG, `esp_log_set_vprintf`, hooks LVGL
(`find_click_at`, `exec_ui_*`, `ui.dump`), `cyberdeck_keyboard_input`,
`wifi_mgr_scan` e a captura `lv_snapshot_take` — exige dispositivo; e o
escopo obrigatorio deste roteiro (secoes 4-7) alem do `idf.py build` que
valida a compilacao do lado device.

**Observacao:** `tests/host/keymap/Makefile` mantem comentarios historicos
"TDD RED" nos alvos `test_serial_*`; eles ja estao em GREEN com a producao
criada (comentario stale, nao comportamento).
