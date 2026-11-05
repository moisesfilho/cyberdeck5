# Plano de Implementação do Sistema Operacional Embarcado Tab5

## 1. Objetivo e limite

Este plano consolida o cyberdeck5 como **sistema operacional embarcado do
M5Stack Tab5**, considerando o produto integrado: bootloader + FreeRTOS +
ESP-IDF + BSP M5Stack + runtime/userland cyberdeck5.

Não propõe escrever um kernel, implementar syscalls próprias ou reproduzir um
SO de propósito geral. O trabalho deve provar que a composição existente atende
às funções, limites, recuperação e segurança requeridos para este dispositivo.
O cyberdeck5 continua sendo uma camada de runtime/userland; escalonamento,
drivers, VFS e alocadores são responsabilidades herdadas de FreeRTOS/ESP-IDF/BSP.

Este é um plano de implementação incremental. Nenhuma fase autoriza por si só
alterar comportamento fora de seu escopo. Mudanças de firmware seguem a política
de versão do `AGENTS.md`; documentação sem mudança funcional não incrementa
`version.txt`.

O [dashboard de progresso](OS-IMPLEMENTATION-DASHBOARD.html) apresenta os
marcos deste plano, os recortes de requisitos e os critérios de aceite. Ele é
uma projeção atualizada manualmente junto com este documento e suas evidências;
percentuais não substituem os gates no Tab5.

**Base em desenvolvimento:** `version.txt` = `1.6.0`, alvo `esp32p4`, build local com
`IDF_VER=v6.1` (em `build/build.ninja`), como confirma também `code-map.md`.
O build local não identifica, por si, o firmware em execução no Tab5. Menções a
ESP-IDF 5.5.5 em evidências históricas documentam aquela validação, não a
plataforma do código atual. Antes de qualquer validação física, conferir
`sys.info` e a identidade do binário conforme `AGENTS.md`.

## 2. Premissas de produto

- Hardware suportado: M5Stack Tab5, ESP32-P4, BSP vendored e periféricos
  definidos pelo projeto.
- Firmware e apps são compilados e entregues como uma imagem controlada.
- Não há execução de binários, scripts ou plugins nativos de terceiros no SD.
- O dispositivo é um sistema dedicado, com uma interface TUI principal e
  serviços definidos pelo produto; não é um computador multiusuário.
- FreeRTOS/ESP-IDF/BSP podem fornecer mecanismos do SO; a documentação deve
  atribuir corretamente cada responsabilidade.
- Grants e quotas atuais são controles entre componentes cooperativos, não uma
  sandbox contra código malicioso.
- A enumeração abaixo representa apps **registradas**, não apps simultaneamente
  ativas; `start_all()` ignora foreground até ativação explícita.

> **Gate de escopo:** se alguma premissa deixar de ser verdadeira, interromper a
> fase afetada e reavaliar arquitetura, ameaça e plataforma antes de implementar.

### Inventário funcional preservado

Fonte atual: `components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp`
(`k_apps`, manifestos e dependências),
`components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp`
(`start_all`, `activate_foreground`, `stop_index`) e `main/app_main.cpp`.

| Bloco existente | Papel/integração que deve sobreviver às mudanças | Prova mínima de regressão |
| --- | --- | --- |
| `cyberdeck.event_log`, `cyberdeck.shell`, `cyberdeck.serial` | Log, console e caminho Serial-JTAG de diagnóstico/safe mode; console e dependências não podem ser parados pelo próprio shell | `test_app_runtime`, `test_shell_app`, contratos de serial/recovery e `term.dump` no Tab5 |
| `cyberdeck.wifi`, `cyberdeck.ssh`, `cyberdeck.screenshot`, `cyberdeck.time_sync` | Rede, SSH interativo, captura HTTP e relógio; dependências preservadas na inicialização e parada | Contratos Wi-Fi/SSH/HTTP/time sync e sessão física com IP e troca de estado |
| `cyberdeck.bluetooth` | BLE via ESP-Hosted e C6, bonds, scan e reconexão; falha opcional não destrói console | Suíte BLE e teste físico separado com periférico real |
| `cyberdeck.editor`, `cyberdeck.netdiag`, `cyberdeck.music`, `cyberdeck.demo` | Apps compiladas de primeiro plano/diagnóstico; transições não podem invalidar shell, grants ou recursos dos serviços | `test_editor_runtime_contract`, testes netdiag/player e testes de troca de foreground; aceites físicos por app |
| Recursos fora de `k_apps` | Display/rotação, teclado, bateria/proteção, screen-off, VFS local, SD, poweroff e recuperação | Contratos específicos, sequência de boot, entradas física/virtual, dados persistentes e desligamento seguro |

