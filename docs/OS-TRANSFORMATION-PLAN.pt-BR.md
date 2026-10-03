# Plano de Transformacao em Sistema Operacional Embarcado

Este documento registra a evolucao do cyberdeck5 para um modelo mais proximo de
um sistema operacional embarcado leve, inspirado em conceitos do Tactility,
sem tentar reproduzir Linux e sem introduzir execucao dinamica antes de haver
limites de seguranca e lifecycle bem definidos.

## Status Atual

- [x] Separar `src/apps/` de `src/platform/`.
- [x] Organizar shell, Wi-Fi, SSH, Bluetooth, Serial-JTAG e Screenshot como apps.
- [x] Criar runtime compilado com `app list`, `app info`, `app start` e `app stop`.
- [x] Criar manifestos bounded para as aplicacoes.
- [x] Criar supervisor central de registro e startup.
- [x] Manter o shell como aplicacao inicial.
- [x] Manter filas, tokens, workers e snapshots bounded.
- [x] Separar a UI dos modelos e servicos de produto.
- [x] Validar a arquitetura com testes host, contratos estruturais e build ESP-IDF.
- [x] Implementar validacao fisica dos fluxos de lifecycle das aplicacoes.
- [x] Tornar o shell uma aplicacao de primeiro plano com console sob o supervisor.

Execucao mais recente da Fase 1: o supervisor resolve dependencias declaradas, mantem
estados de lifecycle, mede o tempo dos hooks, registra a ultima falha, expoe
recursos declarados e suporta restart/stop-all.

Validacao fisica em `/dev/ttyACM0` (ESP32-P4, fw `fa1c976-dirty`, IDF 5.5.5):
`ping`, `sys.info` e `app list` responderam; o log de boot nao continha nenhum
`system app failed`; `app start cyberdeck.screenshot` foi aceito; uptime continuo
(00:00:29 -> 00:04:21) sem reboot, panic ou queda de heap.

Regressao encontrada e corrigida nessa validacao: a primeira versao do supervisor
tratava o estouro do orcamento de lifecycle como falha de estado. O
`wifi_mgr_start()` leva ~2,7 s para subir o radio C6 e o storage SD, o que
excedia o orcamento de 1 s e marcava `cyberdeck.wifi` como `failed`, bloqueando
por dependencia `cyberdeck.ssh` e `cyberdeck.screenshot`. Como os hooks sao
sincronos e nao podem ser preemptados, o estado agora segue o retorno real do
hook e o estouro do orcamento fica apenas como diagnostico pos-retorno; o orcamento do Wi-Fi
foi elevado para 8 s. Regressao coberta por `test_app_runtime.cpp` (`slow_app` e
`slow_dependent_app`) no hook de start e por
`failing_app`/`failing_dependent_app`, que confirmam que falha real continua
fail-closed. A cobertura comportamental do stop permanece encaminhada ao gate
de testes da Etapa 5.

## Fases Pendentes

### 1. Supervisor de Sistema

- [x] Adicionar estados `registered`, `starting`, `running`, `stopping` e `failed`.
- [x] Declarar dependencias entre aplicacoes.
- [x] Tornar a ordem de inicializacao declarativa.
- [x] Implementar restart controlado, timeout e diagnostico por aplicacao.
- [x] Registrar os recursos possuidos por cada app.

### 2. Contrato de Aplicacao

- [x] Expandir `manifest` com versao da API, tipo, dependencias e capacidades.
- [x] Declarar stack e fila requisitadas.
- [x] Adicionar motivo da ultima falha.
- [x] Definir hooks completos de inicializacao e teardown.
- [x] Declarar comandos expostos ao shell.

Execucao da Fase 2: o manifesto agora possui `api_version`, `app_type`,
capacidades, `stack_bytes`, `queue_depth` e uma lista bounded de comandos,
mantendo o comando legado como entrada primaria. O contrato de aplicacao possui
hooks `init()` e `teardown()` com defaults compativeis; `app info` expoe esses
metadados e o ultimo diagnostico de lifecycle. Os system apps declaram tipo,
capacidades, stack e fila requisitadas.

