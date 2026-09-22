# Host-side unit tests: cyberdeck5 (keymap, shell utils, historico local,
# parser de eventos Character do teclado,
# modelo puro da linha de edicao/sessao SSH, coordenador do teardown
# Wi-Fi/SSH com gating dos timers, guarda incremental do eco remoto do SSH e
# composicao local/remota da linha SSH).
#
# Sem dependencias externas alem de g++/make: compila os fontes de producao
# (components/cyberdeck/src/platform/input/tab5_keyboard_keys.cpp,
# components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp,
# cyberdeck_history.cpp e components/cyberdeck/src/platform/input/tab5_keyboard_event.cpp)
# contra um shim da API LVGL (shim/lvgl.h) - exceto historico e parser de
# eventos, que sao logica pura sem LVGL - e roda as assercoes.
#
# Targets:
#   make test   -> verifica paridade com o LVGL real e executa os testes
#   make verify -> compara shim/lvgl.h com o LVGL gerenciado (drift check)
#   make clean

CXX      ?= g++
PYTHON   ?= python3
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2

KEYMAP_SRC       := ../../../components/cyberdeck/src/platform/input/tab5_keyboard_keys.cpp
SHELL_UTILS_SRC  := ../../../components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp
HISTORY_SRC      := ../../../components/cyberdeck/src/features/shell/cyberdeck_history.cpp
KEYBOARD_EVENT_SRC := ../../../components/cyberdeck/src/platform/input/tab5_keyboard_event.cpp
EVENT_LOG_RECENT_SRC := ../../../components/cyberdeck/src/platform/logging/event_log_recent.cpp
TERMINAL_FILTER_SRC := ../../../components/cyberdeck/src/features/shell/cyberdeck_terminal_filter.cpp
EDIT_LINE_SRC    := ../../../components/cyberdeck/src/features/shell/cyberdeck_edit_line.cpp
NET_COORDINATOR_SRC := ../../../components/cyberdeck/src/platform/networking/cyberdeck_net_coordinator.cpp
ECHO_GUARD_SRC   := ../../../components/cyberdeck/src/features/shell/cyberdeck_ssh_echo_guard.cpp
LINE_COMPOSER_SRC := ../../../components/cyberdeck/src/features/shell/cyberdeck_ssh_line_composer.cpp
CLOCK_SRC        := ../../../components/cyberdeck/src/platform/display/cyberdeck_clock.cpp
WIFI_INDICATOR_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_indicator.cpp
WIFI_ICON_SRC      := ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp
WIFI_MENU_SRC      := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_menu.cpp
WIFI_STATE_MACHINE_SRC := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_state_machine.cpp
WIFI_STATE_MACHINE_HDR := ../../../components/cyberdeck/include/features/wifi/cyberdeck_wifi_state_machine.h
WIFI_EVENT_DISPATCH_SRC := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_event_dispatch.cpp
WIFI_PERSISTENCE_QUEUE_SRC := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_persistence_queue.cpp
WIFI_PERSISTENCE_COORDINATOR_SRC := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_persistence_coordinator.cpp
INCLUDES        := -Ishim -I../../../components/cyberdeck/include

# LVGL gerenciado pelo ESP-IDF (fonte da verdade p/ valores de LV_KEY_*).
LVGL_MANAGED := ../../../managed_components/lvgl__lvgl
LVGL_GROUP_H := $(LVGL_MANAGED)/include/lvgl/core/lv_group.h

SCREENSHOT_BMP_SRC := ../../../components/cyberdeck/src/features/screenshot/screenshot_bmp.cpp
LOCAL_SHELL_SRC := ../../../components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp
LOCAL_SHELL_HDR := ../../../components/cyberdeck/include/features/shell/cyberdeck_local_shell.h

BINS := test_keymap test_shell_utils test_history test_keyboard_event test_event_log_recent test_wifi_indicator_state test_wifi_icon_layout test_wifi_menu test_wifi_state_machine test_clock_formatter test_terminal_filter test_edit_line test_net_coordinator test_ssh_echo_guard test_ssh_line_composer test_screenshot_bmp test_wifi_event_dispatch test_wifi_persistence_queue test_wifi_persistence_coordination test_local_shell