O player tem evidência física **parcial** (`code-map.md`, seção de áudio):
relatos de Esc e Play Selected não substituem aceitação integral nem prova de
ausência de stutter. Não converter “host verde” em “Tab5 aprovado”. Manter
serviços opcionais degradáveis onde o código já permite: o boot normal exige
shell + serial; `runtime::start_all()` ignora foreground e falhas de outros
serviços podem registrar diagnóstico sem impedir checkpoint.

**Invariantes de compatibilidade:** não alterar nomes/comandos e respostas
observáveis do shell e da ponte, protocolo NDJSON/HTTP, schemas/versionamento,
conteúdo e confinamento do SD, contratos SDK/manifestos, lifecycle/grants, modos
safe mode/normal, UI e comportamento dos periféricos sem testes de paridade e
decisão explícita. Congelar uma lista de comandos por contrato sem prender a
evolução dos catálogos; derivar expectativas das fontes de produção.

## 3. Critérios globais de aceite

| ID | Critério | Evidência exigida |
| --- | --- | --- |
| OS-TAB5-01 | Todo requisito do produto tem dono identificado: cyberdeck5, FreeRTOS, ESP-IDF, BSP ou hardware. | Matriz de responsabilidade atualizada em `docs/ARCHITECTURE.pt-BR.md` e revisão dos limites de camada. |
| OS-TAB5-02 | Os fluxos essenciais do Tab5 têm resultado esperado, falha esperada e método de recuperação definidos. | Casos de aceite em testes host/contratos e roteiro de validação física. |
| OS-TAB5-03 | Operações concorrentes e buffers relevantes têm limites e política de timeout/backpressure documentados. | Contrato/constante de produção e teste de fronteira; não criar números documentais independentes. |
| OS-TAB5-04 | Boot e recuperação são verificáveis no hardware; falhas de serviço/SD são verificadas primeiro por injeção host e só fisicamente quando houver cenário seguro. | Evidência Serial-JTAG com firmware/commit, comandos, uptime e logs; registrar o resultado atual, inclusive mount SD fatal, sem prometer boot degradado. |
| OS-TAB5-05 | Recursos de RAM/stack e latência atendem aos limites aprovados para cenários reais. | Série temporal e medições reproduzíveis no Tab5; limites derivados de requisitos, não estimados por contagem de chamadas de alocação. |
| OS-TAB5-06 | Segurança declarada corresponde ao modelo de ameaça e aos controles habilitados. | Modelo de ameaça aprovado e configuração/build verificados. Não alegar isolamento de apps enquanto for inexistente. |
| OS-TAB5-07 | Apps/serviços e interfaces existentes preservam comportamento observável e dados persistidos. | Baseline derivado do código e testes por app, contratos de compatibilidade e cenários físicos afetados. |
| OS-TAB5-08 | Otimizações só são aceitas quando melhoram uma métrica relevante sem regredir latência, memória, energia ou funcionalidade. | Imagens antes/depois identificadas, mesma configuração de teste, cenário, resolução e coletor; resultados e reversão documentados. |

Os critérios não obrigam POSIX, ELF, memória virtual, múltiplos usuários ou
isolamento de apps. Esses requisitos só entram após mudança explícita do escopo.

## 4. Fases

### Fase 0 — Fronteira, plataforma e inventário

**Objetivo:** dizer com precisão o que é o produto e o que cada camada fornece.

- [x] Atualizar README e arquitetura para identificar FreeRTOS/ESP-IDF/BSP como
  componentes do produto integrado.
- [x] Nomear a camada própria `system runtime`, evitando sugerir que cyberdeck5
  implementa o kernel.
- [x] Registrar a avaliação segundo Tanenbaum sem pontuação percentual sem
  método.
- [x] Registrar que loader, syscalls próprias e isolamento não são requisitos
  correntes do alvo fixo.
- [x] Confirmar no código atual as 12 apps registradas, as dependências do
  shell/serial, o SDK de storage/editor e o boot com ESP-Hosted/SD no IDF 6.1.

