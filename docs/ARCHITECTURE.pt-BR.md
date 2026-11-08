# Arquitetura

## Atualização de firmware

A flash de 16 MiB reserva `otadata`, factory de resgate de 4 MiB e dois slots
OTA de 4 MiB. Migrar o layout anterior exige um flash USB. O serviço
`cyberdeck.ota` recebe imagens em streaming pela porta HTTP 8080 da LAN. Um
segredo por dispositivo, obtido somente pela Serial-JTAG USB, autentica nonce
de uso único, SHA-256, versão declarada, tamanho e modo com HMAC-SHA256. Em
`--confirm`, `/ota/prepare` envia somente metadados; o Tab5 solicita aprovação
física antes que `/ota` transfira qualquer byte da imagem. Os metadados aprovados
ficam vinculados à transferência, e hash/projeto/versão embutidos são conferidos
antes de selecionar o slot de boot. Aprovação e transferência expiram em 120 s.
`--silent` mantém a transferência direta. A imagem nova é marcada válida depois do
checkpoint normal e de obter IP pelo Wi-Fi em até 30 s; falhas solicitam rollback. HTTP
segue sem criptografia, assim como Secure Boot/flash encryption estão
desativados. Procedimento em `AGENTS.md`.

O `cyberdeck5` é um ambiente operacional embarcado para o único alvo suportado,
o M5Stack Tab5. O produto integrado combina o kernel FreeRTOS e a plataforma
ESP-IDF/BSP com as camadas próprias `platform/hardware`, `system runtime`, `SDK`
e `apps`; runtime e apps são compilados no firmware. O cyberdeck5 não implementa
um kernel próprio nem isolamento de memória entre apps. Consulte o
[plano de transformação](OS-TRANSFORMATION-PLAN.pt-BR.md) e o
[plano de implementação](OS-IMPLEMENTATION-PLAN.pt-BR.md) para escopo, critérios
e validação.

## Princípios

- Usar o hardware diretamente quando isso reduzir camadas e alocações.
- Manter operações bloqueantes fora da thread da UI.
- Não criar extensibilidade antes de existir uma segunda ferramenta real.
- Preferir buffers limitados e descarte explícito de dados antigos.
- Compilar apps contra um SDK predefinido, começando por `storage` com leitura
  bounded e `logger`; não expor VFS, LVGL ou handles de hardware diretamente.
- Preservar output/reply compatível com apps atuais, sobretudo SSH, antes de
  fixar um contrato universal de command apps.

## Organização semântica

O componente ESP-IDF continua único e é organizado por contexto, sem criar
componentes ESP-IDF adicionais. `src/apps/` contém os fluxos do produto
(shell, Wi-Fi, SSH e screenshot), enquanto `src/platform/` contém as
integrações de entrada, display, sensores, logging e networking. Os headers
espelham essa árvore em `include/apps/` e `include/platform/`. A lógica
pura deve permanecer testável no host; `app_main` é o ponto de composição das
partes concretas.

### Camada visual

Os widgets LVGL são mantidos separados dos serviços e comandos. `cyberdeck_ui.cpp`
é a fachada de composição e orquestra callbacks, estado de entrada e serviços;
`cyberdeck_header_view.cpp` cria e atualiza exclusivamente o header; e
`cyberdeck_terminal_view.cpp` cria o textarea e o teclado virtual. Esses views
recebem dados já resolvidos e callbacks de interação, mas não incluem
`wifi_mgr`, `ssh_client`, `ble_mgr`, shell, NVS, I2C ou persistência. Serviços e
modelos de produto permanecem em `src/apps/`, sem dependência de LVGL.

O header mantém uma única `cyberdeck_font` em todos os labels de texto e uma
métrica de line-height compartilhada. A célula da bateria possui três labels:
ícone semântico, percentual base e uma cópia sobreposta do percentual para o
peso visual. O terceiro label usa a mesma fonte, fica deslocado 1 px e é
ignorado pelo layout; acompanha texto e visibilidade sem alterar a geometria.
Essa é a implementação de `REQ-HEADER-FONT-01..03`, coberta por
`AC-HEADER-FONT-01` em `tests/host/keymap/test_display_views.cpp`.

O handoff do teclado físico fica isolado em `cyberdeck_keyboard_dispatch.cpp`.
Ele possui a fila bounded de snapshots, o mutex de enqueue/rollback e o
agendamento por `lv_async_call`. O dispatcher não conhece sessões de shell,
SSH, Wi-Fi ou BLE.

O estado de sessão e as decisões de entrada ficam em
`src/apps/shell/cyberdeck_shell_session.{h,cpp}`. A `session` é livre de
LVGL e possui a linha corrente, o cursor, o histórico, o SSID Wi-Fi pendente, o
`cyberdeck_edit_line` de cada contexto (menu, SSH, senha, host key), a chave de
estado SSH e o buffer de passkey BLE. Ela decide o que cada tecla faz e executa
os comandos, sempre através da interface `host`: saída de terminal, pumps,
serviços de Wi-Fi/SSH/cat, shell local e BLE. `cyberdeck_ui.cpp` fica como
fachada: constrói e atualiza widgets, traduz `lv_key_t` para
`cyberdeck_shell_session::key`, mantém o pump de eventos e implementa o `host`.

O estado da sessão também é dela por completo: linha, cursor, histórico, SSID
pendente, editor por contexto, buffer de passkey BLE e os dois tokens de
conexão Wi-Fi. Nenhuma API de plataforma é chamada diretamente; a sessão usa
apenas as portas declaradas em `host`. Isso permite linká-la no host contra um
`host` falso, e `tests/host/keymap/test_shell_session.cpp` exercita o
comportamento de `handle_key`, `execute_line` e da inserção de texto dessa
forma.

O agendamento da reconexão BLE em background fica isolado em
`cyberdeck_ble_background.cpp` (`scheduler`): restauração de bonds no boot,
janela periódica de 10 s, matching por endereço+`addr_type`, teto de 3
tentativas por ciclo e preempção manual. Ele opera sobre o
`state_machine` BLE e enfileira comandos `ble_mgr`, sem tocar LVGL ou
terminal. A UI mantém apenas o pump de eventos (`on_ble_event`,
`process_ble_events`, `ble_submit_actions`) e roteia eventos e preempção
para o scheduler.

`components/m5stack_tab5/` é o BSP vendored do único alvo Tab5. Componentes
gerenciados pelo ESP-IDF permanecem fora desta reorganização. Não há
portabilidade dos Drivers do Tactility nesta fase.

## Matriz de responsabilidades do produto Tab5