Responsabilidade de logging: `cyberdeck.event_log` agora e um system app
registrado e iniciado pelo supervisor. O `app_main` apenas registra os system
apps e solicita `cyberdeck_system_apps_start_logging()` depois de validar o
cartao SD; ele nao chama mais `event_log_init()` diretamente. O runtime injeta
uma porta `cyberdeck_apps::logger` em cada aplicacao registrada, implementada
no firmware pelo adaptador do supervisor para `event_log_write()`. O evento log
continua bounded e persistente, mas seu lifecycle e ownership pertencem ao
supervisor.

### 3. Separacao de Servicos

- [x] Remover acessos diretos da UI aos servicos.
- [x] Expor Wi-Fi, SSH, BLE e Serial por interfaces de servico.
- [x] Fazer Screenshot consumir uma interface de display.
- [x] Fazer o shell acessar storage, rede e input por portas do sistema.
- [x] Converter callbacks externos em eventos bounded.

Execucao inicial da Fase 3: o acesso direto da UI ao backend `event_log` foi
removido. O logger agora e uma porta do runtime, implementada pelo supervisor
e consumida pela UI atraves de `runtime::app_logger()`. Os acessos diretos da
UI aos servicos Wi-Fi, SSH, BLE e Serial tambem foram removidos: o gateway
`cyberdeck_service_ports` concentra as chamadas aos backends e a UI consome
somente as portas. O shell session ja expunha storage, rede e input por seu
`host`. Os callbacks de Wi-Fi e BLE ja usavam filas bounded; os callbacks SSH
agora tambem publicam eventos limitados em uma fila de 8 entradas e somente o
timer LVGL executa filtro, compositor, render e logging.

Regressao encontrada na validacao fisica desta entrega: a primeira versao do
evento SSH carregava 1024 bytes de payload inline e o timer LVGL reclamava
copias por valor, o que estourava a stack da task e travava o boot logo apos
`wifi_mgr_start()` (`app_main` nunca retornava, sem panic). A causa foi provada
por reversao: sem a fila o boot completava normalmente. A correcao reduziu o
elemento da fila para 332 bytes e passou a reclamar eventos num slot estatico
compartilhado, sem materializar o evento na stack do timer. Payloads acima de
256 bytes por callback continuam truncados de forma fail-safe.

O Screenshot foi entao migrado para `screenshot_frame_t`: o servidor HTTP nao
inclui mais LVGL/BSP nem captura `lv_screen_active()`; recebe um frame RGB565
bounded pela porta `cyberdeck_display_port`, que concentra lock, snapshot e
liberacao do buffer no adapter de display.

### 4. IPC e Event Bus

- [x] Criar eventos tipados e bounded.
- [x] Criar filas por aplicacao.
- [x] Preservar tokens de geracao e descarte de eventos stale.
- [x] Definir backpressure, overflow e timeout explicitamente.
- [x] Proibir chamadas diretas entre tasks quando houver IPC aplicavel.

Recorte aprovado da Etapa 4: o cliente SSH publica uma geração monotônica
`uint64_t` em todos os callbacks; a UI captura a geração aceita e descarta
eventos stale no pump. Callbacks SSH apenas publicam snapshots/eventos e são
proibidos de adquirir `bsp_display_lock` ou tocar LVGL/display diretamente.
O teardown invalida a geração esperada antes de destruir a fila. O teardown da UI chama a porta
`ssh_disconnect_and_wait()` antes de destruir a fila de eventos SSH. A espera e
bounded e usa `vTaskDelay`, observando o retorno nulo de `s_task_handle`. Dados
SSH descartados por fila cheia sao contados em um contador saturante; eventos de
estado tentam reservar espaco removendo um evento de dados antigo. O payload
continua limitado e nao ha arrays grandes na stack. O event bus geral permanece
pendente. Neste recorte, a task SSH nao chama mais o backend Wi-Fi: os eventos
tipados `SESSION_CONNECTING`, `SESSION_ONLINE` e `SESSION_SOCKET_ERROR` entram
em uma fila FIFO bounded de quatro itens, com descarte nao bloqueante contado
quando cheia, e sao consumidos pela `net_worker` sob `net_lock`. A fila e
criada transacionalmente antes do uso e destruida no rollback/teardown; nao ha
bus global.

