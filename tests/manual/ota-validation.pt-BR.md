# Validação OTA no Tab5

Estado em 06/11/2026: **validação física parcial**. Os testes host e o build não substituem
confirmação por tecla/toque físico nem rollback provocado. Não execute teste de falha removendo
o SD: o produto exige o cartão para iniciar e esse cenário pode gerar um loop
de reboots.

1. Anote commit, SHA-256 do `build/cyberdeck5.bin`, tamanho e versão; confirme
   flash de 16 MiB, dispositivo `/dev/ttyACM0`, cartão SD presente e fonte
   estável. Antes da primeira migração, salve readback privado da NVS e do
   particionamento antigo; o layout novo desloca a partição factory de
   `0x10000` para `0x20000`. Inspecione o comando de flash gerado por `idf.py`.
   Faça um único flash USB aprovado e registre boot, Wi-Fi, `sys.info` e NVS.
2. Provisione `python3 tools/update_firmware.py --provision --port /dev/ttyACM0`.
   Não anexe ao relatório a chave retornada. Confirme permissões 0600 no
   arquivo local. Após isso, desconecte o USB e use somente a LAN.
3. Capture `GET /ota/status` antes do teste. Prepare **duas imagens válidas com
   versões distintas** para poder comprovar atualização e rollback, sem
   alterar os dados do SD. Anote partição em execução e estado da imagem.
4. Verifique `--confirm`: envio concluído, modal visível no Tab5, `Esc`
   cancela sem mudar partição/versão; repita e confirme com `Enter`/botão.
   Verifique versão e slot após o reboot e que `running_valid=true` após o
   checkpoint. Reexecute o mesmo cenário usando apenas toque.
5. Verifique `--silent`: não deve abrir modal nem aceitar tecla; resposta
   `202`, reboot, alternância de slot, versão esperada, Wi-Fi/SD/UI funcionando,
   `running_valid=true`. Use o script Python e não faça flash USB nessa etapa.
6. Negativos: MAC inválido, challenge vencido/reutilizado, modo trocado depois
   do HMAC, tamanho acima do slot, hash incorreto, imagem truncada e upload
   interrompido. Nenhum caso deve alterar a partição de boot; o firmware
   anterior continua funcional. Não envie firmware propositalmente malformado
   no modo silencioso quando a proteção ainda não tiver sido confirmada.
7. Rollback: use imagem de teste que falhe **após a seleção OTA e antes do
   checkpoint**, com procedimento revisado separadamente, reboot controlado e
   coleta de logs. Confirme que o bootloader retorna ao slot anterior e que
   nenhum agente interpreta ausência de resposta HTTP durante reboot como
   sucesso. Não repita falhas físicas, reboot loops ou corte de alimentação
   sem analisar logs e obter autorização apropriada.

Registrar tempos, status HTTP, versão/slot antes e depois, validação do SD e
resultado em cada caso. Se a primeira migração ou qualquer reboot for
inconclusivo, interromper o fluxo e manter a validação física pendente.

## Evidências desta entrega — 06/11/2026

- Imagem ESP-IDF 6.1, firmware `1.9.0`, SHA-256
  `d3227fae9d3ea6f55e2f87be0aa2ce1bdf1df5f416b5ea59210c054c182f12ab`;
  build aprovou (`0x280a00` bytes, 37% livres). Backup do layout anterior:
  `/tmp/opencode/cyberdeck5-pre-ota-flash-20261106.bin` (0600, 4.227.072 bytes).
  Flash inicial USB em `/dev/ttyACM0` concluiu com hash da gravação verificado.
  `sys.info`: firmware 1.9.0, `safe_mode=false`, `interrupted_boots=0`,
  `recovery_persisted=true`; `wifi.status`: conectado com IP.
- `tools/update_firmware.py --provision --port /dev/ttyACM0` concluiu;
  `~/.config/cyberdeck5/ota.key` tem permissão 0600. **Não anexar a chave**.
- `--silent` pela LAN retornou `{"state":"activating"}`; após reboot,
  `/ota/status` passou de `running_slot=131072` (`factory`) para
  `running_slot=4325376` (`ota_0`), com `running_valid=true` e versão 1.9.0.
  Foi a **mesma versão/imagem**: troca e validação de slot foram comprovadas,
  mas ainda falta ensaio com versões diferentes.
- `--confirm` retornou `awaiting_confirmation`, versão 1.9.0 e prazo 120 s;
  cancelamento por `ui.escape` **injetado pela ponte Serial-JTAG** retornou
  estado `idle`, mesmo slot 4325376, `running_valid=true`. O script saiu com
  erro esperado `Update was cancelled or confirmation expired`. Isso comprova
  a rota de cancelamento, **não** uma tecla física nem um toque real.