| Superfície/requisito | cyberdeck5 | FreeRTOS | ESP-IDF | BSP M5Stack | Hardware |
| --- | --- | --- | --- | --- | --- |
| Boot, safe mode e checkpoint | Política, ordem de composição e diagnóstico em `app_main`/recovery | Tasks, sincronização e scheduler | Boot/NVS/componentes e serviços de sistema | Inicialização SD/display e APIs do Tab5 | ESP32-P4, C6, SDMMC e alimentação física |
| Runtime e apps | Registro, lifecycle/grants cooperativos, shell, apps e interfaces SDK | Concorrência e lifecycle das tasks | APIs de rede, VFS, TLS e drivers consumidos | Interfaces específicas do Tab5 | Periféricos e peers externos |
| Display, touch, teclado e sensores | Modelos, política, views e despacho | Tasks, filas e sincronização | LVGL/I2C e integração de drivers | Painel/touch, teclado e barramento | DSI, touch, IMU, INA226 e expansores |
| Wi-Fi, SSH, HTTP, time sync e BLE | Política de conexão, protocolo, UI, allowlists e persistência própria | Workers, filas, timeout/join cooperativos | ESP-Hosted, Wi-Fi, sockets, HTTP/TLS e NimBLE | Transporte/controlador do Tab5 | C6/radios, ponto de acesso, servidores e periféricos BLE |
| SD e dados persistidos | Namespace virtual, formatos do produto, grants e transações/recovery | Coordenação de leases e workers | VFS/FATFS/NVS e sincronização | Mount, handle e recursos de storage | Cartão SD, flash e barramentos |
| Poweroff | Admissão one-shot, quiescência, ordem e fail-closed | Workers/watchdog e deadlines cooperativos | APIs de storage/sync | Expander e sinal da placa | Rail/charger e corte físico de energia |

As colunas indicam dono do comportamento, não uma garantia de falha isolada nem
uma alegação de segurança: grants são cooperativos e apps compartilham memória.
Quando uma API cruza a fronteira, a implementação da política continua em
cyberdeck5 e a primitiva/driver permanece com a plataforma/BSP ou hardware.
Detalhamento por fluxo, teste e cenário de falha fica em
[`OS-TAB5-REQUIREMENTS.pt-BR.md`](OS-TAB5-REQUIREMENTS.pt-BR.md); os contratos
de código são a fonte dos limites temporais e formatos.

## Fluxo de boot atual

O estado confirmado ainda concentra em `app_main` a inicialização de NVS e
recovery, a preparação ESP-Hosted no caminho normal **antes** do mount SD
(controlador SDMMC compartilhado no IDF 6), a recuperação de sidecars, o
registro e o logging dos system apps, o
display, IMU/UI, screenshot, `screen_off`, bateria, teclado, brilho e o
startup normal ou safe mode dos serviços, seguido do checkpoint de recovery.

 1. Inicializar NVS/recovery; preparar ESP-Hosted no boot normal, montar o SD e
    validar o handle do BSP. Falha de mount passa por `ESP_ERROR_CHECK` antes da
    UI: boot sem SD não é um modo degradado existente. O SD é requisito do
    produto; ausência impede a inicialização e o reboot volta a tentar o mount,
    comportamento aceito pelo operador. Mensagem em tela para cartão ausente
    seria melhoria opcional e exigiria display de erro antes da UI normal.
2. Recuperar sidecars sob coordenador SD, registrar system apps e iniciar o
   event log quando possível; falhas de NVS/recovery/logging preservam o diagnóstico.
3. Inicializar display/LVGL pelo BSP do Tab5.
4. Sob lock do display, iniciar IMU, UI, screenshot e proteção de tela
   (`screen_off_init`); falha nessas etapas interrompe o boot.
5. Inicializar bateria, teclado e brilho; falhas de bateria são não fatais.
6. Iniciar os serviços pelo supervisor no caminho normal ou a superfície de
   safe mode; falhas preservam o latch e adiam o checkpoint.
7. Registrar o checkpoint de recovery somente quando o startup estiver pronto.

### Alvo/backlog de composição

O alvo arquitetural é reduzir `app_main` a apenas mount do SD, NVS,
register/start dos system apps e checkpoint de recovery. Essa redução ainda é
backlog: não descreve o estado atual nem autoriza remover a composição
existente sem migrar cada responsabilidade e preservar seus contratos.

## Bateria

`battery_status.cpp` concentra a lógica pura: recebe a tensão medida do
barramento em `bus_voltage_mv` e satura a faixa aprovada `6000..8100` mV em
`0..100`. Corrente positiva é descarregando, negativa é carregando e corrente
zero ou indeterminada é neutral. No estado neutral, a UI conserva somente o
percentual numérico, sem glyph ou texto de estado. Ausência é produzida apenas
por `present == false`; falha de leitura é `unavailable`, distinta de neutral,
e nenhum percentual é fabricado. O módulo não depende de ESP-IDF, FreeRTOS,
I2C ou LVGL.

`ina226_reader.cpp` é a integração separada de hardware. Ela usa o barramento
I2C do BSP no endereço `0x41`, lê o registro de tensão `0x02` (1,25 mV/LSB),
grava configuração `0x4527` e calibração `0x0D55`, e executa as leituras de
tensão e corrente numa task FreeRTOS a cada
1000 ms. Um mutex protege o snapshot copiado consultado pela UI. Falha de
probe/identificação publica `absent` somente por presença explícita; uma leitura
que falha depois do startup publica `unavailable`; em `app_main` a falha de
inicialização é apenas registrada. O fluxo não persiste estado de carga e
não implementa controle ou proteção de bateria. Nenhuma transação I2C ocorre na
UI, e o reader não registra timer LVGL.

`cyberdeck_battery_soc.cpp` é o estimador puro de estado de carga (sem ESP-IDF,
FreeRTOS, I2C, NVS, LVGL ou BSP). Ele mantém um contador de coulomb em unidades
de 1e-4 % com o resto da divisão transportado entre observações, ancorado na
tensão de circuito aberto (`ocv_from_terminal_mv`: tensão terminal compensada
pela resistência série medida de 330 mOhm, confiável até +/-500 mA e saturada em
`6000..8100` mV). O teto da janela é a tensão de repouso medida depois do corte
em 8200 mV (âncoras registradas em 8106 e 8116 mV), não um pack pleno: `100 %`
significa a carga máxima desta política, cerca de 5 pontos otimista no meio da
curva; a janela de 2100 mV dá 21 mV por ponto e o cálculo trunca. A capacidade
aprovada é `pack_capacity_mah = 2000`.
`elapsed_ms == 0` é uma reobservação neutra (por exemplo o segundo timer de
1 Hz ou uma consulta de status sob demanda): nada é integrado, a janela de
quietude não é estendida nem zerada e não há reancoragem. Intervalos acima de
`60000` ms são lacunas reais de leitura: não integram nem reancoram e zeram a
janela acumulada, de modo que uma lacuna não "costura" os 60000 ms de repouso.
Amostra inválida mantém a estimativa e bateria ausente a reinicia. A estimativa é reancorada na tensão de circuito aberto uma vez por episódio de
repouso: exige 60000 ms contínuos em repouso (|I| <= 15 mA, sem carregar,
bateria presente) e depois três amostras consecutivas com o mesmo alvo de
circuito aberto (`anchor_confirmations`); qualquer interrupção ou alvo diferente
reinicia a confirmação. A ancoragem é gravada no centro do bin do alvo
(alvo + 0,5 %), então uma única unidade de carga não troca o valor do header, e
o valor publicado nunca varia mais de um ponto por observação. Uma segunda
ancoragem no mesmo episódio nunca ocorre, então a integração continua normal
enquanto o pack permanece em repouso. O percentual do
header é essa estimativa, nunca uma leitura direta da tensão terminal, o que
elimina a oscilação 94 % <-> 98 % no corte do carregador.