**Aceite:** README em português/inglês, arquitetura e plano de transformação
concordam sobre composição, alvo único, ausência de kernel próprio e premissas.
`code-map.md` indexa os documentos.

### Fase 1 — Requisitos operacionais do Tab5

**Objetivo:** transformar “SO para o Tab5” em cenários de uso e falha aceitos.

**Concluída:** os três recortes em
[`OS-TAB5-REQUIREMENTS.pt-BR.md`](OS-TAB5-REQUIREMENTS.pt-BR.md) mapeiam o
inventário de boot/console/ponte, conectividade/apps e periféricos/SD/poweroff
contra código, testes host e gates físicos seguros. `test_app_runtime` verifica
que o boot inicia `demo` e dependências de serviço sem ativar o editor
foreground. Foram documentados resultado/falha/recuperação, criticidade do boot,
limites e política de espera com fontes de produção, formatos persistidos e
observáveis, matriz de dono/teste/gate físico e fronteiras de responsabilidade
em `docs/ARCHITECTURE*.md`. O aceite é documental e não afirma paridade física,
medição de desempenho ou validade do firmware instalado.

- [x] Usar o inventário funcional acima como ponto de partida; incluir boot,
  shell/input, display/rotação, Wi-Fi/SSH/HTTP/time sync, BLE, SD/editor,
  bateria/proteção, screen-off, áudio/player, netdiag, demo,
  poweroff/recovery e ponte serial conforme a configuração atual. Registrar
  explicitamente o que é opcional e o que é essencial.
- [x] Para cada função, documentar precondição, resultado observável, timeout,
  falha segura, comportamento sem o periférico e como retornar ao estado
  operacional.
- [x] Classificar cada serviço por criticidade: boot-critical, essencial ao
  console ou serviço opcional/degradável. Confirmar a classificação existente
  no código em vez de copiá-la sem revisão.
- [x] Criar uma matriz de rastreabilidade `requisito -> módulo/dono -> teste
  host/contrato -> teste físico`.
- [x] Identificar observáveis e formatos já expostos (comandos/respostas, SDK,
  JSON/NDJSON, persistência, identidade de firmware) para formar baseline de
  regressão antes de propor desempenho ou reorganização.

**Aceite:** todos os fluxos considerados necessários ao produto têm um critério
objetivo; itens fora do escopo estão marcados como tais; nenhum requisito implica
execução de código não confiável sem reabrir a arquitetura.

**Validação:** revisar testes e roteiro Serial-JTAG existentes; adicionar somente
lacunas de rastreabilidade necessárias. Não fazer flash nesta fase documental.

### Fase 2 — Responsabilidade e contratos de recursos

**Objetivo:** tornar verificável a divisão de trabalho entre cyberdeck5,
FreeRTOS, ESP-IDF e BSP, sem criar uma camada universal por antecipação.

**Concluída no escopo de contratos host:**
[`OS-RESOURCE-CONTRACTS.pt-BR.md`](OS-RESOURCE-CONTRACTS.pt-BR.md) classifica
portas públicas, implementação de plataforma e exceções concretas de apps;
registra para os fluxos críticos donos, filas/buffers/tasks, fonte de limites,
espera, overflow, teardown e evidências. `test_os_resource_contract` verifica
as invariantes de boot, supervisor, lease SD, filas e quiescência contra o código
atual; testes comportamentais específicos por módulo continuam obrigatórios.
Não houve mudança de firmware: high-water mark, deadlocks reais, quiescência
global e compatibilidade sob falha física pertencem à Fase 3.

- [x] Auditar os acessos das apps e adapters a APIs de hardware/ESP-IDF e
  classificá-los como porta pública, implementação de plataforma ou exceção
  justificada. Uma proibição indiscriminada de includes `platform/` não é
  critério suficiente.
- [x] Para cada fila, buffer, task e operação bloqueante que afete um fluxo
  crítico, registrar capacidade, proprietário, duração/timeout e política em
  overflow/teardown.
- [x] Preservar `runtime::activate_foreground()` e `stop_index()` (console e
  dependências vivos, grants retidos até teardown/join bem-sucedido). O budget
  `lifecycle_timeout_ms` é diagnóstico **após** retorno do hook, não deadline
  preemptivo; os limites efetivos pertencem aos workers/joins cooperativos.