- Teste negativo sem escrita de imagem: nonce obtido por `GET /ota/challenge`,
  duas requisições `POST /ota` de 256 bytes com MAC inválido (mesmo nonce)
  receberam HTTP 401; slot e estado permaneceram inalterados, imagem válida.
  O primeiro teste de cancelamento por coletor ad hoc consultou `/ota/status`
  durante upload e sofreu timeout; o servidor só respondeu após o upload
  terminar, marcou `failed` e manteve o slot válido. Repetido corretamente
  aguardando HTTP 202 antes da consulta; não contar a primeira tentativa como
  prova de cancelamento.
- `make -C tests/host/keymap test` concluiu sem falhas (inclui testes OTA,
  contratos de UI e coverage scope); `test_os_dashboard_contract` e
  `git diff --check` passaram.

**Pendente após o primeiro ensaio:** confirmar com tecla/toque reais no Tab5,
versões distintas, hash incorreto e interrupção controlada com
oráculo independente; rollback provocado e perda de energia sob procedimento
revisado. Não declarar aceite físico integral nem repetir falhas de boot sem
analisar previamente as evidências.

## Atualização entre versões distintas — 06/11/2026

- Build da versão `1.10.0` aprovado; imagem `build/cyberdeck5.bin`, tamanho
  `0x280a00`, SHA-256
  `650b32911c328e98eeed989bcf9324b3a681790a525dfba5866e6dedea576475`.
  `make -C tests/host/keymap test` passou integralmente e
  `git diff --check` ficou limpo.
- Pré-condição: dispositivo em `1.9.0`, slot `ota_1` (`8519680`), imagem válida,
  Wi-Fi conectado, `safe_mode=false`, `interrupted_boots=0`, uptime 6m54s.
- Enviado uma vez com `python3 -u tools/update_firmware.py --host
  192.168.68.111 --firmware build/cyberdeck5.bin --confirm`. O servidor entrou
  em `awaiting_confirmation`; usuário confirmou fisicamente no Tab5. Cliente
  reportou `Device booted firmware 1.10.0`.
- Pós-boot: `/ota/status` mostrou `idle`, versão `1.10.0`, slot `ota_0`
  (`4325376`) e `running_valid=true`; `sys.info` mostrou firmware 1.10.0,
  `safe_mode=false`, `interrupted_boots=0`, `recovery_persisted=true` e uptime
  crescente de 15 s para 26 s; `wifi.status` conectado com IP. O teste comprova
  atualização real de versão, ativação após confirmação física e boot válido.
- **Sintoma visual reportado pelo usuário:** a tela piscou várias vezes antes
  de aparecer a mensagem/modal de confirmação. Depois da confirmação, o sistema
  respondeu normalmente; não foi reportado se as piscadas pararam. Portanto,
  o ensaio OTA passou funcionalmente, mas **não passou como visualmente limpo**.
  Não iniciar novos uploads até investigar/capturar o comportamento das piscadas.

### Investigação do sintoma visual — 06/11/2026

- O usuário descreveu alternância entre o terminal e uma tela azul-clara que
  aparece quando o display está desligando, semelhante ao que vê em reinícios
  ou falhas; relatou que aconteceu somente durante o upload e parou quando o
  modal apareceu.
- Estado consultado durante a investigação: firmware 1.10.0, uptime 5m03s,
  `safe_mode=false`, `interrupted_boots=0`, OTA `idle`/`running_valid=true` e
  `screen.status` retornou `off=true`. A captura `screen.dump` foi recusada com
  `screen_off`; não foi acordada a tela nem feita nova atualização.
- Código: `process_ota_confirmation()` só apresenta o modal quando OTA entra
  em `awaiting_confirmation` após terminar hash e gravação. O fluxo de upload
  não chama funções de display nem reinicia; aceitação explícita agenda um
  único reboot após 1,5 s. A proteção de tela tem timeout persistido de 1 min;
  `screen_off.cpp` põe brilho em 0 e troca para uma superfície LVGL preta. O
  azul-claro relatado não é cor desenhada por esse overlay.
- Log SD contém inicializações completas LVGL/MIPI em 14:09:24 e 14:16:54,
  cada uma seguida por boot normal. Journal NVS mostra `boot_begin` seguido de
  `checkpoint`, sem boots interrompidos. Não há panic, watchdog ou brownout nos
  logs persistidos, e o firmware não registra `esp_reset_reason`; o segundo
  boot não pode ser correlacionado com certeza com o upload nem classificado.
- Conclusão: indício de que o azul está associado a tela desligando/reinício,
  mas causa não determinada. A tela já estava em estado `off` durante a
  investigação, o que torna a proteção de inatividade uma hipótese, não prova
  das piscadas observadas durante o upload. Próxima coleta segura: observar
  com a tela ativa e `screen.timeout` temporariamente desabilitado pelo operador,
  monitor serial aberto desde antes do upload e registrar timestamps/razão de
  reset; não repetir OTA até combinar essa captura.