`battery_status.cpp` também concentra dois helpers compartilhados:
`current_ma_from_raw()` converte o registro assinado de corrente do INA226 com a
escala aprovada de **300 uA/LSB** (3/10 mA por contagem, truncada para zero e
elevada para +/-1 mA) e `ocv_from_terminal_mv()` fornece a âncora de circuito
aberto. `ina226_reader.cpp` usa o primeiro em vez do antigo divisor derivado da
calibração; configuração `0x4527`, calibração `0x0D55` e a conversão de tensão
de 1,25 mV/LSB permanecem intactas.

`cyberdeck_battery_protection.cpp` é a política pura de proteção de carga,
testável no host e sem dependências de ESP-IDF, FreeRTOS, I2C, NVS, LVGL ou BSP.
Ela define os estados `battery`, `external`, `charging`, `absent` e `unknown`,
com threshold de corrente ±15 mA, `external` >= 7900 mV e `absent` >= 8330 mV
exigindo 5 votos consecutivos. O corte atua quando o estado é `charging` e a
**tensão terminal** atinge >= 8200 mV; o percentual estimado não participa mais
da entrada. Uma vez ativo, o latch permanece engajado até (a) o SoC estimado
ficar <= 90 na primeira amostra, sem votos e fora de `charging`, (b) cinco
amostras consecutivas com o estado confirmado `battery`/`absent` (sem fonte
externa), ou (c) a opção ser desligada. A única opção persistida no NVS é
`protection_enabled` (default `true`); desabilitá-la religa o carregador
imediatamente e exige reativação explícita. O release por SoC acompanha a
escala de percentual: 90 % da janela de 2100 mV cai em ~7890 mV, preservando a
banda física de ~210 mV do release anterior de 85 % em `6000..8230` mV
(7895 mV). O que importa é a banda física; o número exibido muda com a escala.
Falhas de leitura não desligam `CHG_EN` nem perdem o último estado seguro.

Duas regras de precedência tornam essa política segura para uma bateria
removível. Primeiro, somente uma leitura INA226 **inválida** preserva o snapshot
anterior: uma leitura válida atualiza tensão, corrente, percentual e
disponibilidade mesmo quando `CHG_STAT` não pôde ser lido, e o sinal ausente é
publicado como `unknown` (nunca como `not_charging`). Uma falha de leitura do
expander, portanto, não congela mais o indicador nem o oculta. Segundo, a
precedência de ausência/presença é resolvida antes do sinal do carregador e da
corrente: com o barramento em `absent` (>= 8330 mV voteado) o estado é
`absent`, de modo que um `CHG_STAT` preso em low não fabrica uma bateria
carregando. Estados, thresholds e número de votos permanecem os aprovados. O
snapshot também publica o sinal `charge` já decodificado.

`cyberdeck_battery_view.cpp` é a camada pura de apresentação do indicador, sem
ESP-IDF, FreeRTOS, LVGL, I2C, NVS ou BSP. `resolve()` é um mapeamento total de
`battery_state` + `charge_signal` + disponibilidade + percentual para
`{visible, show_percentage, percentage, glyph}`: indisponível ou `unknown` fica
oculto; `absent` fica visível com o glyph externo e **sem** percentual;
`charging` (por estado ou por sinal) usa o glyph de carga com percentual; e
`battery`/`external` usam o glyph de bateria com percentual. O glyph nunca é
escolhido pelo nível: o percentual numérico ao lado é a única fonte de nível.

`battery_protection.cpp` é o adaptador exclusivo de hardware: inicializa o
Expander B via `bsp_io_expander1_init()` (endereço I2C 0x44), configura
`CHG_STAT` no pin 6 como entrada active-low com pull-up e `CHG_EN` no pin 7
como saída push-pull com valor inicial alto (carregador habilitado). Integra o
reader INA226 (sensor-only), a política pura, a persistência NVS da opção
`enabled` e expõe snapshots para a UI via timer LVGL de 1 s. Um único
`sample_and_publish` alimenta a política e publica as duas projeções: a
`cyberdeck_battery::snapshot` (`charge_class`, usada pelo shell/status) e o
snapshot puro da política em `battery_protection_get_policy_snapshot`, que é a
única entrada de bateria consumida pela view do header. Falhas de I2C,
NVS ou leitura de `CHG_STAT` são registradas em log sem desligar `CHG_EN` nem
perder o último snapshot seguro.

O header puro `cyberdeck_battery_protection.h` também concentra a representação
textual do status: `state_name`, `charge_signal_name` e `format_status_line`.
O último recebe um único `snapshot` e produz, sem alocação ou ESP-IDF, a linha
bounded `battery: state=%s charge=%s available=%s voltage_mv=%ld current_ma=%ld
percentage=%ld protection=%s charger=%s\n`, com terminação NUL e tratamento seguro
para capacidade zero. A UI usa esse formatador em `battery protection status`
e não consulta getters live adicionais.

## Proteção de Carga da Bateria

O firmware implementa **battery protection** (proteção de carga) para prolongar a vida útil da bateria. A política pura (`cyberdeck_battery_protection`) define os estados `battery`, `external`, `charging`, `absent` e `unknown`, com threshold de corrente ±15 mA, `external` ≥ 7900 mV e `absent` ≥ 8330 mV exigindo 5 votos consecutivos. O corte ativa somente com estado `charging` e tensão terminal ≥ 8200 mV, sem depender do percentual estimado. Uma vez ativo, o latch só libera com SoC estimado ≤ 90 (na primeira amostra), com 5 amostras consecutivas sem fonte externa, ou ao desligar a opção. A única opção persistida no NVS é `protection_enabled` (default `true`); desabilitá-la religa o carregador imediatamente e exige reativação explícita. Falhas de leitura não desligam `CHG_EN` nem perdem o último estado seguro.