### 5. Modelo de Tasks

- [x] Definir uma task principal por servico bloqueante.
- [x] Declarar stack no manifesto.
- [x] Associar fila e mutex ao lifecycle da aplicacao.
- [x] Tornar start e stop idempotentes.
- [x] Exigir join antes de destruir recursos.
- [x] Integrar watchdog e limites de execucao.
- [x] Manter operacoes pesadas fora da task LVGL e de callbacks ESP-IDF.

O campo `lifecycle_timeout_ms` do runtime e um orcamento de observabilidade:
start e stop sao hooks sincronos, portanto o runtime so mede o tempo depois que
o hook retorna. Um hook que demora e retorna sucesso preserva o estado coerente
(`running` apos start ou `registered` apos stop) e recebe o diagnostico
`start hook timeout`/`stop hook timeout`; o runtime nao interrompe a execucao
nem bloqueia dependentes por esse motivo. Os limites efetivos ficam nos joins
cooperativos bounded dos servicos, que devem reter/quarentenar recursos quando
a task nao confirma quiescencia. Falha real do hook continua fail-closed.

O sdkconfig mantém o Task WDT global como política do firmware, com timeout de
5 s e panic desabilitado. Esta etapa não adiciona `esp_task_wdt` por task nem
feeds genéricos: os limites efetivos de lifecycle são os joins cooperativos
bounded, e timeout retém recursos em quarentena e bloqueia restart. Hooks
síncronos continuam com orçamento diagnóstico pós-retorno no runtime.

Finalização da Etapa 5: o app SSH agora usa `disconnect_and_wait()` no
stop, com espera bounded de 1000 ms, e seu manifesto declara a stack dinamica
de 24576 bytes e a fila de eventos bounded de 8 itens. O adaptador de
`service_application` torna somente os lifecycles de SSH e BLE idempotentes,
sem mudar os demais servicos. O BLE possui uma barreira de quiescencia
separada do ACK do comando STOP: a task publica a barreira somente depois da
ultima operacao sobre filas, mutexes e estado de lifecycle, marca seu handle
como encerrado e se auto-exclui. `ble_mgr_stop()` exige essa confirmacao antes
de parar o NimBLE e destruir filas, mutexes e semaforos; em timeout retorna
`ESP_ERR_TIMEOUT` e retém os recursos. O manifesto BLE declara a stack real do
host de 8192 bytes e fila de comandos de 8 itens. O Wi-Fi agora expõe
`wifi_mgr_stop(timeout_ms)`, com estados explícitos, invalidação de
gerações/tokens, cancelamento de timers e join cooperativo de
`wifi_event_worker` e `net_worker` antes de qualquer destruição. Timeout marca
quarentena, retém os recursos e bloqueia restart; não há `vTaskDelete` de
worker vivo. O system app para SSH e Screenshot antes do Wi-Fi pela árvore de
dependências existente e o stop do Wi-Fi é idempotente. Para Serial-JTAG, `bridge_start()`
aguarda o handshake da task `serial_brg` e `serial_stop()` solicita saída
cooperativa e faz join bounded de até 2000 ms. Timeout deixa o estado em
`stopping`, preserva o handle e impede restart concorrente; a task observa o
pedido no loop de leitura e no loop de chunks de `screen.dump`. O driver USB
Serial-JTAG, o writer global de `esp_log_set_vprintf` e os semáforos/mutexes
compartilhados permanecem vivos: este recorte não faz desmontagem de recursos
de boot. O manifesto declara stack de 8192 bytes e `queue_depth=0`, pois a
ponte não possui fila própria; a entrada é bounded pelo driver/assembler
existente, e o campo não é usado para inventar capacidade.

