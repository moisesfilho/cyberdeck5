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

**Esclarecimento do operador, na mesma data:** poweroff, falha de SD,
NVS/recovery e outros fluxos foram validados por ele **historicamente, em
firmware anterior, não no `1.5.0`**. Aceitar sua confirmação como registro
histórico desses ensaios, sem imputar à imagem atualmente instalada um teste
que não foi feito nela. Para poweroff, há evidência já registrada em
[`poweroff-implementation-plan.md`](../poweroff-implementation-plan.md) e
[`poweroff-validation.pt-BR.md`](../tests/manual/poweroff-validation.pt-BR.md):
versão histórica `cef3257-dirty`/ESP-IDF 5.5.5, corte confirmado pelo operador
nas três fontes suportadas; readback/hash do canário em bateria-only e
bateria+USB, cabo-only declarado sem hash independente. Falha física de SD e
NVS/recovery e os demais fluxos ficam registrados **como confirmação histórica
do operador, sem versão/cenário/log detalhado neste trabalho**. Essa confirmação
não equivale a métricas de heap/stack/latência, nem a aceite de OS-TAB5-04/05/07
no firmware atual; os critérios continuam abertos no dashboard.

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
- Coletor `tools/collect_sysinfo_series.py` com **uma única sessão**
  Serial-JTAG (`--seconds 60`), sem concorrência de outro leitor: série
  [`tests/manual/evidence/phase3-sysinfo-20261105.csv`](../tests/manual/evidence/phase3-sysinfo-20261105.csv),
  SHA-256 `6ae9b389d2769e6296199b05845870f3d060fadab58742da19d774e92a85e149`.
  60/60 respostas `sys.info`, 59 intervalos de 1000 ms, duração 59 s,
  latência de resposta 3–4 ms, uptime `02:33:07`→`02:34:06`. Durante **este**
  estado o `free_heap` ficou em 31462343, `internal_free` em 177371,
  `dma_free` em 137883 e `psram_free` em 31284972 bytes. Valores constantes
  em repouso não são mínimos históricos nem high-water de tasks; ainda não
  cobrem carga, boot ou transientes de recursos.

## Alcance da telemetria atual (auditoria no código, sem novo teste físico)

| Grandeza | Fonte já disponível | O que o dado permite afirmar | Lacuna antes de definir orçamento |
| --- | --- | --- | --- |
| Heap | `handle_sys_info()` em `apps/serial/cyberdeck_serial_bridge.cpp`: `esp_get_free_heap_size()`, `heap_caps_get_free_size()` e `heap_caps_get_largest_free_block()` para as capacidades já publicadas; firmware em desenvolvimento `1.6.0` acrescenta `heap_caps_get_minimum_free_size()` para interno, DMA e PSRAM | Amostras instantâneas e maior bloco livre **na hora do pedido**; os novos mínimos são soma dos menores valores por **região**, possivelmente atingidos em momentos diferentes (nota da API ESP-IDF), e não mínimo global simultâneo | Cenários sob carga, stack das demais tasks e picos de alocação; a série antiga `1.5.0` não contém esses campos |
| Stack | Log `scan stack_high_water_mark_bytes` na saída de `apps/music/cyberdeck_music_app.cpp::scan_entry()`; `sys.info` novo captura `uxTaskGetStackHighWaterMark(nullptr)` em `serial_brg` | Mínimo livre desde a criação **somente** da task amostrada, em bytes no port ESP-IDF; não publicar handle de task encerrada | High-water das outras tasks e sob cargas representativas; não inferir sobra pelo tamanho de stack configurado |
| Boot | `HOSTED_DIAG` em `main/app_main.cpp`: begin/end de `hosted_prepare`, `sd_mount` (inclusive `duration_ms`) e `battery_protection`; a próxima imagem adiciona `app_main_complete` com `boot_ready` e `t_us` | Duração dos estágios instrumentados se o log do **mesmo boot** estiver completo; `app_main_complete` indica término da função, não checkpoint persistido | Identidade da imagem, custo de ROM/bootloader e detalhes de display e serviços; `sys.info.uptime` após horas não mede duração do boot |
| Resposta serial | `collect_sysinfo_series.py` mede monotonicamente a chamada `CyberdeckSession.exchange()` | Latência ponta-a-ponta no host nessa sessão (3–4 ms), incluindo transporte, parser e resposta | Tempo interno da task, de UI, de SD ou percentis sob carga; não usar essa latência como orçamento de firmware |
| Player | `music.diagnostics` (contadores e tempo total/máximo de read/decode/write/backend) e `music.trace` (ring bounded, cursor, `lost`) | Evidência por geração com resultado e perdas somente quando o player é exercitado e os eventos ainda estão no ring | Não há série desta imagem/cenário; não concluir sobre stutter, carga do codec ou throughput com contadores zerados/sem execução |