A rastreabilidade desse fluxo é mantida em `code-map.md`: REQ-BAT-001 e
REQ-BAT-002 fixam a matemática e os estados, REQ-BAT-003 fixa a apresentação
neutral, REQ-BAT-004 fixa boot não fatal e ausência de controle de carregador,
REQ-BAT-005 fixa a integração INA226, REQ-BAT-006 exige que esta documentação
e o mapa permaneçam alinhados aos contratos host, REQ-BAT-007 fixa o Expander B
e pinos CHG_STAT/CHG_EN, REQ-BAT-008 fixa os estados e thresholds, REQ-BAT-009
fixa o corte por tensão terminal e o latch 90/fail-safe, REQ-BAT-010 fixa NVS,
timer UI, shell e `ui.type`, REQ-BAT-011 fixa o SoC estimado por contagem de
carga ancorada em OCV e REQ-BAT-012 fixa a escala de corrente de 300 uA/LSB e os
helpers de OCV.

### Indicador de energia removível (REQ-BAT-UI-001..005 / AC-BAT-UI-001..006)

- **REQ-BAT-UI-001 / AC-BAT-UI-001..002** — falha de `CHG_STAT` não congela o
  snapshot: leitura INA válida atualiza tensão/corrente/disponibilidade e
  alimenta o estimador de SoC; leitura INA inválida preserva o estado seguro da
  proteção (e a estimativa) mas publica `available=false` para ocultar a UI e
  não renderizar um snapshot antigo.
- **REQ-BAT-UI-002 / AC-BAT-UI-003** — precedência de ausência/presença antes
  do sinal do carregador, preservando estados, thresholds e votos aprovados.
- **REQ-BAT-UI-003 / AC-BAT-UI-004** — camada pura (`cyberdeck_battery_view`)
  com mapeamento total para visível, percentual e glyph semântico, sem
  ESP-IDF, FreeRTOS ou LVGL.
- **REQ-BAT-UI-004 / AC-BAT-UI-005** — a UI mantém três labels (ícone,
  percentual base e overlay) e a grade 30/40/30 e apenas aplica a view, sem
  regra de negócio no LVGL e sem acesso
  direto a I2C/NVS/reader.
- **REQ-BAT-UI-005 / AC-BAT-UI-006** — `absent` mostra só o glyph externo e o
  glyph nunca é escolhido pelo percentual.

Limitação de hardware: a ausência é inferida pela tensão fixa do barramento
(< 6000 mV ou >= 8330 mV, 5 votos), sem sinal dedicado de presença. Limitação de recurso: a
fonte `cyberdeck_font.c` embarca apenas os codepoints FontAwesome `0xF067`
(mais), `0xF068` (menos), `0xF0E7` (carga), `0xF1EB` (Wi-Fi) e `0xF240..0xF244`
(bateria); não existe glyph de tomada, USB ou energia na fonte compilada e
regenerá-la está fora do escopo, então o caso de alimentação externa sem
bateria usa `LV_SYMBOL_MINUS` como marcador de "sem bateria".

## Proteção de Tela

O firmware protege o display contra burn-in e economiza energia
desligando-o após um tempo de inatividade (padrao: 2 minutos,
configuravel de 0 a 1440). A implementacao tem duas camadas:

- `cyberdeck_screen_protection`: politica/estado puro, testavel no
  host. Gerencia o valor do timeout, o estado on/off e a persistencia
  NVS de `effective_minutes` e `last_positive_minutes`. Definir o
  timeout como 0 desabilita o desligamento automatico sem perder o
  ultimo valor positivo.
- `screen_off`: adaptador LVGL/BSP. Cria um objeto LVGL preto que
  cobre toda a tela, pausa/retoma um timer de inatividade de 1 segundo
  e trata o duplo toque (janela de 400 ms) para religar o display.
  Uma task dedicada FreeRTOS (`screen_nvs`) escreve o timeout no NVS
  fora da task LVGL. A ordem de boot restaura o timeout persistido
  antes do timer iniciar.

Os comandos `screen` do shell roteiam para essa camada:

- `screen on` — chama `screen_off_turn_on()` (restaura a tela e o
  brilho anteriores).
- `screen off` — chama `screen_off_turn_off()` (carrega a sobreposicao
  preta).
- `screen timeout <0-1440>` — chama `screen_off_set_timeout_minutes()`
  e persiste o valor; 0 desabilita o timer.

A tela tambem e desligada/ligada por transicoes de estado do Wi-Fi e
pelo ciclo de vida da sessao SSH. O endpoint de screenshot funciona
enquanto a tela esta desligada porque captura o framebuffer LVGL
diretamente.

## Wi-Fi: dispatch e persistência

O callback do evento `GOT_IP` não executa trabalho pesado na task `sys_evt`.
Ele apenas publica um snapshot no dispatch de Wi-Fi. O coordinator processa
esse snapshot em seu worker, serializa as operações de persistência e coordena
os efeitos derivados da conexão, mantendo a task de eventos e a thread da UI
livres de I/O, mutexes longos e chamadas de rede.

Cada operação assíncrona tem ownership explícito: o produtor transfere a
mensagem ao dispatch/coordinator, que confirma (`ack`) ou a libera conforme o
resultado. Falhas de enfileiramento e operações retryable seguem a política de
retry do coordinator; itens que não podem ser processados são descartados sem
ownership ambíguo. Tokens de conexão, scan e persistência identificam a geração
corrente, portanto callbacks atrasados ou de uma tentativa cancelada não
alteram o estado atual.

O scan é assíncrono: o callback copia o snapshot dos resultados e o entrega ao
contexto que controla a UI, onde SSIDs são deduplicados e ordenados. A busca
respeita cancelamento, timeout e a geração da tentativa. Persistência só ocorre
após `connected` e `has_ip`; em falhas de conexão ou persistência, o coordinator
executa rollback do estado transitório. O fluxo de esquecimento usa wipe
explícito da credencial persistida, sem exibir ou registrar a senha.

Essa separação corrige o reboot observado logo após `GOT_IP`: o trabalho pesado
foi removido de `sys_evt`, enquanto callbacks tardios, retries e cancelamentos
passaram a ser filtrados por tokens e processados pelo coordinator.

## Screenshot HTTP

Quando o Wi-Fi está conectado e possui endereço IP, o firmware disponibiliza
`GET /screenshot` na porta 80. O endpoint aceita somente clientes da rede
privada/local (incluindo IPv4 mapeado em IPv6 e endereços locais IPv6) e retorna
um BMP 24-bit da tela LVGL. O arquivo usa o formato bottom-up do BMP: `biHeight`
é positivo e as linhas são emitidas da última para a primeira. As requisições
são serializadas, e o servidor é parado quando o Wi-Fi perde o endereço IP.

Para salvar a captura a partir de um host na mesma LAN:

```bash
curl --fail --output screenshot.bmp http://IP_DO_DISPOSITIVO/screenshot
```

### Endpoints HTTP