.PHONY: all test verify clean test_boot_sequence test_keyboard_input_contract test_ui_resource_contract test_local_prompt_contract test_prompt_behavior test_wifi_dispatch_contract

all: test

# 1) Verifica (quando disponivel) que o shim nao divergeu do LVGL real.
verify:
	@if [ -f "$(LVGL_GROUP_H)" ]; then \
		echo "==> Comparando shim/lvgl.h com $(LVGL_GROUP_H)"; \
		TMPDIR=$$(mktemp -d); \
		awk '/LV_KEY_/{ \
		      if (match($$0, /LV_KEY_[A-Z0-9_]+[ ]*=[ ]*(0x[0-9a-fA-F]+|[0-9]+)/)) { \
		        split(substr($$0, RSTART, RLENGTH), a, /[ =]+/); print a[2], a[3] } }' \
		    "$(LVGL_GROUP_H)" | sort > "$$TMPDIR/lvgl_keys_real.txt"; \
		awk '/LV_KEY_/{ \
		      if (match($$0, /LV_KEY_[A-Z0-9_]+[ ]*=[ ]*(0x[0-9a-fA-F]+|[0-9]+)/)) { \
		        split(substr($$0, RSTART, RLENGTH), a, /[ =]+/); print a[2], a[3] } }' \
		    shim/lvgl.h | sort > "$$TMPDIR/lvgl_keys_shim.txt"; \
		if ! diff -u "$$TMPDIR/lvgl_keys_real.txt" "$$TMPDIR/lvgl_keys_shim.txt"; then \
			echo "ERRO: shim/lvgl.h divergiu do LVGL gerenciado. Atualize o shim."; \
			rm -rf "$$TMPDIR"; \
			exit 1; \
		fi; \
		rm -rf "$$TMPDIR"; \
		echo "OK: constantes LV_KEY_* identicas ao LVGL $(LVGL_MANAGED)."; \
	elif [ "$${STRICT_VERIFY:-0}" = "1" ]; then \
		echo "ERRO: $(LVGL_GROUP_H) nao encontrado e STRICT_VERIFY=1 esta ativo."; \
		exit 1; \
	else \
		echo "AVISO: $(LVGL_GROUP_H) nao encontrado; pulando verificacao de paridade."; \
	fi

# 2) Compila os binários de teste.
test_keymap: test_keymap.cpp $(KEYMAP_SRC) shim/lvgl.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_keymap.cpp $(KEYMAP_SRC) -o test_keymap

test_shell_utils: test_shell_utils.cpp $(SHELL_UTILS_SRC) shim/lvgl.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_shell_utils.cpp $(SHELL_UTILS_SRC) -o test_shell_utils

# Logica pura (sem LVGL): nao precisa de shim.
# O header entra como dependencia para que mudancas no contrato (ex.: limite)
# forcem recompilacao e a validacao do static_assert de paridade.
test_history: test_history.cpp $(HISTORY_SRC) ../../../components/cyberdeck/include/features/shell/cyberdeck_history.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_history.cpp $(HISTORY_SRC) -o test_history

# Logica pura (sem LVGL): nao precisa de shim.
# O parser e uma unidade pura de producao; o teste fixa o contrato de
# tab5_char_event_parse sem depender de LVGL, FreeRTOS ou hardware.
test_keyboard_event: test_keyboard_event.cpp $(KEYBOARD_EVENT_SRC) ../../../components/cyberdeck/include/platform/input/tab5_keyboard_event.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_keyboard_event.cpp $(KEYBOARD_EVENT_SRC) -o test_keyboard_event

# Logica pura extraida de event_log_latest(): selecao de indices do anel
# "recent" (contrato em event_log_recent.h, sem FreeRTOS/LVGL/SD).
# A implementacao e compilada junto ao teste para validar o contrato sem
# depender de FreeRTOS, LVGL ou acesso ao SD.
test_event_log_recent: test_event_log_recent.cpp $(EVENT_LOG_RECENT_SRC) ../../../components/cyberdeck/include/platform/logging/event_log_recent.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_event_log_recent.cpp $(EVENT_LOG_RECENT_SRC) -o test_event_log_recent