## Ensaio adicional de confirmação manual — 06/11/2026

O usuário relatou reinícios e piscadas durante as duas OTAs anteriores. Antes
de repetir, `sys.info` mostrou firmware 1.9.0, `safe_mode=false`,
`interrupted_boots=0` e uptime crescente; `/ota/status` mostrou `idle`, slot
`ota_0` (`4325376`), `running_valid=true`. `events.read` exportou o log SD e o
journal NVS de recovery: as entradas recentes do journal são pares
`boot_begin`/`checkpoint` concluídos sem incremento de boots interrompidos;
o log SD mostra inicializações repetidas durante a janela de atualização, mas
não identifica a causa de cada reboot. A proteção de tela estava configurada
para 1 minuto. Essas evidências não provam que as piscadas não ocorreram.

Com autorização expressa para **um** novo ensaio, foi executado
`python3 -u tools/update_firmware.py --host 192.168.68.111 --firmware build/cyberdeck5.bin --confirm`.
O script reportou `awaiting_confirmation` e depois `Device booted firmware
1.9.0`; nenhuma tecla foi injetada pela automação. O usuário confirmou ter
**tocado fisicamente no botão Confirmar do Tab5**. Após o reboot, o endpoint
mostrou `idle`, `running_slot=8519680` (`ota_1`) e `running_valid=true`;
`sys.info` confirmou firmware 1.9.0, `safe_mode=false`,
`interrupted_boots=0` e uptime 14 s. O usuário informou **um único reboot,
sem novas piscadas**, e estabilidade posterior. A mesma imagem/versão foi
reinstalada; a validação de versões distintas e o rollback provocado continuam
pendentes. A causa das piscadas anteriores não foi determinada.

## Protocolo `--confirm` antes da transferência — validação física 06/11/2026

- O fluxo foi alterado para `GET /ota/challenge` → `POST /ota/prepare` com
  corpo vazio e HMAC dos metadados → confirmação no Tab5 → `POST /ota` com o
  binário. O cliente aguarda `approved_upload` antes de abrir/transmitir o `.bin`.
- O modal mostra versão declarada, tamanho e prefixo SHA-256. A aprovação fica
  vinculada ao nonce, hash, tamanho e versão por até 120 s; a janela de envio
  também expira em 120 s. Cancelar/expirar limpa a autorização sem selecionar
  slot novo. `--silent` continua no caminho de envio direto.
- Após receber o arquivo, o firmware valida SHA-256 e compara projeto e versão
  embutidos com os metadados autenticados antes de agendar a ativação.
- Teste executado com build ESP-IDF 6.1, binário de 2.625.504 bytes, SHA-256
  `2d6ca82299c041f2787ba5ee496415eb36c62078b9c32c7cd60ffedb7ac04a34`, versão
  1.10.0. O firmware foi instalado por USB em `factory` (`0x20000`) para habilitar
  o novo endpoint; `idf.py flash monitor` registrou boot normal, Wi-Fi/IP,
  display inicializado e checkpoint concluído, sem panic.
- Antes do teste, `screen.status` confirmou tela ativa e `screen timeout 0` foi
  aplicado. O cliente exibiu `No image bytes sent yet`; o usuário viu o modal
  com metadados e confirmou fisicamente. Só então o cliente reportou
  `Confirmation accepted; sending firmware bytes now.` e transferiu a imagem.
- Pós-OTA: cliente reportou `Device booted firmware 1.10.0`; `/ota/status`
  mostrou transição factory `0x20000` → `ota_0` `0x420000`, `running_valid=true`.
  O monitor registrou `rst:0xc (SW_CPU_RESET)` esperado pela ativação OTA,
  carregamento do app em `0x420000`, inicialização do painel e checkpoint normal;
  sem panic, watchdog ou brownout. `sys.info`: uptime 1m07s, `safe_mode=false`,
  `interrupted_boots=0`. O timeout original foi restaurado com `screen timeout 1`.
- **Sintoma visual reproduzido:** com timeout desabilitado, o usuário observou a
  tela alternando rapidamente entre o progresso e o azul-claro durante o envio;
  conseguiu acompanhar o processo. Portanto, a proteção de inatividade não
  explica a piscada. A atualização concluiu corretamente, mas a causa visual
  continua indeterminada. Não repetir OTA até análise específica deste novo
  comportamento e autorização.

### Hipótese de latência DSI/PSRAM durante erase OTA — 06/11/2026

- Evidência no ESP-IDF local: `components/esp_lcd/dsi/esp_lcd_panel_dpi.c`
  documenta que `MIPI_DSI_BRG_LL_EVENT_UNDERRUN` pode deixar a tela azul quando
  o DSI não consegue buscar dados da memória externa com banda suficiente. O
  BSP aloca o framebuffer DPI (1.843.200 bytes, 720×1280 RGB565) em PSRAM e
  aloca também os buffers LVGL na PSRAM neste ESP32-P4.