| Rota | Método | Resposta | Fonte |
| --- | --- | --- | --- |
| `/screenshot` | GET | `image/bmp` 24-bit bottom-up | frame RGB565 da porta de display, serializado pelo mutex de requisição |
| `/events.txt` | GET | `text/plain; charset=utf-8` | projeção textual bounded de `events.log` em chunks de 1024 bytes |
| `/battery` | GET | `application/json` com `Cache-Control: no-store` | snapshot de política em cache (`battery_protection_get_cached_policy_snapshot`) e timestamp (`battery_protection_get_cached_policy_sampled_at_us`) |
| `/usb/audio` | GET | `application/json` com `Cache-Control: no-store` | snapshot bounded do serviço opcional UAC 1.0 na USB-A Host; estado, identidade e formatos PCM |
| `/control/challenge` | GET | `application/json` com `Cache-Control: no-store` | nonce de uso único (16 bytes aleatórios, 32 hex) válido por 60 s |
| `/control/type` | POST | `202 Accepted` com `{"accepted":true,"bytes":N,"lines":N}` | texto UTF-8 limitado, autenticado e enfileirado para a task de injeção |
| `/control/terminal` | GET | `application/json` com `Cache-Control: no-store` | snapshot textual bounded (`cyberdeck_ui_term_dump`, 2048 bytes) |
| `/control/upload` | POST | `201 Created` com `{"name","bytes","sha256","path"}` | arquivo BMP autenticado (até 3 MiB) em `/sdcard/screenshots`; streaming com buffer fixo e publicação sem sobrescrita |

Todos os endpoints aceitam apenas clientes da rede privada/local, respondem 405
a métodos diferentes de `GET` e 503 quando o mutex de requisição está ocupado.
O `GET /battery` é somente leitura e barato: o handler do `httpd` não amostra o
INA226, não abre I2C, não adquire o display lock e não escreve no SD. O corpo é
um JSON de ordem fixa (`schema`=1, `state`, `charge`, `available`,
`bus_voltage_mv`, `current_ma`, `percentage`, `protection_enabled`,
`protection_active`, `charger_enabled`, `sampled_at_us`, `sampled_age_ms`,
`fw_version`, `uptime_s`) limitado a 384 bytes; valor desconhecido é `null` e um
snapshot indisponível publica `state`/`charge` como `unavailable`. Os handlers
são registrados por tabela com rollback: se um registro falhar, o servidor é
parado (`httpd_stop`) em vez de ficar parcialmente exposto.

### Canal HTTP de comandos

As rotas `/control/*` também vivem na porta 80 e existem para validar o
dispositivo com o `idf.py monitor` aberto: em vez de usar a ponte USB para
injetar teclas, o operador envia o comando pela LAN. O canal reproduz o efeito
de `ui.type` (texto + Enter na UI ativa), portanto alcança shell local, SSH,
apps e diálogos exatamente como a entrada manual.

A autenticação é dedicada e não reutiliza a chave OTA. A chave HMAC-SHA256 de 32
bytes fica no NVS (`ctl_auth/key`), é gerada com `esp_fill_random` no primeiro
boot e só pode ser lida pela ponte USB (`control.key`); nenhuma rota HTTP a
expõe e ela nunca é logada. Cada requisição usa um nonce de uso único obtido em
`GET /control/challenge` e envia `X-CTL-Nonce` e `X-CTL-MAC`, onde o MAC cobre
`<nonce>\n<METHOD>\n<uri>\n<sha256(body)>`. O nonce é consumido apenas depois de
o MAC conferir; um MAC inválido não queima o desafio do operador, mas uma
requisição autenticada sempre o consome (sem replay). O handler responde
`202 Accepted` quando o trabalho entra na fila bounded de 4 comandos e **não**
injeta nada na task HTTP: uma task própria (`ctl_input`) aplica o texto em
segmentos de 30 ms. A proteção de tela recusa a injeção (`409 screen_off`) em
vez de perder a primeira tecla no wake.

O corpo de `/control/type` é limitado a 1024 bytes UTF-8 e 32 linhas, com CRLF
normalizado para LF; `NUL` e UTF-8 inválido são recusados. `GET
/control/terminal` exige a mesma autenticação e devolve até 2048 bytes do
terminal com `truncated`. O snapshot é compartilhado: ele reflete o terminal
inteiro (shell, SSH, apps e teclado físico), não apenas a última requisição.
HTTP não oferece confidencialidade — o HMAC autentica a origem lógica, não cifra
o texto — e o filtro de peer local não identifica o usuário. Use o canal apenas
em LAN confiável.

`POST /control/upload` é o transporte binário do canal. Ele reusa a chave e o
nonce, mas assina uma mensagem própria com domínio fixo
(`cyberdeck-control-upload/v1`, nonce, método, URI, basename de destino,
tamanho declarado e SHA-256), de modo que um MAC de `/control/type` nunca
autoriza um upload. O handler autentica esses metadados (inclusive o basename
que publicará) **antes** de abrir qualquer arquivo, adquire
um lease WRITE do coordenador de SD e lê o corpo em chunks de 4096 bytes,
reconferindo o digest incrementalmente. O destino é fixo em
`/sdcard/screenshots` com basename `.bmp`; a publicação usa temporário exclusivo
no mesmo diretório, `fsync`/`close` e nunca sobrescreve um alvo existente. Um
upload por vez (`429 busy`), `413` acima de 3 MiB, `400` para nome/tamanho
inválido, `409` para alvo existente, `422` para digest divergente e `503` com o
gate de SD fechado. O tempo total de cada transferência é limitado por um
orçamento absoluto por request (`k_max_upload_duration_us`, 120 s) que
progresso não renova, mantendo o limite de timeouts consecutivos como defesa
secundária. A proteção de tela não bloqueia o upload porque a rota não
depende do display.

```bash
python3 tools/cyberdeck_http_cli.py --port /dev/ttyACM0 provision
python3 tools/cyberdeck_http_cli.py --host IP_DO_DISPOSITIVO type "help"
python3 tools/cyberdeck_http_cli.py --host IP_DO_DISPOSITIVO terminal
python3 tools/cyberdeck_http_cli.py --host IP_DO_DISPOSITIVO upload fixture.bmp
```

`cyberdeck.usb_audio` é serviço opcional do runtime. O BSP habilita o USB Host
na porta USB-A, e `usb_host_uac` 1.5.0 detecta interfaces de transmissão UAC
sem filtro por VID/PID; consultas de descritores ocorrem no worker do serviço.
Um cliente USB passivo registra presença de dispositivo e lê os descritores
(bcdUSB, VID/PID, bcdADC e direção dos endpoints isócronos), o que distingue
"nada no barramento" de "enumerou mas não é UAC 1.0 de reprodução". O snapshot
limita formatos e strings, e o handler LAN somente o serializa.
`CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=1024` é obrigatório: com o padrão de
256 bytes o descritor de configuração de um headset falha em
`CHECK_SHORT_CONFIG_DESC` e o dispositivo nunca chega ao driver de classe.

