# Avaliação como Sistema Operacional Embarcado

Análise qualitativa de como as responsabilidades discutidas por Tanenbaum se
distribuem no produto integrado do M5Stack Tab5 e na camada cyberdeck5. Não é
uma pontuação de conformidade nem uma medição percentual. Retrato de
2026-11-05: `version.txt` = `1.5.0`, build local configurado com ESP-IDF 6.1
(`build/build.ninja`), alvo ESP32-P4 e BSP M5Stack Tab5. Esses dados não provam
que a mesma imagem esteja instalada no dispositivo.

## 1. Critério de análise

Tanenbaum apresenta o sistema operacional tanto como **máquina estendida**,
oferecendo abstrações convenientes sobre o hardware, quanto como
**gerenciador de recursos**, coordenando CPU, memória, dispositivos e arquivos.
Processos, escalonamento, sincronização/IPC, memória, arquivos, E/S e proteção
são temas centrais para estudar essas responsabilidades.

Essa lista não significa que todo SO tenha que reimplementar todos os
mecanismos. A avaliação deve perguntar quem fornece o serviço no sistema
integrado e se ele atende aos requisitos do produto. Memória virtual, loader
dinâmico, múltiplos usuários e compatibilidade POSIX, por exemplo, não são
requisitos automáticos de um dispositivo dedicado.

Neste caso há duas fronteiras relevantes:

| Fronteira | O que está sendo avaliado |
| --- | --- |
| **Produto integrado Tab5** | Bootloader + FreeRTOS + ESP-IDF + BSP + runtime/userland cyberdeck5. Esta é a unidade adequada para avaliar o produto como SO embarcado. |
| **Camada cyberdeck5** | Políticas, abstrações e serviços implementados pelo projeto, distintos dos fornecidos por FreeRTOS, ESP-IDF e BSP. Não é um kernel próprio. |

## 2. Composição e responsabilidades

| Tema de Tanenbaum | Responsável no produto integrado | Contribuição do cyberdeck5 |
| --- | --- | --- |
| Máquina estendida | ESP-IDF/LVGL + cyberdeck5 | TUI, shell, apps, namespace, window manager e serviços específicos do Tab5 |
| Tasks e escalonamento | FreeRTOS | Define e coordena workers, prioridades e lifecycle de serviços; apps não são processos escalonáveis independentes |
| Memória | ESP-IDF/FreeRTOS e hardware | Buffers bounded e quotas para alguns recursos; sem espaço de endereçamento ou heap isolado por app |
| Arquivos e armazenamento | VFS/FAT do ESP-IDF e BSP | Política de namespace, confinamento, operações permitidas e coordenação de I/O |
| Dispositivos e drivers | BSP M5Stack Tab5 e ESP-IDF | Adapters, portas, coordenação e teardown dos serviços |
| IPC e sincronização | Primitivas FreeRTOS + contratos cyberdeck5 | Filas bounded, tokens, backpressure, descarte stale e ownership explícito |
| Proteção entre apps | Não há isolamento contra código malicioso | Grants/capabilities cooperativas, apropriadas às apps integradas e confiáveis |
| Boot e recuperação | Bootloader ESP-IDF + política do produto | Startup dos serviços, safe mode, recuperação persistente e diagnóstico |

Evidências para a camada própria:
`components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h`
(manifestos, grants e lifecycle),
`components/cyberdeck/include/apps/shell/cyberdeck_vfs_namespace.h`,
`components/cyberdeck/include/apps/runtime/cyberdeck_window_manager.h` e
`main/app_main.cpp` (composição do boot). O plano de transformação registra os
contratos e o estado das fases em `OS-TRANSFORMATION-PLAN.pt-BR.md`.

### Inventário confirmado no código

`components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp` registra 12
apps: `cyberdeck.event_log`, `cyberdeck.shell`, `cyberdeck.wifi`,
`cyberdeck.serial`, `cyberdeck.ssh`, `cyberdeck.screenshot`,
`cyberdeck.bluetooth`, `cyberdeck.time_sync`, `cyberdeck.demo`,
`cyberdeck.editor`, `cyberdeck.netdiag` e `cyberdeck.music`. A lista de registro
não significa que todas estejam ativas ao mesmo tempo: `runtime::start_all()`
ignora apps `foreground`; o console shell permanece protegido ao trocar o
foreground (editor, netdiag ou música). Shell, serviços e apps devem permanecer
funcionais em qualquer melhoria de boot ou desempenho. `poweroff`, bateria,
IMU, proteção de tela, teclado e UI também são funções reais do firmware, mesmo
quando não são entradas separadas em `k_apps`.

Os grants são revogáveis e as quotas de `cyberdeck_app_quota.h` limitam
requisições declaradas (stack, profundidade de fila, leitura, logging, output),
**não** o consumo efetivo de heap ou CPU. O budget de `lifecycle_timeout_ms`
é registrado depois do retorno do hook; não o interrompe. Em falha de teardown,
`runtime::stop_index()` retém o estado e o grant para permitir retry seguro.

## 3. Avaliação por subsistema

### Processos e apps

O runtime mantém apps compiladas, dependências, lifecycle, start/stop/restart,
grants e diagnóstico. Isso é supervisão de módulos de firmware, não um modelo
de processos isolados: uma app pode ser chamada no fluxo do shell e serviços
bloqueantes podem possuir tasks FreeRTOS. Não há loader dinâmico nem processos
com espaço de endereçamento próprio. Para o conjunto fechado de apps do Tab5,
isso é uma escolha válida; vira requisito se houver extensões independentes ou
código não confiável.

### CPU, concorrência e IPC