- [x] Conferir o caminho de boot real: NVS/recovery -> preparação ESP-Hosted
  (normal) -> SDMMC/SD -> sidecars sob coordenador -> apps/log -> LVGL/IMU/UI
  -> bateria/teclado -> start normal/safe mode -> checkpoint. Em IDF 6, C6 e
  SD compartilham controlador; não paralelizar/reordenar para reduzir boot sem
  prova de compatibilidade elétrica, de ownership e de recuperação.
- [x] Tratar falha de mount SD como comportamento atual **fatal**
  (`ESP_ERROR_CHECK` antes da UI). Boot degradado sem SD exige requisito e
  desenho aprovados; não prometer que já funciona nem testar remoção em uso
  como procedimento padrão.
- [x] Não adicionar uma tabela universal de operações, quotas de heap por app
  ou quotas de CPU por app sem um consumidor e mecanismo real de enforcement.
- [x] Revisar ordem de locks, chamadas bloqueantes e dependências de lifecycle
  nos recursos compartilhados; adicionar testes determinísticos para riscos
  observados (deadlock, starvation, callback stale, teardown incompleto).

**Aceite:** cada recurso crítico tem dono e contrato observável; exceções de
camada são explícitas; limites vêm de headers/configuração de produção; testes
protegem comportamento e não apenas a presença de texto.

**Validação:** contratos host focados e suíte host completa. Build ESP-IDF se
houver alteração de firmware.

### Fase 3 — Medição, otimização reversível e compatibilidade no hardware

**Objetivo:** provar que os limites e a recuperação funcionam no dispositivo
real, não apenas em shims host.

**Em andamento:** cenários e riscos em
[`OS-PHASE3-BASELINE.pt-BR.md`](OS-PHASE3-BASELINE.pt-BR.md). A imagem
instrumentada `1.6.0` tem SHA-256 de build, flash verificado, boot normal
observado, séries a 1 Hz de heap/stack serial e bateria, carga HTTP moderada e
I/O descartável no SD. Faltam atestação independente da imagem em execução,
stack de outras tasks, orçamento aprovado, paridade de apps/transientes e
recuperação sob falhas reais. Não reduzir limites nem declarar aceite físico
agregado a partir deste recorte. Ensaios antigos do operador seguem históricos,
sem transferência automática de aceite à nova imagem.
Na inspeção do player foi detectado que `ui.type` em foreground injeta Enter e
pode iniciar áudio; o roteiro foi corrigido e a consulta read-only
`music.status` foi acrescentada à ponte Serial-JTAG. Sua inspeção física
sem playback foi observada na imagem `1.6.0` de hash diferente: 60/60 amostras
de estado/heap a 1 Hz, sem acionamento do decoder. Latência de scan, stack do
worker, playback e budgets continuam pendentes; não classificar a tentativa
anterior (com Enter involuntário) como inspeção passiva nem aceite de música.
Para observar o retorno de apps em foreground sem acionar Enter, a ponte tem
`ui.escape` (única tecla, guardada quando a tela está apagada). Seu ACK não
comprova teardown; confirmar o retorno por `term.dump` e uptime no Tab5.

- [x] Definir cenários representativos e de estresse a partir da Fase 1:
  boot/recovery, shell/editor/SD, troca de foreground, Wi-Fi/SSH/HTTP/time sync,
  BLE, netdiag, áudio/player e bateria/poweroff, conforme aplicável. Separar
  observação física segura de injeção de falha potencialmente destrutiva.
- [ ] Medir heap livre/mínimo, high-water mark de stack, latência de boot e
  operações, contadores de overflow e erros de I/O com resolução adequada ao
  transiente. Registrar série temporal quando o valor oscilar.
- [ ] Coletar baseline **antes** de mudar código: tempo por estágio de boot já
  instrumentado em `app_main`, responsividade LVGL/teclado, throughput/latência
  SD, transições foreground, conectividade e playback (incluindo perdas do
  trace). Distinguir custo de render, I/O, rede e codec por evidência; não
  presumir gargalo por tamanho do módulo.
- [ ] Priorizar somente gargalos demonstrados. Formular hipótese e métrica;
  modificar um caminho por vez mantendo limites e interfaces; repetir o mesmo
  cenário após a alteração e fazer rollback **apenas do próprio experimento**
  se houver regressão. Não reduzir stacks, filas, timeouts ou checkpoints por
  intuição nem introduzir buffers ilimitados.