A reprodução usa o driver de classe quando o dispositivo está `ready`:
`uac_host_device_start` abre o stream PCM 16-bit estéreo na taxa da faixa e
`uac_host_device_write` alimenta o ring buffer. Como o decodificador roda mais
rápido que o relógio isócrono, a escrita bloqueia com deadline limitado em vez
de falhar, o que cadencia a reprodução. A rota é escolhida por
`cyberdeck_audio_output`: com dispositivo USB pronto o PCM vai para a USB-A;
se o stream não abrir, o player volta ao codec da placa. O volume do player
chega ao fone pela Feature Unit quando houver suporte e, caso contrário, por
atenuação do PCM.

Remoção do fone: a biblioteca USB Host só entrega
`USB_HOST_CLIENT_EVENT_DEV_GONE` a um cliente que mantém o dispositivo aberto,
então um player em pausa (sem stream UAC) nunca é avisado. O worker do serviço
consulta `usb_host_device_addr_list_fill` a cada ciclo de 50 ms, fecha o stream e
publica `waiting` quando o endereço desaparece; `playback_start` repete a
verificação antes de abrir. O close exigido pelo driver de classe roda no worker
e nunca dentro do callback, porque o driver mantém a interface na lista até o
dono fechá-la.

Inserir ou remover um fone — USB-A (UAC de reprodução) ou P2/3,5 mm — durante a
reprodução **pausa** a faixa. O player amostra as duas fontes no worker e, ao
confirmar a mudança, usa o mesmo caminho da pausa manual: fecha a saída, publica
`paused` e preserva faixa/posição; a retomada é explícita e reelege a rota. O
detector puro `cyberdeck_audio_presence::watch` exige duas leituras consecutivas
iguais (`unknown` nunca equivale a ausente, então uma falha de I2C não pausa) e a
referência é tomada no início da reprodução, de modo que um fone já conectado não
interrompe a faixa. O pino do jack vem de `bsp_headphone_detect_state()`
(`PRESENT/ABSENT/UNKNOWN`) e a presença USB de
`cyberdeck_usb_audio_host::playback_presence()`; a sonda de dispositivo no player
tem cache de 200 ms porque o jack é lido por I2C.

Para não vazar áudio para outra saída, uma escrita USB que falha só cai no codec
quando o dispositivo comprovadamente continua no barramento: o callback do
driver levanta a flag atômica `s_playback_lost`, o hook
`usb_output_hooks::device_present` a consulta e `write_timing::route_lost` pede a
pausa em vez do fallback. Sem esse hook, ou com o dispositivo presente, o
comportamento anterior (entregar o frame ao speaker) permanece.

`espressif/usb_host_uac` 1.5.0 resolve por `override_path` para
`components/usb_host_uac/`, uma cópia local: o rótulo `fail:` de
`uac_host_device_open` liberava o ring buffer depois de apagar a interface, o que
dereferenciava NULL quando a abertura falhava antes da interface existir (o
dispositivo já havia saído do barramento) e causava panic/reboot. A cópia local
libera o ring buffer antes e somente com a interface viva. A USB-C física
continua ligada à Serial-JTAG; para um fone USB-C é necessário adaptador na
USB-A Host.

## Ponte manual USB Serial-JTAG NDJSON

A ponte roda na task `serial_brg` (prio 3, 16384 bytes de stack), fora da
stack do LVGL, iniciada por `bridge_start()` ao fim de `app_main`. O driver
USB Serial-JTAG só é instalado se ainda não estiver ativo — o console
`ESP_LOG` compartilha a mesma porta —; frames e logs são serializados por um
mutex de escrita, e `esp_log_set_vprintf` redireciona o log para a mesma
porta com conversão `\n` → `\r\n` (o driver direto não passa pela conversão
do VFS).

O protocolo é NDJSON com uma linha por mensagem, limite de 4096 bytes e
correlação por `rid`. O parser JSON é próprio (sem cJSON), valida UTF-8
estrito, escapes `\uXXXX` com pares sintéticos e object/alvo de campos de
topo (`rid`, `type`, `text`, `x`, `y`, `target`, `symbol`). Sob
`bsp_display_lock`, `ui.click`/`ui.tap` resolvem o alvo (hit-test por
coordenada, ou texto visível no ancestral clicável mais próximo) e agendam
`LV_EVENT_CLICKED` com `lv_async_call`; `ui.type`/`ui.clear` injetam eventos
na fila bounded do teclado (capacidade 8) com 30 ms entre eventos;
`wifi.scan` usa o scan assíncrono do `wifi_mgr` com semáforo estático e
timeout de 8 s (cancelamento no timeout); `screen.dump` captura RGB565 por
`lv_snapshot_take`, monta o BMP 24-bit e transmite chunks de 1024 bytes com
CRC IEEE nos frames `start`/`chunk`/`end`.

`fs.write` grava bytes Base64 no `/sdcard` para testes e automação. O payload
decodificado é limitado a 2048 bytes, o caminho é confinado ao cartão e a
escrita usa temporário exclusivo. O CLI aceita `--input`, `--stdin` ou
`--data` e retorna tamanho e CRC32.

A lógica pura (parse/envelopes/BMP/CRC/`LineAssembler`) é host-testável e
autocontida: fora de `ESP_PLATFORM` ela não referencia funções de
`screenshot_bmp_*` — stride/size/header/conversão são espelhados localmente
com fórmulas idênticas — porque três dos quatro testes host linkam apenas a
ponte.

## SDK e aplicações

Apps compiladas implementam o contrato bounded de manifesto/lifecycle do
runtime e declaram seus recursos. O SDK já oferece `storage.bounded_read`,
operações de save confinadas usadas pelo editor e `logger` bounded; as fachadas
revalidam grants. O contrato de saída deve continuar compatível com o shell e
com SSH antes de padronizar replies para command apps.

Os comandos `wifi`, `ssh`, `bluetooth`, `battery`, `screen` e `log` são
aplicações de comando compiladas (`cyberdeck.wifi.shell`, `.ssh.shell`,
`.bluetooth.shell`, `.battery.shell`, `.screen.shell`, `.event_log.shell`).
Cada manifesto declara um comando raiz, recursos e `command_help_entries`, sem
dependência do serviço homônimo e sem `owns_console`; `execute()` funciona mesmo
com o serviço parado ou falho. A camada pura (`cyberdeck_*_console.{h,cpp}`)
não inclui LVGL/ESP-IDF/`session::host`: menus, pickers, modos SSH, composer,
filtro e ownership de entrada continuam na composição de UI, atrás de uma porta
estreita por aplicação. `wifi` e `log` guardam configuração por sessão
(duração do survey, janela de eventos) restaurada por
`runtime::reset_session_states()` quando um novo console é criado. Os seis
comandos saíram do catálogo legado de help e do switch do parser; `help` publica
suas linhas como `[app]`.