- A captura serial do ensaio **não** contém a mensagem `can't fetch data from
  external memory fast enough, underrun happens`; portanto, a aparência azul é
  compatível com DSI underrun, mas esse evento não foi confirmado nos logs.
- `CONFIG_SPI_FLASH_AUTO_SUSPEND` está desabilitado. O handler anterior chamava
  `esp_ota_begin(partition, package.size, ...)`, que no IDF apaga, de forma
  síncrona, toda a região alinhada da imagem antes de ler o primeiro bloco do
  corpo HTTP. As operações de flash podem afetar latência/banda do caminho de
  memória enquanto o DSI faz scanout contínuo da PSRAM; essa interação é a
  hipótese principal, não causalidade provada. O código OTA não altera
  backlight, LDO, reset GPIO nem chama API do painel.
- Mitigação OTA `OTA_WITH_SEQUENTIAL_WRITES` testada fisicamente uma vez após
  autorização: o usuário confirmou que a alternância azul/progresso persistiu;
  logo, erase sequencial não resolveu. `ota_1` ficou válido, `safe_mode=false`,
  `interrupted_boots=0`, uptime contínuo, sem panic e sem log DSI de underrun.
- Investigação do DW-GDMA: `esp_hal_dma/esp32p4/include/hal/dw_gdma_ll.h`
  define master port 0 como acesso ao DSI bridge e master port 1 como acesso a
  L2/ROM/MSPI Flash/PSRAM. O `esp_lcd_panel_dpi.c` configura a fonte do DMA como
  memória; portanto o scanout lê framebuffer pela porta 1. O AXI ICM permite
  QoS de leitura separado por master em `hal/axi_icm_ll.h` (campo de 4 bits).
- Mitigação local corrente: BSP ajusta somente a prioridade de leitura do DW-
  GDMA memory master para 15 (máximo de campo), mantendo write priority 0;
  clocks DSI, backlight, LDO, GPIO reset e timing do painel não mudam. Firmware
  passa a 1.11.0 conforme regra de versão. Build/teste estrutural validam
  compilação e wiring, não efeito elétrico/visual.
- Validação física autorizada: imagem 1.11.0, 2.625.536 bytes, SHA-256
  `b44fb71ee05769d7de09ceed0070ff8eae2e87ddf731e6f8f1eee6ef7a98e708`;
  update confirmado fisicamente e ativado `ota_1`→`ota_0` (0x420000),
  `running_valid=true`. Usuário confirmou que a tela **continuou piscando**
  azul/progresso: elevar QoS apenas do memory master do DW-GDMA também não
  resolveu. Monitor serial registrou reset SW_CPU_RESET do fluxo OTA, boot
  1.11.0, Wi-Fi/IP, checkpoint `ESP_OK`, sem panic e sem mensagem de DSI
  underrun; `sys.info` mostrou uptime contínuo pós-boot, `safe_mode=false`,
  `interrupted_boots=0`. Timeout voltou a 1 minuto. Não repetir OTA sem nova
  autorização; a causa visual permanece não diagnosticada.

### Mitigação da ISR DSI cache-safe — 06/11/2026

- O caso externo `espressif/arduino-esp32#13026` reproduziu flash write/erase
  fazendo MIPI-DSI piscar azul/preto no ESP32-P4 e confirmou em hardware que
  `CONFIG_LCD_DSI_ISR_CACHE_SAFE=y` evita a falha. É evidência para a hipótese,
  não prova para este Tab5.
- Candidato 1.12.0 habilita ISR DSI/DW-GDMA cache-safe. O callback do overlay
  LVGL fica em IRAM e usa contexto interno; na ISR apenas sinaliza semáforo,
  enquanto o chamador espera a cópia DMA2D e chama `lv_disp_flush_ready` fora
  da ISR. Falha de registro libera o display, em vez de deixá-lo pendurado.
- Antes do ensaio OTA: exigir teste estrutural, suíte host, build, boot no
  dispositivo com display funcional e `sys.info`/Wi-Fi válidos. No ensaio:
  timeout 0, monitor serial e aprovação física; comparar piscada azul/progresso
  e registrar slot, checkpoint e timeout restaurado. Não fazer upload se o
  novo firmware não inicializar o display de forma segura.
- Testes focados `test_ble_integration_contract`, `test_ota_contract` e
  `test_ota_script` passaram. O agregado `make ... test` parou em
  `test_time_sync_bin` (asserção `fake::task_deletions == 2`); repetição isolada,
  inclusive após recompilar objetos, também falhou na mesma contagem em linhas
  distintas. É um gate não relacionado ao DSI e permanece aberto, não marcar
  a suíte completa como aprovada.

#### Ensaio físico da versão 1.12.0