- [ ] Validar falha de rede e timeouts de serviços com injeção segura. Falha
  de SD antes do boot, remoção em uso, corrupção de arquivo, poweroff e carga
  real da bateria exigem cenário e autorização específicos, backup quando
  houver risco de dados e estratégia de recuperação; primeiro usar shims
  determinísticos. Não repetir panic antes de analisar a evidência. A matriz
  host de time sync/netdiag/Wi-Fi em
  [`OS-PHASE3-BASELINE.pt-BR.md`](OS-PHASE3-BASELINE.pt-BR.md) foi exercitada;
  a recuperação física na imagem atual continua pendente.
- [ ] Derivar budgets aprovados dos dados e só então fixá-los em configuração,
  código e contratos; manter documentação sincronizada com a fonte de verdade.

**Aceite:** limites de OS-TAB5-04/05 atendidos, sem reboot/panic inesperado,
sem corrupção observada e com comportamento degradado documentado **onde
implementado**. OS-TAB5-07/08 exige paridade de fluxos e ganho mensurado sem
regressão. Toda validação registra firmware/commit, porta, comandos, uptime,
logs e configuração/periféricos conectados. Pendência física mantém o critério
em aberto, não impede registro de testes host verdes.

**Validação:** testes host, `verify`, `idf.py build` e validação física conforme
`AGENTS.md` e `tests/manual/serial-bridge-validation.pt-BR.md`.

### Fase 4 — Modelo de ameaça e endurecimento proporcional

**Objetivo:** decidir controles de segurança a partir de exposição e impacto
reais do Tab5.

**Inventário preliminar:** [`OS-PHASE4-THREAT-MODEL.pt-BR.md`](OS-PHASE4-THREAT-MODEL.pt-BR.md)
registra ativos, fronteiras, controles existentes e riscos não cobertos sem
alterar firmware. Escolha de controles, risco aceito, recuperação e validação
do build/dispositivo permanecem pendentes; não antecipar OS-TAB5-06.

- [x] Documentar ativos, interfaces de entrada, acesso físico, serviços LAN,
  credenciais/configuração e processo de atualização.
- [x] Separar ameaças cobertas por confinamento/grants cooperativos das que
  exigiriam isolamento de hardware ou autenticação.
- [ ] Avaliar Secure Boot, criptografia de flash, proteção de credenciais,
  autenticação de endpoints e estratégia de atualização; cada controle exige
  requisito, impacto operacional e método de recuperação.
- [ ] Se código não confiável ou apps independentes forem requisito, produzir
  uma decisão arquitetural separada comparando PMP/syscall/loader com adoção de
  plataforma (por exemplo, Zephyr/NuttX). Não iniciar kernel próprio como
  consequência implícita deste plano.

**Aceite:** declarações de segurança não excedem os controles realmente
habilitados; riscos aceitos e não cobertos são explícitos; configuração e
recuperação foram validadas no dispositivo.

### Fase 5 — Release e manutenção

**Objetivo:** estabelecer que cada versão liberada do firmware corresponde a
uma configuração e evidência operacional reproduzíveis.

- [ ] Executar gates host, contratos e build definidos em `AGENTS.md`.
- [ ] Validar no Tab5 os critérios físicos afetados pela mudança e registrar
  hash/versão do firmware e resultados.
- [ ] Atualizar `code-map.md`, arquitetura, rastreabilidade e documentação de
  validação no mesmo trabalho.
- [ ] Incrementar a versão apenas segundo a política do `AGENTS.md`; não criar
  tag sem solicitação explícita.

**Aceite:** qualidade, build e validação física registrados; pendências são
marcadas como pendentes, nunca inferidas a partir de testes host.

## 5. Fora de escopo enquanto as premissas forem válidas

- Kernel cyberdeck5 próprio, scheduler proprietário ou substituição do
  FreeRTOS.
- `fork`/`exec`, loader ELF, plugins nativos ou execução de scripts do SD.
- PMP por app, syscalls próprias ou sandbox contra apps maliciosas.
- Multiusuário, permissões POSIX ou compatibilidade com Linux.
- Event bus global, reply universal ou abstrações sem caso de uso do Tab5.
- Portabilidade para outros dispositivos — isso exige rever BSP, drivers e
  critérios de produto.

