# Fase 3 — baseline e gates físicos do Tab5

O baseline antecede qualquer mudança de desempenho. Não inferir commit ou hash
da imagem instalada a partir de `version.txt`/`sys.info.fw_version`: essa
resposta identifica a versão, mas não autentica o binário. Quando houver
mudança de firmware, registrar hash do build gravado e boot do dispositivo
antes de comparar medições. Poweroff, retirada de SD, corrupção, descarga/carga
forçada e reset para safe mode exigem cenário específico, backup e autorização
de risco; não são sondagens de rotina desta fase.

**Decisão do operador em 05/11/2026:** apenas testes físicos seguros nesta
etapa. Não executar poweroff controlado, retirada/falha física do SD, corrupção
de dados/NVS ou interrupção forçada de boot. Esses cenários ficam explicitamente
pendentes; nenhuma simulação host equivale a aceite do hardware.

## Cenários e modo de evidência

| Fluxo | Observação segura inicial | Teste de carga/falha condicionado | Grandezas que ainda faltam |
| --- | --- | --- | --- |
| Boot/recovery | `sys.info`, versão, uptime crescente, flags recovery e `HOSTED_DIAG` em boot observado sem reset induzido | Falha SD/Hosted, NVS ou interrupção de boot só com preparo/recuperação específicos | duração dos estágios de boot em imagem identificada; stack/min heap |
| Shell/SD/editor/foreground | `screen.wake` se off, `pwd` + `term.dump`, arquivos **descartáveis** em `/data`, transição documentada para editor | Injetar I/O fault primeiro em host; never remove SD in use | latência comando/render, throughput SD, round-trip/hash e grants/teardown |
| Wi-Fi/SSH/HTTP/time sync | `wifi.status`, `GET /battery` e screenshot em LAN própria; só SSH para peer autorizado | Queda de rede controlada/timeout se recuperação segura; não expor credenciais | latência HTTP e erro por estágio, eventos de perda/reconexão, tempo de sync |
| BLE | Inspecionar status/scan sem parear peer novo; conservar bonds | Desconexão/reconexão de peer próprio sem apagar NVS | tempo de scan, contadores de descartes, recuperação após callback stale |
| Netdiag | `status`/`result` sem gerar tráfego remoto | DNS/TCP/HTTPS somente a destino explicitamente autorizado | duração de estágios e cancelamento/join |
| Música | `music status`/`list` apenas após conferir dono de foreground e versão | Play/pause/stop só com faixa e áudio autorizados; `ui.type` envia Enter e pode acionar playback | latência e perdas do trace, carga decoder/I2S, stack do worker |
| Bateria/poweroff | `GET /battery` read-only a 1 Hz sem cabo serial e `sys.info` antes/depois | Ciclo de carga/corte e poweroff só sob procedimento específico por fonte, canário/hash e recuperação | transiente de 100→90%, validade de INA, estado elétrico e série em mudança |

Critérios OS-TAB5-04/05/07/08 **não** ficam aceitos por observação isolada.
O inventário de recursos e limites prévios está em
[`OS-RESOURCE-CONTRACTS.pt-BR.md`](OS-RESOURCE-CONTRACTS.pt-BR.md); testes de
host protegem políticas, não tempos e recursos no ESP32-P4.

## Primeira observação física — 05/11/2026 (parcial)

- USB Serial-JTAG `/dev/ttyACM0` identificado por
  `usb-Espressif_USB_JTAG_serial_debug_unit_...-if00`; CLI `sys.info` respondeu
  `chip=esp32p4`, `idf_version=v6.1`, `fw_version=1.5.0`,
  `safe_mode=false`, `interrupted_boots=0`, `recovery_persisted=true`.
  O HEAD de trabalho era `3284b33` (somente docs/testes após firmware), **mas
  não foi obtido hash/commit verificável da imagem instalada**. Não declarar
  paridade exata de imagem com esse HEAD.