O Screenshot completa este recorte com a task `screenshot_ctl` (stack de 6144
bytes e fila bounded de 8 itens). O callback de estado Wi-Fi somente copia e
publica snapshots; a task serializa `httpd_start()`/`httpd_stop()`. O start
aguarda handshake de prontidão e o stop remove o listener, sinaliza a task e
aguarda a barreira de quiescência do mutex de requisição antes de retornar.
Timeout não executa `vTaskDelete` forçado e coloca task, fila, mutex e HTTPD em
quarentena. O listener Wi-Fi agora possui unregister sincronizado, que não
retorna enquanto um callback estiver em execução.

### 6. Sistema de Arquivos Virtual

- [x] Primeiro recorte: catálogo compilado e resolver readonly bounded para
  `/apps`, `/data`, `/dev`, `/tmp` e `/system`, integrado ao shell local.
- [x] Segundo recorte: backends físicos readonly bounded para `/data` e
  `/system`, confinados a seus diretórios sob `host_root` e reutilizando as
  proteções de path/descritor do shell.
- [x] Terceiro recorte: interface virtual readonly `/dev/null`, sem abertura
  do `/dev` físico e sem entrega de handles.
- [x] Quarto recorte: mutações bounded (`touch`, `mkdir`, `rm`, `rmdir`) sob
  `/data`, com proteção da raiz e sem alterar `/system`.
- [x] Consolidar o namespace virtual do dispositivo com backends.
- [x] Manter `/data` para dados persistentes do usuario.
- [x] Manter `/system` para configuracoes e estado interno.
- [x] Expor dispositivos por interfaces virtuais em `/dev`.
- [x] Preservar confinamento, limites e protecao contra traversal e symlink.
- [x] Manter execucao de binarios do SD fora do escopo inicial.

O recorte atual mapeia `/data` e `/system` para seus diretórios sob
`host_root`; somente `/data` aceita as mutações bounded do shell. `/dev/null` é a única interface virtual exposta;
ela sempre retorna conteúdo vazio e não entrega handles. `/apps` não executa
conteúdo. O `/sdcard` físico continua sendo `host_root`, separado do catálogo.

Validação física do segundo recorte em `/dev/ttyACM0` (ESP32-P4, fw
`e3323db`, IDF 5.5.5): `ping` e `sys.info` responderam; `cd /data` e `ls`
exibiram o diretório persistente `com.tab5.notas`; `ls
/data/com.tab5.notas` exibiu `nota.txt`; `cd /apps/tools` foi rejeitado como
caminho inválido. O uptime permaneceu contínuo (`00:00:07` -> `00:00:25`),
sem reboot, panic ou queda relevante de heap.

Na validação do backend `/system`, o cartão físico não possuía o diretório
`/sdcard/system`: `cd /system` foi rejeitado e `touch /system/new.txt` não
criou arquivo. O fallback para a raiz virtual continuou funcional e o uptime
permaneceu contínuo (`00:00:07` -> `00:00:18`), sem reboot ou panic. O caminho
positivo de `cd`/`ls`/`cat` em `/system` permanece coberto pelo teste host;
ausência do backend físico é tratada como falha fechada.

Validação física da interface `/dev/null` em `/dev/ttyACM0` (fw
`b86dee6-dirty` durante a gravação): `ls /dev` e `ls /dev/null` exibiram
`null`; `cat /dev/null` retornou vazio; `cat /dev/tty` foi rejeitado como
namespace readonly. O uptime permaneceu contínuo (`00:00:04` -> `00:00:05`),
sem reboot ou panic.

Validação física das mutações `/data` em `/dev/ttyACM0` (fw `6c273c2`):
`touch /data/phase6.tmp` criou o arquivo, `ls /data` o exibiu, `rm` o removeu
e `rm -r /data` foi rejeitado pela proteção da raiz. O uptime permaneceu
contínuo (`00:00:04` -> `00:00:05`), sem reboot ou panic.