O escalonamento preemptivo e SMP são fornecidos por FreeRTOS. O cyberdeck5
coordena serviços por tasks, filas bounded, tokens e contratos de teardown. Não
há IPC genérico como pipes/sinais ou bus universal. Para um conjunto pequeno e
conhecido de serviços, canais explícitos podem ser mais simples e verificáveis.
O plano futuro deve medir latência, starvation e contenção nos fluxos reais, e
não criar um bus por semelhança com SOs de propósito geral.

### Memória e isolamento

Alocadores C/C++ e ESP-IDF fornecem heap interno e PSRAM. Não há memória por
processo nem proteção entre apps; a flag PMP do ESP32-P4 não equivale a uma
política de isolamento do runtime. A ausência de memória virtual não desqualifica
um sistema embarcado. Para o Tab5, o requisito prático é medir margens de heap e
stack, picos e falhas de alocação sob cenários representativos. Isolamento por
hardware só se justifica caso o modelo de ameaça ou a distribuição de apps mude.

### Arquivos e dispositivos

O FAT/VFS e os drivers são do ESP-IDF/BSP; cyberdeck5 acrescenta um namespace e
políticas de confinamento para o shell, incluindo limites e operações restritas.
Não possuir `chmod`, uid, montagem por processo ou driver model próprio não é
defeito automático para um dispositivo de função fixa. Devem ser validados os
requisitos relevantes: comportamento sem SD, recuperação de escrita
interrompida, integridade, capacidade, latência e erros de dispositivos Tab5.

### Chamadas e proteção

Apps e SDK são compilados no mesmo firmware e não atravessam uma fronteira de
syscalls/modos de privilégio. Grants são controles cooperativos, não sandbox.
Isso é aceitável sob a premissa de firmware controlado e apps confiáveis; não
deve ser apresentado como proteção contra código hostil. Secure Boot,
criptografia de flash, autenticação e atualização segura devem ser decididos por
um modelo de ameaça do produto, não usados isoladamente como critério para
classificá-lo como SO.

### Boot e recuperação

O produto tem política própria de recuperação, safe mode, diagnóstico e
sequenciamento de serviços, combinada ao bootloader ESP-IDF. Essa é uma
contribuição relevante para confiabilidade embarcada e deve continuar sendo
validada no hardware real.

No código atual (`main/app_main.cpp`), a preparação do transporte ESP-Hosted
antecede o mount do SD: no IDF 6 os dois compartilham o controlador SDMMC e a
ordem preserva os slots C6/SD. Falha de `bsp_sdcard_mount*()` é tratada por
`ESP_ERROR_CHECK` antes da UI; **não existe boot degradado sem SD demonstrado**.
O boot prossegue com NVS/recovery, recuperação de sidecars sob o coordenador de
I/O, registro e logging, display/UI/IMU, bateria/teclado e serviços. Em modo
normal, a prontidão requer shell e ponte serial; serviços opcionais podem falhar
sem impedir o checkpoint. O safe mode inicia event log, shell e serial. Modificar
o mount, a ordem ou o critério de prontidão muda comportamento e exige decisão
e validação específica, não apenas otimização de tempo de boot.

## 4. Veredito

O **produto integrado** pode ser chamado de **sistema operacional embarcado
específico do M5Stack Tab5**: ele combina um kernel estabelecido (FreeRTOS),
serviços de plataforma (ESP-IDF/BSP) e o runtime/userland do cyberdeck5 para
oferecer uma máquina operacional coerente. A camada cyberdeck5, considerada
isoladamente, é runtime/userland sobre um kernel de terceiros — não um kernel
próprio.

Não há necessidade, no escopo atual, de implementar PMP por app, `ecall`,
syscalls próprias, loader ELF, filesystem POSIX ou drivers próprios. Esses itens
são **gatilhos de reavaliação**, não tarefas pendentes por padrão:

- executar apps de terceiros, plugins nativos ou conteúdo não confiável;
- exigir isolamento de falhas ou segurança entre apps;
- suportar atualização independente de apps;
- portar o produto para outra classe de hardware;
- exigir semântica de arquivos, usuários ou APIs não coberta pelo VFS atual.

Se um desses requisitos aparecer, deve-se comparar extensão do stack atual com
adoção de um RTOS/plataforma com os mecanismos necessários, antes de iniciar um
kernel próprio.

## 5. Correções da análise preliminar

- Removidas notas e percentuais como “78%/22%”: não tinham escala ou método
  quantitativo e misturavam o produto integrado com a autoria do cyberdeck5.
- “SASOS” não é sinônimo de funil de APIs; não é a arquitetura atual nem uma
  recomendação necessária para o Tab5.
- Ausência de loader, múltiplos usuários, syscalls próprias ou drivers próprios
  foi reclassificada como limite deliberado, salvo mudança dos requisitos acima.
- Secure Boot, criptografia de flash e stack checking são controles de segurança
  e robustez; não definem sozinhos se o produto integrado é um SO.
- A analogia com MINIX 3 foi removida como equivalência: cyberdeck5 não possui
  microkernel nem isolamento equivalente.

## 6. Referências internas

- `docs/OS-TRANSFORMATION-PLAN.pt-BR.md`: arquitetura, decisões, fases e limites.
- `docs/OS-IMPLEMENTATION-PLAN.pt-BR.md`: sequência proposta para consolidar
  requisitos, evidências e validação específica do Tab5.
- `docs/ARCHITECTURE.pt-BR.md`: arquitetura e fluxo de boot atuais.
- `components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h`: contrato
  do runtime de apps, grants e manifestos.
- `components/cyberdeck/include/apps/shell/cyberdeck_vfs_namespace.h`: namespace
  virtual do shell.