O `cyberdeck.editor` é uma command app foreground compilada. Seu modelo não
inclui LVGL, VFS ou descritores: recebe somente a fachada SDK de storage, com
leitura, temporário, `flush_or_fsync` e `rename_atomic`, todos confinados ao
namespace virtual. Documentos são limitados a 12000 bytes, rejeitam binário e
bytes inválidos e suportam UTF-8, UTF-16 LE/BE e Windows-1252. Arquivos
existentes preservam codec/BOM/EOL; novos usam UTF-8 sem BOM e LF. A escrita
segue temporário -> flush/fsync do arquivo -> commit no mesmo diretório; fsync
do diretório é best-effort. O backend serializa rename, recusa overwrite e, se o
destino existir, move-o primeiro para o sidecar exclusivo `<destino>.rollback`;
após falha no commit tenta restaurá-lo. O resultado preserva diagnóstico primário
(`stage`/`errno`) e separado de rollback (`rollback_stage`/`rollback_errno`,
`rollback_attempted`). Sidecar já existente nunca é substituído. Cada etapa
revalida grant/lifecycle. Falhas preservam dirty e o diálogo; o diagnóstico bounded aparece no
rodapé/terminal e no event log. O modelo concentra edição,
busca, undo/redo bounded e entrada direcional/gestual.
`cyberdeck_editor_view` é a única superfície LVGL do editor: usa slots fixos
para linhas, documento/cursor/status e uma janela de scroll bounded. Ela não
acessa VFS nem serviços. O window manager fornece o contexto opaco e a fachada
de input é revalidada a cada evento; teclado físico/virtual, setas, toque e
gesto chegam ao modelo. `Ctrl+S`, `Ctrl+Q`, `Esc` e `Ctrl+F`/Enter/Esc cobrem
salvar, fechar, confirmação Salvar/Descartar/Cancelar e busca. O teardown
remove a superfície antes da raiz LVGL para impedir callbacks tardios.
O rodapé é uma única linha bounded ancorada na base da superfície útil do
editor. Ele contém os atalhos ativos e somente a sigla do encoding (`UTF-8`,
`UTF-16LE`, `UTF-16BE` ou `Windows-1252`). O inset do teclado virtual reduz
essa superfície antes do cálculo da capacidade do documento e da posição do
rodapé, sem sobreposição. Mudanças de tamanho recalculam o layout e limitam
scroll e visibilidade do cursor em retrato e paisagem.


O aplicativo `cyberdeck.image_viewer` (`image <caminho>`) é um app foreground
isolado: o recorte aprovado é um BMP 24-bit `BI_RGB` sem compressão de no máximo
5 MiB e 1..1280 px por eixo, com listagem do diretório limitada a 64 nomes
`.bmp` ordenados por bytes. Um `image <caminho>` explícito também funciona
quando o arquivo pedido ordena depois desses 64 nomes: o scan mantém a página
ordinária mas promove o pedido para a última posição (63 menores + pedido),
preservando o cap, a ordenação, a unicidade e todos os filtros de caminho,
formato e tamanho, e só depois de o próprio scan provar que o arquivo é um BMP
elegível daquele diretório. O indicador e o `+` descrevem a janela, não o
diretório inteiro; um scan de navegação nunca promove. PNG/JPEG, compressão RLE,
transparência, zoom e varredura recursiva são recusados em vez de adivinhados.

A divisão de camadas segue a regra do editor. `cyberdeck_image_bmp` é dono do
envelope (assinatura, `biSize`, planos, profundidade, compressão, dimensões,
`((w*3)+3) & ~3` e limites em 64 bits), `cyberdeck_image_fit` é dono da
colocação inteira (`min(área/imagem)` com clamp para nunca ampliar, mais o zoom
LVGL e a posição do objeto cujo centro coincide com o retângulo encaixado) e
`cyberdeck_image_decode` lê uma linha BGR (com padding) por `pread` direto no
frame RGB565 final, então não existe buffer de 5 MiB: a única alocação grande é
`w*h*2` (no máximo 3.276.800 B) mais uma linha de scratch no mesmo bloco.
`cyberdeck_image_list` enumera pelo seam confinado do shell local, portanto
traversal, alias `/sdcard`, symlink, diretório, arquivo vazio e arquivo acima do
teto falham fechado.

A decodificação roda em task worker dedicada com geração monotônica; resultado
superado por um pedido mais novo, ou que termina depois de `Esc`, é descartado
em vez de publicado. O aplicativo nunca chama LVGL e a UI nunca toca o
filesystem. Apenas um frame é publicado por vez e sua posse é transferida para
`cyberdeck_image_view`, que libera o frame anterior ao repointar o widget e o
último no `destroy()`; a UI destrói a view antes do supervisor parar o
aplicativo. A posse de teclado é um predicado puro: as setas só são consumidas
enquanto o visualizador é o foreground **e** há imagem publicada, então uma
carga recusada mantém o teclado do shell, e `Esc` sempre volta para
`cyberdeck.shell`.

## SSH

`ssh_client` usa uma task FreeRTOS dedicada, filas de entrada e senha e
callbacks de saída/estado. O callback de UI é executado sob o lock do display
para que widgets LVGL não sejam alterados concorrentemente.

## Header de estado

O header usa a grade direta 30/40/30 (título/relógio/célula direita). A célula
direita possui dois filhos, nessa ordem: o ícone Wi-Fi e o grupo de bateria. O
ícone Wi-Fi é desenhado com primitivas LVGL e fica claro somente quando o Wi-Fi
está habilitado, conectado e possui IP; fica escuro nos demais estados. O SSID
nunca é renderizado. Estados e erros de SSH são exibidos no terminal e
registrados no log de eventos. Diagnósticos de rede continuam disponíveis pelo
comando `wifi` do shell, fora do header.

O grupo de bateria mantém exatamente três labels: um glyph semântico, o
percentual numérico saturado e uma cópia sobreposta desse percentual. A escolha
não acontece na camada LVGL; o overlay é apenas uma decisão de apresentação.
`refresh_battery_status` copia do adaptador de proteção o snapshot puro da
política, entrega-o a `cyberdeck_battery_view::resolve` e aplica o resultado:
`charging` renderiza o glyph de carga com o percentual, bateria presente
(`battery`/`external`) renderiza o pictograma constante de bateria com o
percentual, um `absent` voteado renderiza o glyph externo sem percentual, e
leitura indisponível ou estado `unknown` oculta o grupo inteiro. O glyph nunca
é selecionado pelo nível do percentual; o nível pertence ao número ao lado. Ele
é atualizado a partir do snapshot sincronizado do adaptador, sem acesso direto
a I2C, expander, NVS ou reader. O layout do
Wi-Fi reage a `LV_EVENT_SIZE_CHANGED` dentro de sua parte alocada da célula
direita, com pixels visíveis a aproximadamente 2 px da borda dessa subcélula.

### Geometria do ícone Wi-Fi