Finalização do recorte de consolidação: `cyberdeck_vfs_namespace` agora expõe
uma classificação única de backend (`metadata`, `filesystem`, `null_device` ou
`invalid`), consumida pelo shell para decidir leitura, mutação e interfaces
virtuais. O shell não duplica mais a regra de identificação por índice. O teste
host cobre a classificação dos cinco namespaces, a raiz, `/dev/null`, os
backends físicos e a política mutável exclusiva de `/data`; os testes existentes
mantêm cobertura de traversal, symlink, limites bounded e proteção das raízes.
Execução de binários do SD continua explicitamente fora do escopo.

Validação física do recorte consolidado em `/dev/ttyACM0` (2026-10-02, fw
`e5e29a9-dirty`, IDF 5.5.5): `pwd` iniciou em `/`; `ls /` exibiu `apps`,
`data`, `dev`, `tmp` e `system`; `/data` aceitou `touch`, `ls` e `rm` do
arquivo temporário; `/dev/null` foi acessado sem conteúdo; e `cd /sdcard` foi
rejeitado como caminho inválido. O `sys.info` passou de `00:00:04` para
`00:01:19`, com heap estável e sem reboot, panic ou erro da ponte.

### 7. Shell como Userland

- [x] Transformar o shell em uma aplicacao de primeiro plano completa.
- [x] Fazer o prompt e a linha de comando pertencerem ao app shell.
- [x] Fazer `app` consultar exclusivamente o supervisor.
- [x] Expor servicos por contratos de comando sem interceptar comandos legados.
- [x] Manter historico, edicao e sessoes isolados.
- [x] Tratar SSH como modo de sessao do shell.
- [x] Avaliar suporte futuro a multiplas sessoes ou consoles.

Execucao da Fase 7: `cyberdeck.shell` deixou de ser um `service_application`
com hooks stub (`start_shell` devolvia `true` e `stop_shell` devolvia `false`)
e passou a ser a aplicacao real `cyberdeck_shell_app::application`, registrada
no supervisor com `app_type::foreground` e dependencia de
`cyberdeck.event_log`. O supervisor e o dono do ciclo de vida do console: cada
`start` cria uma `session` nova, o que isola historico, edicao, tokens de
conexao Wi-Fi e buffer de passkey, e cada `stop` apaga o passkey antes de
liberar o console. `start` e `stop` sao idempotentes e o `start` falha fechado
sem host de composicao.

O prompt e a linha de comando migraram para o modulo puro
`cyberdeck_shell_console`. As helpers de UTF-8 e de ajuste de largura sairam do
TU anonimo da UI sem mudanca de comportamento, de modo que prompt, linha e
scrollback passam a ter uma unica autoria sobre o limite de 12288 bytes. A UI
nao constroi mais marker nem linha ajustada: `compose_console_line()` resolve
apenas quem possui a entrada (SSH conectado, senha pendente, ownership de
BLE/Wi-Fi e cwd) e `s_shell_app.compose_line(surface)` devolve marker, linha e
cursor ja clampado. As facades locais `sync_editor`, `sync_line` e
`execute_line` foram removidas da UI.

O `app stop` nao pode destruir o console. Como `stop_index` para dependentes
antes do alvo, `app stop cyberdeck.event_log` — dependencia declarada do shell —
destruiria o console sem nunca nomea-lo, e todas as teclas, inclusive as que
restaurariam o estado, seriam descartadas ate um reboot. O supervisor expoe
`stops_console_owner(id)`, que percorre a arvore de dependentes com marcadores
visitados (bounded contra ciclos), e recusa qualquer `app stop` cuja cascata
alcance o dono do console. A API de lifecycle do supervisor
(`stop_application`, `stop_all`, `restart_application`) permanece disponivel para
quem controla o boot, porque o bloqueio se aplica aos comandos que chegam pelo
proprio console.