- Imagem de 2.626.944 bytes, SHA-256
  `7396c254cb175df65dc1a6da036e38ded71305b54de7052e6538469f37b2b5ce`.
  `idf.py build` passou; `build/config/sdkconfig.h` confirmou
  `LCD_DSI_ISR_CACHE_SAFE`, `DW_GDMA_ISR_IRAM_SAFE` e `DW_GDMA_OBJ_DRAM_SAFE`;
  `nm` localizou os callbacks DSI na IRAM. Antes do teste, `/ota/status` mostrou
  1.11.0 válida em `ota_0` (`0x420000`), `sys.info` sem safe mode nem boots
  interrompidos; a tela foi acordada e `screen timeout 0` confirmado em
  `term.dump`.
- Uma única execução de `python3 -u tools/update_firmware.py --host
  192.168.68.111 --firmware build/cyberdeck5.bin --confirm`: cliente reportou
  preparo sem bytes, aceitação física no Tab5, envio, estado `activating` e
  `Device booted firmware 1.12.0`. **O usuário confirmou que a tela voltou a
  piscar azul/ciano durante o envio.** A opção cache-safe não eliminou o
  sintoma neste dispositivo; não repetir OTA sem nova investigação/autorização.
- Pós-boot: `/ota/status` mostrou `idle`, 1.12.0 em `ota_1` (`0x820000`),
  `running_valid=true`. Monitor Serial-JTAG registrou um `SW_CPU_RESET` após
  ativação, inicialização do display, obtenção do IP e checkpoint `ESP_OK`,
  sem panic/watchdog/brownout ou mensagem de underrun capturada. O monitor
  inicialmente provocou também um reset USB ao abrir a porta; não atribuir
  esse reset ao OTA. `sys.info` confirmou uptime 58 s, `safe_mode=false`,
  `interrupted_boots=0`; `term.dump` confirmou restauração de
  `screen timeout 1` (`screen timeout set to 1 minutes`). A primeira consulta
  serial após encerrar o monitor sofreu disputa/desconexão de porta; `ping` e
  consultas subsequentes responderam normalmente.

#### Investigação estática após o ensaio (sem novo upload)

- A opção cache-safe mantém as ISRs DSI e DW-GDMA executáveis durante flash,
  mas **não reserva largura de banda da PSRAM** para o scanout. O painel ST7121
  usa framebuffer único RGB565 de 720 × 1280 em PSRAM, clock DPI de 70 MHz e
  timings 802 × 1524: cerca de 57,3 quadros/s e 105,6 MB/s de leitura média.
  A cópia de áreas redesenhadas pelo LVGL usa DMA2D para esse mesmo framebuffer.
- `process_ota_confirmation()` é chamado a cada 250 ms e executa
  `lv_label_set_text()` em todo tick durante o upload, mesmo se o texto exibido
  não mudou. No LVGL 9.6 local, `set_text_internal()` realoca/copia e marca o
  label para redesenho sem comparar conteúdo; isso pode amplificar a escrita
  concorrente ao scanout. `screen_off_turn_on()` não altera brilho quando a
  tela já está ligada, portanto não é uma explicação direta para a piscada com
  timeout 0. O driver habilita interrupção de underrun e registraria erro,
  mas nenhum apareceu no monitor; **não há contagem independente do evento**.
