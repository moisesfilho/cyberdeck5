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
sincronos e nao podem ser preemptados, o estado agora reflete o retorno real do
hook e o estouro do orcamento fica apenas como diagnostico; o orcamento do Wi-Fi
foi elevado para 8 s. Regressao coberta por `test_app_runtime.cpp` (`slow_app` e
`slow_dependent_app`) e por `failing_app`/`failing_dependent_app`, que confirmam
que falha real continua fail-closed.

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

- [ ] Remover acessos diretos da UI aos servicos.
- [ ] Expor Wi-Fi, SSH, BLE e Serial por interfaces de servico.
- [ ] Fazer Screenshot consumir uma interface de display.
- [ ] Fazer o shell acessar storage, rede e input por portas do sistema.
- [ ] Converter callbacks externos em eventos bounded.

Execucao inicial da Fase 3: o acesso direto da UI ao backend `event_log` foi
removido. O logger agora e uma porta do runtime, implementada pelo supervisor
e consumida pela UI atraves de `runtime::app_logger()`. Os acessos diretos da
UI aos servicos Wi-Fi, SSH, BLE e Serial ainda precisam ser migrados para
portas equivalentes; esta fase permanece parcialmente concluida.

### 4. IPC e Event Bus

- [ ] Criar eventos tipados e bounded.
- [ ] Criar filas por aplicacao.
- [ ] Preservar tokens de geracao e descarte de eventos stale.
- [ ] Definir backpressure, overflow e timeout explicitamente.
- [ ] Proibir chamadas diretas entre tasks quando houver IPC aplicavel.

### 5. Modelo de Tasks

- [ ] Definir uma task principal por servico bloqueante.
- [ ] Declarar stack no manifesto.
- [ ] Associar fila e mutex ao lifecycle da aplicacao.
- [ ] Tornar start e stop idempotentes.
- [ ] Exigir join antes de destruir recursos.
- [ ] Integrar watchdog e limites de execucao.
- [ ] Manter operacoes pesadas fora da task LVGL e de callbacks ESP-IDF.

### 6. Sistema de Arquivos Virtual

- [ ] Consolidar o namespace virtual do dispositivo.
- [ ] Definir `/apps`, `/data`, `/dev`, `/tmp` e `/system`.
- [ ] Manter `/data` para dados persistentes do usuario.
- [ ] Manter `/system` para configuracoes e estado interno.
- [ ] Expor dispositivos por interfaces virtuais em `/dev`.
- [ ] Preservar confinamento, limites e protecao contra traversal e symlink.
- [ ] Manter execucao de binarios do SD fora do escopo inicial.

### 7. Shell como Userland

- [ ] Transformar o shell em uma aplicacao de primeiro plano completa.
- [ ] Fazer o prompt e a linha de comando pertencerem ao app shell.
- [ ] Fazer `app` consultar exclusivamente o supervisor.
- [ ] Expor servicos por contratos de comando sem interceptar comandos legados.
- [ ] Manter historico, edicao e sessoes isolados.
- [ ] Tratar SSH como modo de sessao do shell.
- [ ] Avaliar suporte futuro a multiplas sessoes ou consoles.

### 8. Window Manager LVGL

- [ ] Criar superficie principal por aplicacao.
- [ ] Centralizar foco e ownership de input.
- [ ] Criar barra de sistema persistente.
- [ ] Adicionar notificacoes e transicoes entre apps.
- [ ] Impedir acesso direto das aplicacoes a arvore LVGL.
- [ ] Expor uma API controlada de view/contexto.

### 9. Recursos e Capacidades

- [ ] Definir APIs controladas para display, input, storage e network.
- [ ] Definir APIs controladas para BLE, Serial-JTAG e Screenshot.
- [ ] Definir APIs controladas para event log, clock e bateria.
- [ ] Entregar a cada app somente os recursos declarados no manifesto.

### 10. Persistencia e Recuperacao

- [ ] Persistir estado de aplicacoes quando necessario.
- [ ] Criar recovery de boot apos falha.
- [ ] Registrar ultimo erro por aplicacao.
- [ ] Criar modo seguro contra loops de reinicializacao.
- [ ] Estruturar logs de lifecycle.
- [ ] Expor diagnostico por `sys.info` e `app info`.

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