O runtime so e consultado para os tokens que lhe pertencem. O dispatcher
classifica o primeiro token (limite de 256 bytes): `app` pertence
exclusivamente ao supervisor, mesmo malformado; um comando declarado por uma
aplicacao registrada e roteado ao supervisor; e um comando legado nunca e
interceptado, mesmo quando outra aplicacao declara o mesmo nome. A reserva de
comandos legados e derivada do catalogo unico de ajuda
(`cyberdeck_shell_help::kCatalog`), portanto nao pode divergir do `help`
exibido, e `collisions()` reporta em modo diagnostico os comandos declarados
que colidem com um legado sem nunca rotea-los. O shell local continua sendo o
primeiro dono das linhas e o parser legado permanece o unico dono de `wifi`,
`log`, `clear`, `screen`, `battery`, `bluetooth`, `ssh` e `help`.
`cyberdeck_shell_console.cpp` tambem absorveu as helpers de UTF-8 e de ajuste
de largura que viviam no TU anonimo da UI, e o budget de 12288 passou a ter uma
unica origem: a UI deriva `TERMINAL_LIMIT` de `k_terminal_limit` e valida a
igualdade com `static_assert`, de modo que scrollback, prompt e linha nao podem
divergir.

SSH passou a ser um modo de sessao do console do shell
(`session_mode::{menu,ssh_host_key,ssh_password,ssh_interactive}`), derivado de
`host_->ssh_phase()` em vez de estado duplicado. O cliente SSH continua sendo um
servico separado com seu proprio lifecycle; o shell possui apenas o modo.

Decisao sobre multiplas sessoes ou consoles: o recorte atual suporta um unico
console de primeiro plano, porque existe um unico terminal LVGL, uma unica
fila bounded de teclado e um unico `screen_off`. O isolamento exigido pela fase
e por instancia — cada aplicacao shell possui seu proprio console, e o host
teste prova que duas instancias nao compartilham linha nem estado. Suporte a
varios consoles simultaneos permanece fora de escopo e depende de uma superficie
de display com multiplas areas e de um roteador de input por console, que sao
trabalho das fases 8 e 9. O manifesto ganhou `owns_console`, e o supervisor
recusa `app stop cyberdeck.shell` porque o console e o unico caminho para
restaurar o proprio console; a API de lifecycle do supervisor continua disponivel
para quem controla o boot.

Validacao host da fase: `test_shell_app` (166 checks) cobre a matriz de prompt,
os helpers de UTF-8 e de modo com fallbacks, os limites bounded, a politica de
despacho incluindo registry cheio e runtime ausente, o ownership do console
(start/stop idempotentes, start sem host, fachada nula-segura, console novo a
cada start, duas instancias isoladas) e a propriedade pelo supervisor
(dependencia ausente mantem o shell `failed` sem console, auto-stop recusado,
`at()` bounded). `test_shell_app_contract.py` protege as fronteiras que o
binario host nao alcanca, incluindo a remocao do console da UI, a ordem das
decisoes no dispatcher e a interseccao do switch legado. O prompt e os helpers
passaram a ser compilados a partir do TU de producao em
`test_prompt_behavior.py`, em vez de copias de corpos.

### 8. Window Manager LVGL

- [x] Criar superficie principal por aplicacao.
- [x] Centralizar foco e ownership de input.
- [x] Criar barra de sistema persistente.
- [x] Adicionar notificacoes e transicoes entre apps.
- [x] Impedir acesso direto das aplicacoes a arvore LVGL.
- [x] Expor uma API controlada de view/contexto.

Execucao da Fase 8: o window manager mantem limites bounded de ate 8
superficies e 8 notificacoes FIFO. O adaptador e o unico owner da arvore LVGL
composta pela tela, barra de sistema persistente e area de conteudo; as
aplicacoes recebem somente uma API opaca de `view_context`, com geracao para
invalidar handles durante teardown. Foco e ownership de input ficam
centralizados, e notificacoes/transicoes entre apps passam pelo manager, com
descarte seguro de capacidades expiradas e payloads fora do limite. A fase nao
inclui multiplos consoles nem APIs de capabilities: esses itens permanecem
fora do escopo e sao trabalho da Fase 9.

### 9. Recursos e Capacidades