**Instrumentação mínima em preparação:** os campos de mínimo e stack acima
foram escolhidos por terem custo limitado à consulta `sys.info`; o log de fim
do `app_main` não adiciona polling. Mudanças de firmware exigem versão, build,
teste host e validação na imagem gravada. Não comparar a imagem antiga `1.5.0`
sem hash atestado com a nova como se fossem o mesmo baseline. Nenhum budget
numérico novo é definido por esta auditoria.

## Injeção de falhas no host: fronteira do que é comprovável

As fixtures em `tests/host/keymap/` permitem exercitar estes casos sem mudar a
rede do Tab5 nem apagar dados. Em 05/11/2026,
`make -C tests/host/keymap test_time_sync test_netdiag_controller test_wifi_event_dispatch test_net_coordinator`
terminou com status 0; netdiag controller: 122 checks/0 falhas; Wi-Fi dispatch:
112/0; net coordinator: 151 checks; time sync: alvo executado sem falha.

| Fluxo host | Cenário coberto pela fixture | O que ainda não é provado |
| --- | --- | --- |
| `test_time_sync.cpp` | Timeout, DNS/TLS/HTTP inválido, fallback, stop entre etapas e falha de persistência do evento | Deadline real no rádio/stack lwIP, quiescência física após perda de Wi-Fi |
| `test_netdiag_controller.cpp` | Falhas por estágio DNS/TCP/HTTPS, timeout, grant revogado, cancelamento e token stale | Tempo em rede real, fechamento de socket em disputa e estabilidade da UI |
| `test_wifi_event_dispatch.cpp` e `test_net_coordinator.cpp` | Eventos stale, rollback de segredo não confirmado, retry/teardown, pressão de fila e prioridade de cancelamento | Comportamento do driver Hosted/C6 e reconexão no dispositivo |

São testes **host** de política e dos adapters simulados, não injeção física de
falha nem aceite de OS-TAB5-04/07. Ao observar falha real mais tarde, registrar
token/geração, resultado e uptime antes/depois sem atribuir silêncio a sucesso.

## Imagem instrumentada e observação física — 05/11/2026 (parcial)

- Snapshot do código `0917276`, `version.txt=1.6.0`, ESP-IDF v6.1,
  ESP-Hosted `3.0.10` com CPU-staging verificado. `make -C tests/host/keymap
  clean test` e `verify` passaram; `idf.py build` passou com 38 % da partição de
  app livre. SHA-256 de `build/cyberdeck5.bin`:
  `4dbe949e3f0fa5a971d6c9c36c2acab0f1a9cd0910aed36c3bcc803b017f5f4e`.
  Flash em `/dev/ttyACM0` com `idf.py -p /dev/ttyACM0 flash` retornou
  **`Hash of data verified`**; não houve erase NVS nem SD. É evidência do
  binário gravado pelo processo de flash, não atestação criptográfica remota.
  `sys.info` após o flash respondeu `fw_version=1.6.0`, `safe_mode=false`,
  `interrupted_boots=0` e `recovery_persisted=true` com uptime `00:00:06`.
- Coletor Serial-JTAG `tools/collect_sysinfo_series.py`, **sessão exclusiva**,
  60 amostras a 1 Hz (59 intervalos de 1000 ms; 59 s entre extremos), sem erro,
  uptime `00:00:28`→`00:01:27`:
  [`phase3-sysinfo-1.6.0-20261105.csv`](../tests/manual/evidence/phase3-sysinfo-1.6.0-20261105.csv),
  SHA-256 `74b241cb8e894d6142f47dddde413d79b20e0f6dc6c8166c94089b1760512968`.
  `internal_free` 177259–177459 B, `internal_min_free` 136400 B;
  `dma_free` 137771–137971 B, `dma_min_free` 96912 B;
  `psram_free` 27535160–31285016 B, `psram_min_free` no fim
  27532636 B; `serial_stack_high_water_bytes` 10572–10620 B.
  Latência host de `sys.info` 4–8 ms. **Esses mínimos são somas de marcas por
  região desde o início da imagem**, não mínimos simultâneos nem medição de
  stacks de todos os workers. Não extrapolar budgets desta janela curta.