## 6. Gatilhos para reabrir arquitetura

Reabrir o plano antes de implementar se surgir qualquer requisito de app de
terceiros/não confiável, instalação independente de apps, isolamento de falha
entre apps, multiusuário, outra classe de hardware ou API de sistema que o
ESP-IDF/BSP não possa fornecer. A decisão deve comparar custo de estender a
plataforma atual com adoção de um RTOS/OS existente.

## 7. Gates e rastreabilidade

Gates gerais do repositório, conforme `AGENTS.md`:

```bash
make -C tests/host/keymap test
make -C tests/host/keymap verify
source /home/moises/esp/esp-idf/export.sh && idf.py build
```

O gate físico depende da mudança e segue o roteiro Serial-JTAG. Atualizar a
matriz abaixo à medida que os requisitos da Fase 1 forem aprovados:

| Requisito | Módulo/dono | Teste host/contrato | Validação Tab5 | Estado |
| --- | --- | --- | --- | --- |
| OS-TAB5-01..04/07: boot, recovery, console e Serial-JTAG | `main/app_main.cpp`, system apps, runtime, shell e bridge; matriz detalhada em `docs/OS-TAB5-REQUIREMENTS.pt-BR.md` | `test_boot_sequence`, `test_recovery_policy`, `test_recovery_runtime`, `test_app_runtime`, `test_shell_app`, `test_serial_ndjson_dispatch`, `test_serial_term_dump` e contratos complementares na matriz | Roteiro Serial-JTAG, após confirmar firmware e segurança | OS-TAB5-01/02 aceitos como documentação; validação física 04/07 pendente |
| OS-TAB5-01/02/07: conectividade e apps | `cyberdeck_system_apps.cpp`, manifests e adapters por app no Recorte 2 de `docs/OS-TAB5-REQUIREMENTS.pt-BR.md` | `test_app_runtime` (boot/demo/foreground) e testes por módulo listados na matriz | Conforme risco, periféricos e autorização; identificar imagem antes de executar | Contratos iniciais; Tab5 pendente |
| OS-TAB5-01/02/07: periféricos, SD e poweroff | `app_main.cpp`, adapters de display/bateria/screen-off/poweroff, shell/storage; Recorte 3 de `docs/OS-TAB5-REQUIREMENTS.pt-BR.md` | Testes de bateria, display, proteção de tela, SD e poweroff listados na matriz | Apenas cenários seguros aprovados; poweroff não executado nesta entrega | OS-TAB5-01/02 aceitos como documentação; aceite integrado/07 no Tab5 pendente |
| OS-TAB5-03..08: formatos, limites, falhas físicas e desempenho | Baseline de formatos/timeout nos requisitos, inventário de recursos em `docs/OS-RESOURCE-CONTRACTS.pt-BR.md` | `test_os_resource_contract` e gates por módulo; medições ainda não coletadas | Conforme risco/escopo e segurança | Contratos host da Fase 2 concluídos; OS-TAB5-03 requer verificação de fronteiras por módulo; validações físicas/medidas pendentes para fases 3–5 |

## 8. Relação com os demais documentos

- `docs/OS-TRANSFORMATION-PLAN.pt-BR.md`: decisões e histórico das fases já
  implementadas.
- `docs/OS-MATURITY-ASSESSMENT.pt-BR.md`: análise de responsabilidades segundo
  Tanenbaum e limites do escopo Tab5.
- `docs/OS-TAB5-REQUIREMENTS.pt-BR.md`: matriz e três recortes operacionais da
  Fase 1; registra criticidade, cenários, limites/timeout por fonte, formatos
  persistidos e observáveis, recuperação e gates físicos sem aceite físico
  implícito.
- `docs/OS-RESOURCE-CONTRACTS.pt-BR.md`: auditoria de fronteiras, limites,
  ownership, overflow, locks e lifecycle da Fase 2; evidencia e lacunas físicas.
- `docs/OS-IMPLEMENTATION-DASHBOARD.html`: projeção visual dos marcos deste
  plano; deve ser sincronizada com o estado real das entregas e aceites.
- `docs/ARCHITECTURE.pt-BR.md`: arquitetura concreta e fluxo de boot atuais.
- `AGENTS.md`: gates, versão e processo obrigatório de validação física.