O indicador Wi-Fi mantém o tamanho, a semântica dos estados e as cores atuais.
O limite inferior visual dos arcos superiores é
`center_y - outer_radius * (1 - sin(45°))`; o ponto fica 1,5 px abaixo desse
limite (faixa aceita de 1–2 px). O mesmo cálculo de geometria
é usado na criação do ícone e no reposicionamento após
`LV_EVENT_SIZE_CHANGED`; portanto, o resize altera somente a posição, não a
aparência nem o estado do indicador. A âncora vertical aprovada é
`center_y = box_height - 12.5px`; no header de 42px, isso corresponde a
`29.5px` e alinha opticamente o ícone ao título e ao relógio. Essa âncora
mantém X, tamanho, gap, cores e semântica de estado; o resize continua
atualizando somente a posição.

## Bluetooth LE (ESP32-C6 via ESP-Hosted)

O radio BLE nao reside no ESP32-P4; ele pertence ao coprocessor ESP32-C6
alcancado por `esp_hosted` (VHCI/HCI), com a stack host (NimBLE) rodando no P4.
Nenhum modulo BLE puro toca `lvgl.h`, FreeRTOS, NVS, BSP ou ESP-IDF, exceto o
adaptador `ble_mgr.cpp`. Bluetooth Classic (BR/EDR) esta fora do escopo e e
rejeitado por contrato.

### Modulos puros (testaveis no host)

- `cyberdeck_ble_types.{h,cpp}`: tipos limitados, classificacao por `appearance`
  (teclado `0x03C1`, mouse `0x03C2`, fone `0x0401`/`0x0408`/`0x0418`/`0x0419`/`0x041A`/`0x041B`),
  sanitizador de nome bounded/UTF-8, normalizacao estrita de endereco, helpers
  de passkey (parse/formato/mascaracao `******`) e `device_list` deduplicado por
  endereco (RSSI mais forte, primeiro nome, `paired`/`connectable` monotonicos).
- `cyberdeck_ble_state_machine.{h,cpp}`: maquina de estados da tela
  (`idle`/`searching`/`results`/`paired`/`pairing`/`auth`/`connecting`/`connected`),
  deadlines exatos (scan 10 s, pair 30 s, auth 30 s, connect 20 s), tokens
  monotonicos para descartar callbacks obsoletos, acoes derivadas
  (`start_scan`/`pair`/`submit_auth`/`connect`/`reconnect`/...), quatro teclas
  (`up`/`down`/`enter`/`escape`), mensagens unicas por classe (vazio/falha/timeout/
  cancelamento para scan/pair/connect), reconexao automatica com orcamento de 3
  tentativas e rearme manual. O predicado puro `owns_input()` e a unica fonte
  de ownership das teclas: `searching` e fluxos ativos retêm BLE, enquanto
  `results`/`paired` so retêm as teclas quando a lista possui itens. Assim,
  uma conclusao vazia, falha ou timeout libera o terminal depois de anexar a
  mensagem/lista final, sem apagar a saida.
- `cyberdeck_ble_event_dispatch.{h,cpp}`: seam bounded (8 eventos) entre
  callbacks da stack e o modelo de tela; filas com overflow fail-closed,
  geracoes de token monotono para scan/pair/connect, `event_summary` sem
  segredos (passkey mascarado).
- `cyberdeck_ble_store.{h,cpp}`: persistencia logica de bonds (capacidade 16,
  registro `addr`+`name`+`kind`+`last` sem material de chave), encode
  deterministico (`CDB1;addr=...;name=...;kind=...;last=0|1`), decode estrito
  rejeitando campos duplicados/reordenados/desconhecidos (impede contrabando de
  LTK/IRK/passkey), deserialize atomico fail-closed, round-trip de reboot.

### Adaptador ESP-IDF (`ble_mgr.{h,cpp}`)

Unico modulo autorizado a falar com a stack BLE do C6 via `esp_hosted`.
`ble_mgr_start` executa em ordem estrita `esp_hosted_connect_to_slave()` ->
`esp_hosted_bt_controller_init()` -> `esp_hosted_bt_controller_enable()` ->
`nimble_port_init()`, registra cada retorno e encerra a tentativa sem iniciar
NimBLE quando transporte ou controlador falham. O boot permanece não fatal e
não há retry/reset loop; a versão do firmware C6 é consultada e registrada pela
API `esp_hosted_get_coprocessor_fwversion` quando disponível. Possui task FreeRTOS dedicada (`ble_mgr`, stack 4 KiB, prio 5), task
host nomeada `ble_host` com stack de 8 KiB e fila bounded de comandos (8). A UI apenas enfileira acoes; `ble_mgr_start` e
nao fatal no boot. O `event_dispatch` é protegido por mutex entre a task de
comandos e callbacks GAP. Usa NimBLE VHCI (`CONFIG_BT_NIMBLE_ENABLED=y`,
`CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y`, `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y`).

### Integracao Shell e UI

- `apps/bluetooth/cyberdeck_bluetooth_console.{h,cpp}`: o app de console
  `cyberdeck.bluetooth.shell` aceita exatamente `search` e `paired`; qualquer
  outra forma mantem `unknown command; type help`. A porta (`start_search`,
  `open_paired`) executa a mesma mutacao de UI que o switch antigo fazia.
- `cyberdeck_shell_help.h`: catalogo legado com 12 entradas `[terminal]`;
  `help` acrescenta, ao final, comandos unicos dos manifests de apps registrados
  como `[app]`. A linha `bluetooth [search|paired]` agora vem do manifesto do
  console, nao do catalogo legado.
- `cyberdeck_ui.cpp`: roteia `Up`/`Down`/`Enter`/`Escape` conforme o predicado
  `cyberdeck_ble::state_machine::owns_input()`, consome acoes
  derivadas e as encaminha ao `ble_mgr` via fila, processa eventos BLE em
  timer LVGL de 100 ms (`process_ble_events`), renderiza lista e mensagens
  do modelo puro.

### Configuracao

- `sdkconfig.defaults`: `CONFIG_BT_ENABLED=y`, `CONFIG_BT_CONTROLLER_DISABLED=y`,
  `CONFIG_BT_NIMBLE_ENABLED=y`, `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y`,
  `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y`.
- `main/idf_component.yml`: mantem `esp_hosted` para o link C6.
- `components/cyberdeck/CMakeLists.txt`: registra os 4 modulos puros +
  `ble_mgr.cpp`; depende de `bt`, `nimble`, `esp_hosted`.

### Rastreabilidade

REQ-BLE-001..014 / AC-BLE-001..014 mapeados em `code-map.md`; contratos host
em `tests/host/keymap/contracts/cyberdeck_ble_*.h`; alvos de teste
`test_ble_types`, `test_ble_state_machine`, `test_ble_event_dispatch`,
`test_ble_store`, `test_ble_command_parse`, `test_ble_integration_contract`.
O teardown de `nimble_port_freertos` não foi alterado nesta correção e permanece
como follow-up dedicado.