# Logica pura (sem LVGL): indicador de estado do Wi-Fi (contrato em
# cyberdeck_wifi_indicator.h; plano aprovado: o indicador eh UNICO icono,
# LIT somente quando enabled && connected && has_ip, APAGADO nos demais
# estados; o contrato NAO recebe parametro ssid). A implementacao pura e
# compilada junto ao teste para manter o contrato host-side independente do
# LVGL.
test_wifi_indicator_state: test_wifi_indicator_state.cpp $(WIFI_INDICATOR_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_indicator.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_indicator_state.cpp $(WIFI_INDICATOR_SRC) -o test_wifi_indicator_state

# Logica pura (sem LVGL): layout geometrico do icono Wi-Fi no
# caixote variavel do header (contrato em cyberdeck_wifi_icon.h;
# plano aprovado: tres arcos + ponto, raios/espessura/angulos
# validos, rejeicao de dimensoes invalidas, determinismo e
# estado lit separado reutilizando cyberdeck_wifi_indicator_is_lit).
# Cobre tambem a geometria pura de posicionamento com base na largura
# real do container: extensao visivel do arco determinante
# (visible_half_width / determinant_outer_radius), larguras
# validas/invalidas, determinismo byte a byte, monotonicidade de
# center_x (largura e margem), margem final ~2px e casos angulares.
test_wifi_icon_layout: test_wifi_icon_layout.cpp $(WIFI_ICON_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_icon.h ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_indicator.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_icon_layout.cpp $(WIFI_ICON_SRC) $(WIFI_INDICATOR_SRC) -o test_wifi_icon_layout

# Logica pura (sem LVGL): modelo e renderizacao do menu Wi-Fi (scan/saved/deduplicacao/navegacao).
test_wifi_menu: test_wifi_menu.cpp $(WIFI_MENU_SRC) ../../../components/cyberdeck/include/features/wifi/cyberdeck_wifi_menu.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_menu.cpp $(WIFI_MENU_SRC) -o test_wifi_menu

# Host-side contract coverage for the pure search/saved state machine:
# timeout/cancel recovery, stale/future callback tokens, UI lifecycle state,
# and persistence only after the IP event. No hardware, callbacks, or
# production adapters are faked; the production pure adapter is linked.
test_wifi_state_machine: test_wifi_state_machine.cpp $(WIFI_STATE_MACHINE_SRC) $(WIFI_STATE_MACHINE_HDR)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_state_machine.cpp $(WIFI_STATE_MACHINE_SRC) -o test_wifi_state_machine

# TDD contracts for the IP_EVENT crash fix. The producer must only copy a
# bounded snapshot and enqueue it; persistence/SNTP/UI work belongs to the
# consumer. These targets intentionally stay RED until the coder adds the
# production modules named below. No ESP-IDF or hardware is used here.
test_wifi_event_dispatch: test_wifi_event_dispatch.cpp $(WIFI_EVENT_DISPATCH_SRC) contracts/cyberdeck_wifi_event_dispatch.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_event_dispatch.cpp $(WIFI_EVENT_DISPATCH_SRC) -o test_wifi_event_dispatch

test_wifi_persistence_queue: test_wifi_persistence_queue.cpp $(WIFI_PERSISTENCE_QUEUE_SRC) $(WIFI_EVENT_DISPATCH_SRC) contracts/cyberdeck_wifi_persistence_queue.h contracts/cyberdeck_wifi_event_dispatch.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_persistence_queue.cpp $(WIFI_PERSISTENCE_QUEUE_SRC) $(WIFI_EVENT_DISPATCH_SRC) -o test_wifi_persistence_queue

# Contract for the worker coordination seam.  The coordinator is linked with
# both concrete bounded hand-off modules because its public implementation
# owns references to their types (and therefore cannot be linked in isolation).
test_wifi_persistence_coordination: test_wifi_persistence_coordination.cpp $(WIFI_PERSISTENCE_COORDINATOR_SRC) $(WIFI_PERSISTENCE_QUEUE_SRC) $(WIFI_EVENT_DISPATCH_SRC) contracts/cyberdeck_wifi_persistence_coordinator.h contracts/cyberdeck_wifi_persistence_queue.h contracts/cyberdeck_wifi_event_dispatch.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -Icontracts test_wifi_persistence_coordination.cpp $(WIFI_PERSISTENCE_COORDINATOR_SRC) $(WIFI_PERSISTENCE_QUEUE_SRC) $(WIFI_EVENT_DISPATCH_SRC) -o test_wifi_persistence_coordination


# Logica pura (sem LVGL): formatador do relogio do header (plano aprovado:
# clock "DD/MM/YYYY HH:MM" em GMT-3 no centro do header, NENHUM status SSH no
# header, indicador de Wi-Fi a direita; conversao UTC->local com offset fixo
# -180 min, formatacao estilo snprintf com zero-padding e entrada invalida
# retornando 0 sem escrever nada). O contrato vive em cyberdeck_clock.h; a
# implementacao (cyberdeck_clock.cpp) sera criada pelo coder junto com a
# integracao do relogio na UI. Ate la, este target fica vermelho por design
# (TDD): o build falha por MODULO AUSENTE, com a mensagem
# "No rule to make target '.../cyberdeck_clock.cpp'".
test_clock_formatter: test_clock_formatter.cpp $(CLOCK_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_clock.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_clock_formatter.cpp $(CLOCK_SRC) -o test_clock_formatter

# Logica pura (sem LVGL): filtro puro e incremental da saida SSH (contrato em
# cyberdeck_terminal_filter.h; plano aprovado: remover ANSI/CSI/OSC, normalizar
# CR/LF, preservar UTF-8/texto e suportar fragmentacao). A implementacao
# (cyberdeck_terminal_filter.cpp) sera criada pelo coder junto com a integracao
# no fluxo on_ssh_data()/append_output() do TUI. Ate la, este target fica
# vermelho por design (TDD): o build falha por MODULO AUSENTE, com a mensagem
# "No rule to make target '.../cyberdeck_terminal_filter.cpp'".
test_terminal_filter: test_terminal_filter.cpp $(TERMINAL_FILTER_SRC) ../../../components/cyberdeck/include/features/shell/cyberdeck_terminal_filter.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_terminal_filter.cpp $(TERMINAL_FILTER_SRC) -o test_terminal_filter

# Logica pura (sem LVGL): modelo da linha de edicao + sessao SSH (contrato em
# cyberdeck_edit_line.h; bug aprovado A: cada caractere aparece exatamente uma
# vez, entrada fisica/virtual com paridade, backspace/setas UTF-8 na posicao
# real do cursor e Enter que envia comando+newline uma unica vez e limpa a
# linha sem duplicacao). A implementacao (cyberdeck_edit_line.cpp) existe e os
# testes de payload passam; a regressao E (politica de eco explicita:
# cyberdeck_enter_result::echo — MENU ecoa "$ cmd\n", CONNECTED/PASSWORD/
# HOST_KEY nao ecoam) fica vermelha EM RUNTIME ate o coder preencher `echo`
# na implementacao (TDD; nenhuma implementacao fake e usada).
test_edit_line: test_edit_line.cpp $(EDIT_LINE_SRC) ../../../components/cyberdeck/include/features/shell/cyberdeck_edit_line.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_edit_line.cpp $(EDIT_LINE_SRC) -o test_edit_line

# Logica pura (sem LVGL): coordenador do teardown Wi-Fi/SSH e do gating dos
# timers (contrato em cyberdeck_net_coordinator.h; bug aprovado B: desconexao
# idempotente, offline por erro de socket notificado uma unica vez, callbacks
# inertes apos o teardown e timers que apenas SINALIZAM scan/retry sem
# executar APIs Wi-Fi pesadas dentro do callback). A implementacao
# (cyberdeck_net_coordinator.cpp) sera criada pelo coder junto com a
# integracao em wifi_mgr/ssh_client. Ate la, este target fica vermelho por
# design (TDD): o build falha por MODULO AUSENTE, com a mensagem
# "No rule to make target '.../cyberdeck_net_coordinator.cpp'".
test_net_coordinator: test_net_coordinator.cpp $(NET_COORDINATOR_SRC) ../../../components/cyberdeck/include/platform/networking/cyberdeck_net_coordinator.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_net_coordinator.cpp $(NET_COORDINATOR_SRC) -o test_net_coordinator

# Logica pura (sem LVGL): guarda incremental do eco remoto do SSH (contrato em
# cyberdeck_ssh_echo_guard.h; feature aprovada historico/eco remoto SSH:
# armar com o payload local "cmd\n", alimentar chunks JA FILTRADOS e suprimir
# apenas o eco que corresponde byte a byte; na ausencia de eco ou com eco
# incompleto, preservar a saida integralmente; suportar fragmentacao,
# divergencia, saida imediata, multiplos comandos, linha vazia, UTF-8 e fluxo
# pos-filtro). A implementacao (cyberdeck_ssh_echo_guard.cpp) sera criada pelo
# coder junto com a integracao em on_ssh_data(). Ate la, este target fica
# vermelho por design (TDD): o build falha por MODULO AUSENTE, com a mensagem
# "No rule to make target '.../cyberdeck_ssh_echo_guard.cpp'".
test_ssh_echo_guard: test_ssh_echo_guard.cpp $(ECHO_GUARD_SRC) ../../../components/cyberdeck/include/features/shell/cyberdeck_ssh_echo_guard.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_ssh_echo_guard.cpp $(ECHO_GUARD_SRC) -o test_ssh_echo_guard

# Logica pura (sem LVGL): composicao local/remota da linha SSH (contrato em
# cyberdeck_ssh_line_composer.h; plano aprovado de SERIACAO do envio SSH:
# begin() exibe o comando local SEM o newline, o separador '\n' e emitido
# exatamente uma vez entre o comando e a primeira saida remota — com/sem eco,
# eco quebrado, saida que comeca com newline, comando vazio, UTF-8 e Enter
# virtual sem duplicacao). O compositor mantem NO MAXIMO UM comando pendente
# (IDLE/PENDING): begin() adicional enquanto armado e recusado (retorna 0, nao
# escreve nada e preserva o casamento do eco); apos eco/divergencia/flush
# begin() volta a ser aceito. O contrato removeu o estado FIFO/late_echo
# (deques); a implementacao atual (cyberdeck_ssh_line_composer.cpp) ainda usa a
# politica FIFO antiga e por isso NAO compila — este target fica vermelho por
# design (TDD) ate o coder reimplementar o estado unico previsto no contrato.
test_ssh_line_composer: test_ssh_line_composer.cpp $(LINE_COMPOSER_SRC) ../../../components/cyberdeck/include/features/shell/cyberdeck_ssh_line_composer.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_ssh_line_composer.cpp $(LINE_COMPOSER_SRC) -o test_ssh_line_composer

# Logica pura (sem LVGL): API pura de geracao de BMP 24-bit top-down
# (contrato em screenshot_bmp.h; plano aprovado: cabecalho BMP,
# padding/stride, conversao RGB565->BGR888, chunking sem gaps/
# sobreposicao, validacoes de dimensoes/capacidade). A implementacao
# (screenshot_bmp.cpp) sera criada pelo coder. Ate la, este target
# fica vermelho por design (TDD): o build falha por MODULO AUSENTE,
# com a mensagem "No rule to make target '.../screenshot_bmp.cpp'".
test_screenshot_bmp: test_screenshot_bmp.cpp $(SCREENSHOT_BMP_SRC) ../../../components/cyberdeck/include/features/screenshot/screenshot_bmp.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_screenshot_bmp.cpp $(SCREENSHOT_BMP_SRC) -o test_screenshot_bmp

# Host test for the confined /sdcard local shell.  Include the production
# header directly: a duplicate test-only ABI here can make the test compile
# while linking a different class layout/API than the implementation.
test_local_shell: test_local_shell.cpp $(LOCAL_SHELL_SRC) $(LOCAL_SHELL_HDR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_local_shell.cpp $(LOCAL_SHELL_SRC) -Wl,--wrap=opendir -o test_local_shell

# Structural host-side TDD contract for the boot-loop fix.  app_main is not
# duplicated or linked against ESP-IDF: the test checks the real source order,
# call cardinality, handle guard, and the existing /sdcard terminal wiring.
test_boot_sequence: test_boot_sequence.py ../../../main/app_main.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) test_boot_sequence.py

# Structural host-side TDD contract for the physical keyboard/UI seam.  The
# UI is not linkable on host; this test checks the real source and intentionally
# remains RED until the bounded lv_async_call handoff is implemented.
test_keyboard_input_contract: test_keyboard_input_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) test_keyboard_input_contract.py

# Structural host-side contract for every partial cyberdeck_ui_init resource
# allocation path.  It checks complete cleanup and zeroing of all handles.
test_ui_resource_contract: test_ui_resource_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) test_ui_resource_contract.py

test_local_prompt_contract: local_prompt_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) local_prompt_contract.py