- [rokudev/lt-sdk#22](https://github.com/rokudev/lt-sdk/pull/22) documenta
  flicker ciano em outro painel MIPI-DSI/ESP32-P4 devido a escritas concorrentes
  no framebuffer em PSRAM. Ali a prioridade DMA máxima não ajudou, e reduzir
  a área escrita reduziu underruns medidos. Hardware/driver distintos: hipótese
  plausível para este Tab5, **não diagnóstico confirmado**. O vídeo anterior
  mostra tela ciano contínua por segundos e depois escura; não fornece
  sincronização com escrita flash, callbacks nem o teste 1.12.0.
- Próximo experimento, se aprovado: coletar contagem de underruns do bridge e
  duração de cópias DMA2D, com timestamps junto aos eventos de gravação flash e
  atualização do modal; comparar execução com texto de progresso inalterado
  sem esconder ou afrouxar o fluxo de aprovação. Não usar uma nova OTA apenas
  para inferir causalidade pela aparência; exigir instrumento e autorização
  antes de outro ensaio físico.

#### Preparação da instrumentação (histórico anterior ao ensaio físico)

- Candidato 1.12.1 registra durante a transferência o tempo de cada
  `esp_ota_write` (sem incluir recepção/hash), bytes tentados/falhas, tempos e
  área dos flushes DSI parciais até conclusão da cópia, além das atualizações
  do modal. Dados agregados em até 96 intervalos de 1 s em heap bounded;
  nenhum log por chunk. Ao terminar a gravação, emite linhas `ota_diag` no
  monitor serial antes de ativar o novo slot. `sample_second` atribui o delta
  de flush/UI ao instante da amostragem após cada escrita, **não** ao início
  exato da cópia. O último intervalo agrega excedentes se o ensaio superar
  96 s, sinalizando `overflow=1`.
- Não há contador de underrun acessível pela API do IDF: o driver consome e
  limpa o bit na ISR. `underruns=not_counted` não significa zero; somente a
  mensagem de erro emitida pelo próprio driver pode ser correlacionada, com
  resolução limitada. Não modificar o ESP-IDF instalado, nem alegar contagem
  exata por sondagem periódica do bit.
- Neste ponto do histórico ainda não havia flash/OTA com a instrumentação;
  o firmware em execução era 1.12.0. O ensaio autorizado posteriormente está
  registrado na seção seguinte.
- Build ESP-IDF 6.1 da candidata 1.12.1 passou (binário `0x281df0`, 37% livre;
  SHA-256 `d2a8a67285cc6f75d753b8005b29ffde17aa82d28ecfe5718b2bfde0b2f35bd1`);
  testes focados OTA/contrato DSI e dashboard passaram. Naquele momento, a
  validação no Tab5 e a coleta real dos contadores ainda estavam pendentes.
- O contrato host de help foi adaptado à versão própria do manifesto OTA:
  `test_shell_help_contract` e `test_shell_help` passaram, preservando a
  comparação da versão efetiva com o fixture e o formato semver.
- `make -C tests/host/keymap test` completou integralmente sem falhas após
  o ajuste do manifesto; `make -C tests/host/keymap verify` e
  `git diff --check` também passaram. A falha anterior de `test_time_sync_bin`
  não reapareceu nesta execução; não atribuir causalidade à instrumentação.
- O usuário escolheu **somente código** nessa etapa, antes de autorizar
  explicitamente o flash USB e a OTA descritos abaixo.

#### Ensaio físico autorizado posteriormente — 1.12.1

- O usuário autorizou explicitamente flash USB e uma OTA confirmada fisicamente.
  Imagem 1.12.1: 2.629.104 bytes, SHA-256
  `d2a8a67285cc6f75d753b8005b29ffde17aa82d28ecfe5718b2bfde0b2f35bd1`.
  `idf.py -p /dev/ttyACM0 flash` gravou factory, tabela, otadata e bootloader,
  verificou o hash e reiniciou. `sys.info` mostrou 1.12.1 sem safe mode ou boots
  interrompidos; `/ota/status` mostrou factory (`0x20000`),
  `running_valid=true`; `wifi.status` obteve IP, `ui.dump` exibiu header/TUI.
- `screen timeout 0` foi confirmado por `term.dump` e pelo boot no monitor,
  que reiniciou a placa ao abrir a USB (reset `CHIP_USB_UART_RESET` **anterior
  à OTA**). O usuário aprovou fisicamente no Tab5 **uma** OTA `--confirm` da
  mesma imagem 1.12.1. O cliente reportou `activating` e `Device booted firmware
  1.12.1`. O monitor registrou só o `SW_CPU_RESET` da ativação seguido de
  boot com IP/checkpoint normais; `/ota/status` confirmou `ota_0` (`0x420000`),
  `running_valid=true`. `sys.info` após o ensaio: uptime 3m48,
  `safe_mode=false`, `interrupted_boots=0`. Timeout original restaurado por
  `screen timeout 1`, confirmado em `term.dump`.
- **Visual:** o usuário viu o progresso passar de 30% sem piscar; depois viu
  tela azul/ciano **antes** do reboot. Logo, OTA funcional e falha visual ainda
  presente; o percentual visível não permite inferir em que byte a tela
  parou de mostrar os quadros. Nenhuma mensagem de underrun foi capturada;
  `underruns=not_counted` não autoriza afirmar zero eventos.
- Telemetria `ota_diag`: duração 19.504 ms, `overflow=0`, 777 chamadas
  `esp_ota_write`, 2.629.104 bytes tentados (exatamente o arquivo), zero
  falhas, 17.380.707 µs acumulados em escrita, maior chamada 34.630 µs;
  96 flushes DSI (2.054.682 µs acumulados, maior 84.121 µs), 513.536 pixels
  solicitados e 27 atualizações do modal. Entre os buckets 6..19, depois
  de aproximadamente 31% dos bytes, ainda houve 69 flushes e 19 atualizações
  do modal. A piscada não tem marca temporal sincronizada: não é possível
  afirmar que esses eventos ocorreram **depois** do azul. Bucket de 1 s atribui
  os deltas ao fim da escrita: não é linha do tempo exata do evento visual,
  nem prova de que pixels foram recebidos pelo painel. Evidência filtrada
  local (sem SSID/chave):
  `/tmp/opencode/ota-diag-1.12.1-20261106.log` (0600).
- Conclusão limitada: UI/flush do software prosseguiram durante o upload,
  inclusive depois do primeiro terço dos bytes, mas não se sabe se a ponte DSI
  deixou de fornecer quadros no instante da piscada, se
  ocorreu underrun sem registro ou se o painel perdeu sinal/alimentação.
  **Não repetir OTA** sem nova hipótese, instrumento confiável e autorização.

#### Instrumento e ensaio físico 1.12.2

- O IDF 6.1 lê e **limpa** `MIPI_DSI_BRG_LL_EVENT_UNDERRUN` dentro de
  `mipi_dsi_bridge_isr_handler` (`esp_lcd/dsi/esp_lcd_panel_dpi.c`). Uma leitura
  posterior do registrador não mede quantos eventos ocorreram. O overlay local
  `components/esp_lcd` conta o bit na própria ISR, antes de limpá-lo, com
  contador atômico lock-free em memória interna e custo constante, sem printf
  nem alocação na ISR. O CMake compara SHA-256 do driver, `Kconfig` e wiring
  originais para impedir uma versão de IDF não revisada; o IDF instalado não é
  modificado. A primeira reconfiguração sem `Kconfig` local removeu o gate de
  ISR cache-safe; a imagem desse build foi **descartada sem flash**. O overlay
  agora contém `Kconfig` idêntico ao original. Verificar
  ligação efetiva no build e segurança com cache desabilitado antes de gravar.
  O callback de conclusão DMA também incrementa contador independente; OTA
  agrega deltas fora da ISR e imprime apenas depois do upload. Isso mede o
  evento de underrun da ponte, **não** pixels recebidos pelo
  painel; zero só é conclusivo para esse bit se a interrupção estava habilitada
  durante todo o período amostrado.
- Para separar falha do framebuffer de falha do scanout/painel, correlacionar
  os contadores com vídeo da tela. Mesmo DMA ativo não comprova recepção de
  pixels pelo painel; se necessário, observar enlace DSI/alimentação com
  instrumento externo. A autorização de 06/11/2026 cobriu este flash e uma OTA.
- Firmware 1.12.2 compilado (imagem 0x281e80, 37% livre, SHA-256
  `33e6a3b8295498911bc90c18847f1d50efc2f58cf0fcd95e43b31245156ca593`).
  Suíte host completa, `verify`, contratos OTA/help e `git diff --check`
  passaram. `sdkconfig` efetivo contém
  `CONFIG_LCD_DSI_ISR_CACHE_SAFE=y`, `CONFIG_DW_GDMA_ISR_IRAM_SAFE=y` e
  `CONFIG_LCD_DSI_OBJ_FORCE_INTERNAL=y`; `build.ninja` compila o arquivo DPI
  local. `nm` confirmou ISR e callback DMA em RAM e contadores internos.
- Flash USB gravou a imagem e verificou hash. Boot em factory (`0x20000`),
  `/ota/status running_valid=true`, Wi‑Fi/IP, screen on e `ui.dump` íntegro;
  `sys.info` sem safe mode/boots interrompidos. Uma OTA confirmada fisicamente
  reinstalou a mesma imagem em `ota_0` (`0x420000`), com resposta `activating`,
  reboot `SW_CPU_RESET` esperado e checkpoint `ESP_OK`; status final idle,
  `running_valid=true`. Sem reenvio.
- Telemetria, 20 buckets em 19.749 ms: 2.629.248 bytes tentados em 863 writes,
  zero falhas; 1.130 callbacks de conclusão DMA, zero underruns da ponte;
  106 flushes (2.033.395 µs agregados; máximo 78.176 µs), 561.498 pixels
  solicitados, 29 updates do modal (237.481 µs agregados). Bucket final contém
  eventos após o fim de `esp_ota_write` e não deve ser tratado como período de
  escrita. Underrun IRQ permaneceu habilitado e o contador foi lido antes do
  clear na ISR durante a instrumentação.
- Visual relatado pelo usuário: **sem piscada durante o upload**; tela azul
  apenas no reboot. Vídeo contínuo: `/home/moises/Vídeos/20261106_164400.mp4`
  (vídeo anterior: `/home/moises/Vídeos/20261106_135831.mp4`). Portanto, o
  sintoma de upload não se reproduziu nesta execução e não houve underrun da
  ponte medido. Isso não prova que a causa histórica está corrigida nem que o
  painel recebeu cada pixel: as contagens DMA/flush não são medição elétrica
  do enlace/painel. Inspeção de contato a 1 fps do vídeo concorda: progresso
  permanece visível, seguido por quadro azul/branco no reboot, sem azul durante
  a transferência. Não repetir OTA sem hipótese nova e autorização.
- Monitor serial encerrado; `sys.info` confirmou uptime contínuo 4m23,
  `safe_mode=false`, `interrupted_boots=0`. Timeout restaurado para 1 minuto e
  confirmado por `term.dump`.

#### Repetição autorizada da OTA 1.12.2

- Usuário pediu uma repetição para verificar reprodutibilidade. Estado prévio:
  1.12.2 em `ota_0`, `running_valid=true`, OTA idle, Wi‑Fi/IP ativo, uptime
  contínuo e `safe_mode=false`. Imagem idêntica (2.629.248 bytes,
  SHA-256 `33e6a3b8295498911bc90c18847f1d50efc2f58cf0fcd95e43b31245156ca593`).
  A abertura do monitor provocou reset USB esperado; boot/IP/checkpoint
  concluíram antes de preparar a OTA. Timeout permaneceu em 1 min.
- Usuário confirmou fisicamente o modal. O cliente reportou `activating` e
  `Device booted firmware 1.12.2`; `/ota/status` final: `idle`, slot `ota_0`
  (`0x820000`/`8519680`), `running_valid=true`. Um `SW_CPU_RESET` de ativação,
  boot/checkpoint ESP_OK. Pós-teste: uptime contínuo, Wi‑Fi/IP, tela ligada,
  `safe_mode=false`, `interrupted_boots=0`. Sem reenvio.
- Telemetria da segunda execução (19 buckets, 18.034 ms): 2.629.248 bytes,
  837 chamadas `esp_ota_write`, zero falhas; 1.032 conclusões DMA, zero
  underruns bridge; 83 flushes (1.599.074 µs, máximo 103.842 µs), 425.958
  pixels solicitados, 22 updates UI (247.105 µs). A IRQ de underrun permaneceu
  habilitada e contador capturava antes do clear na ISR.
- Relato visual do usuário: **sem piscada durante o upload; azul somente no
  reboot**. Sem vídeo nesta repetição. Isso reproduz o resultado positivo da
  primeira 1.12.2: o sintoma durante OTA não reapareceu nas duas execuções e
  ambas marcaram zero underruns. Evidência ainda não demonstra que a correção
  causal esteja identificada nem exclui falha intermitente/painel/enlace.
  Log serial filtrado (0600):
  `/tmp/opencode/ota-diag-1.12.2-repeat-20261106.log`. Timeout 1 min mantido;
  não fazer nova OTA sem outra hipótese e autorização.

## Aparência do modal — remoção da faixa inferior (1.13.0)

- Defeito visual: o painel de confirmação/progresso desenhava uma faixa clara
  na borda inferior. Causa: o tema LVGL default roda em modo **light**
  (`CONFIG_LV_THEME_DEFAULT_DARK` não setado), então todo `lv_btn` herda raio
  arredondado e sombra cinza (`shadow_width` 3, `shadow_opa` 50%,
  `shadow_offset_y` +3). Sobre a superfície escura do painel, essa sombra
  aparece como uma borda clara na base.
- Correção: `flatten_btn_chrome()` em `cyberdeck_ui.cpp` zera raio, borda,
  outline e sombra; é aplicado ao painel e aos botões Confirmar/Cancelar. A
  linha `lv_obj_set_style_border_color(s_ota_modal, WHITE, 0)` foi removida por
  ser estilo morto (sem `border_width`).
- Verificação automatizada: `make -C tests/host/keymap test_ota_contract`
  (inclui controle negativo por mutação do helper e da chamada).
- Validação física em 06/11/2026 com firmware **1.13.0** (flash USB em
  `factory`, `running_valid=true`, Wi‑Fi/IP): `POST /ota/prepare` abriu o modal
  apenas com metadados, o usuário confirmou a tela correta e clicou em
  **Cancelar**; `/ota/status` voltou a `idle` com `received=0` (nenhum byte de
  imagem enviado), uptime contínuo e sem reboot. Captura da tela com o modal
  aberto em `/tmp/opencode/ota-modal2.bmp` (artefato local): sem faixa clara na
  borda inferior do painel. O painel tem a mesma cor do terminal (#0A0A0A), então
  a prova é a ausência da sombra, não uma borda escura visível.
- Observação resolvida em seguida: os botões Confirmar/Cancelar herdavam
  `bg_color_primary` do tema light e apareciam em azul. Passaram a usar
  `BUTTON` (#303030), o mesmo cinza elevado dos controles do player, mantendo
  texto branco herdado do painel. `test_ota_contract.py` fixa a constante e a
  chamada de `bg_color` no botão.
- Atualização por OTA com essa imagem (SHA-256
  `b75cb40ad744b015e1faf2811025e61d447c7a82ef0435b3a989e5c0e32c4804`,
  2.629.376 bytes): envio `--silent` ativou `ota_0` (`4325376`) e uma
  confirmação física posterior ativou `ota_1` (`8519680`); uma terceira
  confirmação física voltou a `ota_0`. Todas com `running_version=1.13.0`,
  `running_valid=true`, `received=0` no estado final (upload concluído antes),
  `safe_mode=false` e `interrupted_boots=0`. Nenhuma piscada foi relatada nessas
  transferências.