- [x] Definir APIs controladas para display, input, storage e network.
- [x] Definir APIs controladas para BLE, Serial-JTAG e Screenshot.
- [x] Definir APIs controladas para event log, clock e bateria.
- [x] Entregar a cada app somente os recursos declarados no manifesto.

Execucao da Fase 9: `manifest.resources` e a autoridade de autorizacao; nomes
desconhecidos, duplicados, ausentes ou acima do limite de 8 recusam o registro.
`capabilities` permanece metadado de diagnostico. O runtime entrega grants
opacos por aplicacao apenas durante `running`; cada operacao das facades tipadas
revalida o grant e a geracao, com revogacao antes do teardown. Display e input
reutilizam o `view_context` do window manager, sem expor a arvore LVGL. Os
limites de 16 apps, 8 recursos, 8 capabilities e 8 superficies/notificacoes
continuam bounded e nao existe loader dinamico.

### 10. Persistencia e Recuperacao

- [x] Persistir estado de aplicacoes quando necessario.
- [x] Criar recovery de boot apos falha.
- [x] Registrar ultimo erro por aplicacao.
- [x] Criar modo seguro contra loops de reinicializacao.
- [x] Estruturar logs de lifecycle.
- [x] Expor diagnostico por `sys.info` e `app info`.

Execucao da Fase 10: `cyberdeck_recovery_policy` concentra as transicoes puras
de boot pendente, checkpoint de prontidao, contador de tres boots interrompidos,
latch de safe mode e erros bounded por aplicacao. O adaptador NVS usa um blob
versionado e nunca apaga NVS corrompido ou indisponivel automaticamente. A
tentativa e persistida antes do startup; o commit ocorre somente depois de
display, input e apps estarem prontos. Safe mode permite apenas
`cyberdeck.event_log`, `cyberdeck.shell` e `cyberdeck.serial`; o latch sai por
`sys.safe_mode.clear` (acao explicita via Serial-JTAG) ou reset/reflash externo,
nao por boot normal. Erros restaurados sao exibidos pelo diagnostico existente
de `app info`, e `sys.info` expoe o estado de recovery e o contador de boots.
Os hooks do supervisor continuam gerando lifecycle logs pelo event log.

### 11. Distribuicao Futura

- [ ] Criar catalogo de aplicacoes compiladas.
- [ ] Definir recursos empacotados no firmware.
- [ ] Avaliar pacotes no SD somente para dados e assets.
- [ ] Manter loader dinamico ou ELF fora do escopo ate haver sandbox.
- [ ] Exigir permissoes, limites de memoria e validacao de assinatura antes de
      qualquer execucao dinamica.

## Estrutura-Alvo

```text
components/cyberdeck/
├── include/
│   ├── apps/
│   │   ├── bluetooth/
│   │   ├── demo/
│   │   ├── runtime/
│   │   ├── screenshot/
│   │   ├── serial/
│   │   ├── shell/
│   │   ├── ssh/
│   │   ├── system/
│   │   └── wifi/
│   └── platform/
└── src/
    ├── apps/
    │   ├── bluetooth/
    │   ├── demo/
    │   ├── runtime/
    │   ├── screenshot/
    │   ├── serial/
    │   ├── shell/
    │   ├── ssh/
    │   ├── system/
    │   └── wifi/
    └── platform/
```

## Nao Objetivos

- Transformar o firmware em Linux.
- Executar scripts ou ELF arbitrarios do SD.
- Permitir acesso direto das aplicacoes a LVGL.
- Introduzir alocacao ilimitada ou filas sem limite.
- Copiar codigo GPLv3 do Tactility.
- Substituir testes host por validacao visual manual.

## Gate de Validacao

Cada fase deve preservar os seguintes gates:

```bash
make -C tests/host/keymap test
make -C tests/host/keymap verify
source /home/moises/esp/esp-idf/export.sh && idf.py build
```

Flash e validacao no dispositivo devem ser executados separadamente, seguindo
`tests/manual/serial-bridge-validation.pt-BR.md`.