test_prompt_behavior: test_prompt_behavior.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) test_prompt_behavior.py

test_wifi_dispatch_contract: test_wifi_dispatch_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) test_wifi_dispatch_contract.py

wifi_enter_dispatch_contract: wifi_enter_dispatch_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) wifi_enter_dispatch_contract.py

test: $(BINS)
	@echo "==> Executando testes do keymap do shell de entrada"
	@./test_keymap
	@echo "==> Executando testes dos utilitarios de shell e SSH"
	@./test_shell_utils
	@echo "==> Executando testes do historico local do terminal"
	@./test_history
	@echo "==> Executando testes do parser de eventos Character do teclado"
	@./test_keyboard_event
	@echo "==> Executando testes da selecao de indices do anel recente do log"
	@./test_event_log_recent
	@echo "==> Executando testes do indicador de estado do Wi-Fi"
	@./test_wifi_indicator_state
	@echo "==> Executando testes do layout do icono Wi-Fi"
	@./test_wifi_icon_layout
	@echo "==> Executando testes do modelo e renderizacao do menu Wi-Fi"
	@./test_wifi_menu
	@echo "==> Executando testes da maquina de estados pura de busca/salvas Wi-Fi"
	@./test_wifi_state_machine
	@echo "==> Executando contrato TDD do produtor IP_EVENT"
	@./test_wifi_event_dispatch
	@echo "==> Executando contrato TDD da fila de persistencia Wi-Fi"
	@./test_wifi_persistence_queue
	@echo "==> Executando contrato TDD da coordenacao da persistencia Wi-Fi"
	@./test_wifi_persistence_coordination
	@echo "==> Executando testes do formatador do relogio do header (GMT-3)"
	@./test_clock_formatter
	@echo "==> Executando testes do filtro de saida SSH"
	@./test_terminal_filter
	@echo "==> Executando testes do modelo da linha de edicao/sessao SSH"
	@./test_edit_line
	@echo "==> Executando testes do coordenador de teardown Wi-Fi/SSH e timers"
	@./test_net_coordinator
	@echo "==> Executando testes da guarda incremental de eco remoto do SSH"
	@./test_ssh_echo_guard
	@echo "==> Executando testes da composicao local/remota da linha SSH"
	@./test_ssh_line_composer
	@echo "==> Executando testes da API de geracao de BMP 24-bit"
	@./test_screenshot_bmp
	@echo "==> Executando contrato do shell local seguro"
	@./test_local_shell
	@echo "==> Executando contrato estrutural da sequencia de boot SD"
	@$(MAKE) --no-print-directory test_boot_sequence
	@echo "==> Executando contrato TDD do despacho assincrono do teclado fisico"
	@$(MAKE) --no-print-directory test_keyboard_input_contract
	@echo "==> Executando contrato de cleanup parcial da inicializacao da UI"
	@$(MAKE) --no-print-directory test_ui_resource_contract
	@echo "==> Executando contrato do prompt local contextual"
	@$(MAKE) --no-print-directory test_local_prompt_contract
	@echo "==> Executando testes comportamentais de prompt, truncamento e cursor"
	@$(MAKE) --no-print-directory test_prompt_behavior
	@echo "==> Executando contrato do despacho e menus Wi-Fi"
	@$(MAKE) --no-print-directory test_wifi_dispatch_contract
	@echo "==> Executando regressao do Enter no menu Wi-Fi"
	@$(MAKE) --no-print-directory wifi_enter_dispatch_contract

clean:
	rm -f $(BINS)