- Carga moderada **durante** a série serial: três `GET /screenshot`
  sequenciais, HTTP 200, BMP 1280×720×24 de 2764854 B em 3,195 / 3,425 /
  3,116 s no host. Arquivos BMP ficaram apenas em `/tmp/opencode/` por conterem
  conteúdo da tela. A diferença persistente de ~1,8 MiB de PSRAM após a
  primeira captura corresponde ao buffer RGB565 estático retido por
  `cyberdeck_display_port.cpp::s_snapshot_storage` (`BSP_LCD_H_RES ×
  BSP_LCD_V_RES × 2 + 64` bytes, `std::call_once`); a cópia temporária por
  request é liberada em `cyberdeck_display_release`. **Não** atribuir a
  diferença a vazamento sem outro teste. Depois do reset controlado, o heap
  retornou ao patamar pré-captura; a série não demonstra ausência universal de
  vazamentos.
- Coletor HTTP independente `tools/collect_battery_series.py`, 60 respostas
  HTTP 200 a 1 Hz, sem falhas (59 s entre extremos), uptime `59`→`118` s:
  [`phase3-battery-1.6.0-20261105.csv`](../tests/manual/evidence/phase3-battery-1.6.0-20261105.csv),
  SHA-256 `345eacef6f2eb0207e6045189daa53c499207693c50af978f3b5afee81b8de54`.
  Tensão 8128–8170 mV, corrente -216 a -151 mA, percentual 98 constante,
  `state=charging`, `charge=not_charging` (sinal CHG_STAT separado da corrente),
  proteção inativa e carregador habilitado durante a série; idade da amostra
  0–997 ms e latência HTTP 23–331 ms. Não atravessou o corte de carga nem a
  reancoragem, portanto **não** aceita o ciclo completo da bateria.
- `idf.py monitor` iniciou **um reset controlado após o fim dos coletores**;
  `timeout 35s` terminou com código 124 esperado por limite do monitor, não
  por falha do firmware. Log bruto local (não commitado: contém dados de rede)
  `/tmp/opencode/phase3-boot-1.6.0-20261105.log`, SHA-256
  `d761bfc5a78b400898b149fe56cc3f579802e5451681d63e63f80158d2ba3afd`.
  Linhas `HOSTED_DIAG`: `hosted_prepare` 73542→2434408 us (2360,866 ms);
  `sd_mount` 2434632→2504271 us (`ESP_OK`, 69 ms no log);
  `battery_protection` 5480023→5593955 us (`ESP_OK`, 113,932 ms);
  `app_main_complete boot_ready=1 t_us=9352450`. O checkpoint recovery
  retornou `ESP_OK persisted=1`; após o monitor, `sys.info` indicou uptime
  crescente, safe mode false. `t_us` mede desde o timer ESP, **não** ROM,
  bootloader ou tempo total desde energização. Sem panic/WDT no log capturado;
  ocorreram avisos não fatais de GPIO ISR já instalado e IRK do NimBLE (`rc=8`),
  que não são evidência de BLE funcional.
- I/O SD e shell em imagem `1.6.0`: `fs.write` criou **apenas** o descartável
  `/sdcard/data/phase3-sysinfo-20261105.txt` (15 B, CRC32 2069341589);
  `ui.type 'cat /data/phase3-sysinfo-20261105.txt'` + `term.dump` exibiu
  `PHASE3-1.6.0-OK`. `rm` removeu somente esse arquivo; `cat` subsequente
  retornou `file not found`. Uptime `00:00:42`→`00:01:49`, sem reboot
  inesperado. Isto não valida editor, round-trip de encoding ou falha física
  do SD.

### Player: incidente na inspeção sem playback (nova rodada, **não aceita**)

Na imagem `1.6.0` anterior (SHA-256
`4dbe949e3f0fa5a971d6c9c36c2acab0f1a9cd0910aed36c3bcc803b017f5f4e`),
`ui.type "music"` abriu a superfície e iniciou o
scan. Uma tentativa posterior de `ui.type "music status"` recebeu ACK, mas
`term.dump` **não** mostrou a resposta esperada e `ui.dump` exibiu progresso
de reprodução (`5s / 121s`). O código confirma a causa: no foreground do
player, `ui.type` injeta Enter e `cyberdeck_music_manager::manager::handle`
roteia Enter para `action::toggle`; o texto **não** foi executado pelo shell.
`music.diagnostics` read-only mostrou uma geração ativa, sem falhas, e depois
uma geração aposentada naturalmente (`read_calls=512`, `failures=0`); não houve
novo `play` intencional nem repetição da ação insegura. Uma tentativa de clique
em botão durante a proteção de tela retornou `screen_off`, sem efeito; após
`screen.wake`, o clique foi aceito, mas `pause_count=0`, portanto **não** atribuir
parada ao clique. O roteiro `tests/manual/music-player-validation.pt-BR.md`
foi corrigido para proibir `ui.type` após abrir o player. Não usar
esta rodada como aceite de scan seguro, player, áudio ou latências. Nenhuma
rota HTTP nova é necessária para esta lacuna: o protocolo Serial-JTAG já
expõe diagnósticos do player e permite uma consulta read-only bounded.