- `sys.info` inicial: uptime `02:12:38`, `free_heap=31462715`,
  `internal_free=177459`, `dma_free=137971`, `psram_free=31285256` bytes.
  `sys.info` posterior: uptime `02:15:20`, `free_heap=31462719`, sem reboot
  observado. Não há série de heap, mínimo histórico de heap ou high-water de
  cada task nessas duas leituras; não extrair orçamento desses números.
- Na primeira tentativa `ping` não recebeu resposta; `sys.info` subsequente
  respondeu. Uma consulta serial concorreu indevidamente com `screen.status`:
  `screen.status` respondeu `off=true` e o outro processo recebeu
  `SerialException` (perda de bytes/porta). A porta USB é de leitor único;
  comandos subsequentes foram serializados e `sys.info` voltou a responder.
  **Essa disputa é erro do procedimento host**, não evidência de reboot/panic.
- Após `screen.wake` (`woke=true`), `ui.type pwd` recebeu ACK e `term.dump`
  confirmou texto exato `/$ pwd\n/\n/$ ` (`truncated=false`). Isso observa
  console e retorno local, não editor/teclado físico/latência de boot.
- `ui.type 'netdiag status'` recebeu ACK e `term.dump` confirmou
  `interface=WIFI_STA_DEF`, campos IP/gateway/DNS e RSSI, com retorno ao prompt
  `/$ ` e sem operação remota DNS/TCP/HTTPS. Os endereços não foram copiados
  para esta evidência. `sys.info` posterior mostrou uptime `02:25:22`,
  `safe_mode=false` e `free_heap=31462343` bytes; ausência de queda de uptime
  nesta janela não valida ainda a duração/recuperação de outras apps.
- Coletor: `tools/collect_battery_series.py` com `--seconds 60`, uma requisição
  read-only por segundo ao `GET /battery` na LAN, sem cabo Serial-JTAG para a
  leitura de bateria. Série: [`tests/manual/evidence/phase3-battery-20261105.csv`](../tests/manual/evidence/phase3-battery-20261105.csv).
  SHA-256 do CSV (LF normalizado): `99a7e8d5e6a95b9b260afcf8eba10ecc65c98c449530131e6731b9666bf956f3`.
  60/60 respostas HTTP 200, sem falhas; 59 intervalos consecutivos de 1000 ms
  (resolução 1 s, duração 59 s entre primeira e última amostra), maior latência
  HTTP de 130 ms. `uptime_s` 8031→8090, `sampled_age_ms` máximo 615;
  `bus_voltage_mv` 8060–8061, `current_ma` 2, `percentage` 100,
  `state=external`, `charge=not_charging`, `protection_active=true`,
  `charger_enabled=false` em toda a série. **Não atravessou transiente de
  partida, corte nem reancoragem**; não comprova estabilidade fora desse estado.

## Pendências para fechar a Fase 3

1. Confirmar identidade do binário em execução de maneira não destrutiva ou
   compilar/gravar uma imagem aprovada como etapa de validação de firmware
   implementado; registrar SHA-256, log de boot e cenário completo. Flash não
   substitui a identificação do baseline anterior.
2. Obter série de 1 s para heap livre/mínimo, stack high-water e overflow sem
   interferir na UI; o `sys.info` atual não expõe mínimos nem high-water. Só
   instrumentar após definir a métrica e seu custo; versionar/validar no Tab5
   qualquer firmware novo. Não fixar budgets a partir da amostra isolada.
3. Medir os cenários seguros da matriz com tempos por estágio e configuração
   idêntica antes/depois; separar tempo de render, rede, storage e codec. Só
   otimizar gargalo observado e reproduzível; sem evidência de gargalo não há
   mudança/redução de stack, fila ou timeout a justificar.
4. Executar injeções seguras de rede/timeout primeiro via shims; preparar
   individualmente os cenários físicos com risco de dado/energia somente após
   nova autorização explícita. Nenhum poweroff, remoção de SD, falha de NVS
   ou teste de carga foi executado aqui. A Fase 3 não pode ser marcada `done`
   enquanto seus critérios físicos obrigatórios estiverem sem evidência.