### Consulta segura do player — imagem instrumentada subsequente (parcial)

- Código `96cc6bd`, mesma versão de desenvolvimento `1.6.0` **sem tag**,
  binário diferente do baseline anterior: SHA-256 de `build/cyberdeck5.bin`
  `ca6aaa61595a25de9f1b80441fb04e8680f95afdf0c8ed4884f0eece4afffcd9`.
  `idf.py build`, `make -C tests/host/keymap clean test`, `verify` e
  `test_os_dashboard_contract` passaram; flash em `/dev/ttyACM0` confirmou
  `Hash of data verified`. `sys.info` pós-flash reportou `fw_version=1.6.0`,
  `safe_mode=false`, zero boots interrompidos, recovery persistido e uptime
  `00:00:06`; hash de flash **não** é atestação independente da imagem rodando.
- Com a app parada, `music.status` retornou `player_not_running`. Após
  `screen.status` (`off=false`) e **uma única** chamada `ui.type music` no shell,
  não houve mais injeção de teclas. `music.status` informou `state=stopped`,
  `queue_scanning=false`, `track_count=14`, posição/duração 0; o scan já havia
  terminado antes da primeira consulta, portanto sua latência e high-water
  **não foram medidos**.
- Coletor `tools/collect_music_status_series.py`, uma única sessão USB
  Serial-JTAG, `--seconds 60`, resolução 1 Hz: 60/60 respostas válidas, 59
  intervalos de 1000 ms, sem falhas, uptime `00:00:28`→`00:01:27`:
  [`phase3-music-status-1.6.0-20261105.csv`](../tests/manual/evidence/phase3-music-status-1.6.0-20261105.csv),
  SHA-256 `47efd7e0d0b8d275b6630039ec596d6e3e0c56904edbecb385f423455f57106e`.
  Em toda a série `state=stopped`, `queue_scanning=false`, contagem 14,
  posição/duração 0. `internal_free=177003` B,
  `internal_min_free=129820` B, `dma_free=137515` B,
  `dma_min_free=90332` B; `psram_free` 31241064–31243580 B,
  `psram_min_free=31146488` B; folga mínima reportada da task serial
  10476–10608 B. Latência conjunta das duas consultas no host 5–10 ms.
  O CSV não contém SSIDs, títulos nem caminhos de faixas.
- `music.diagnostics` antes e depois da série confirmou
  `generations_queued=0`, `generations_started=0`, `generations_retired=0`,
  `failures=0` e `read_calls=0`; `sys.info` final indicou uptime
  `00:01:45`, `safe_mode=false`, sem reboot observado. Isso valida **apenas**
  inspeção passiva e estabilidade nessa janela; não atesta reprodução,
  teardown, custos sob decoder ativo, BLE, editor ou budgets da Fase 3.

## Pendências para fechar a Fase 3

1. A imagem nova tem build/hash, flash verificado e log de boot; falta uma
   atestação independente do binário em execução. A identificação da imagem
   anterior `1.5.0` continua limitada à versão, não ao hash; não comparar
   binários anteriores como iguais com base somente em `fw_version`.
2. As séries de 1 s cobrem repouso/captura HTTP, player parado e uma task
   serial. Faltam stack high-water das outras tasks, overflow e tempo de
   operações de editor/BLE/player ativo sob cenários representativos. Não fixar
   budgets a partir de uma janela curta nem da soma de mínimos por região.
3. Medir os cenários seguros da matriz com tempos por estágio e configuração
   idêntica antes/depois; separar tempo de render, rede, storage e codec. Só
   otimizar gargalo observado e reproduzível; sem evidência de gargalo não há
   mudança/redução de stack, fila ou timeout a justificar.
4. Executar injeções seguras de rede/timeout primeiro via shims; preparar
   individualmente os cenários físicos com risco de dado/energia somente após
   nova autorização explícita. Nenhum poweroff, remoção de SD, falha de NVS
   ou teste de carga foi executado aqui. A Fase 3 não pode ser marcada `done`
   enquanto seus critérios físicos obrigatórios estiverem sem evidência.
