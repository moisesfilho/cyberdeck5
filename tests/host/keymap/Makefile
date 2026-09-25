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
#   make test_battery_contract test_battery_reader_contract test_battery_ui_contract
#                 -> contratos host para tensao real/estados neutros/UI e
#                    rastreabilidade; devem falhar ate a correcao do coder

CXX      ?= g++
PYTHON   ?= python3
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2

KEYMAP_SRC       := ../../../components/cyberdeck/src/platform/input/tab5_keyboard_keys.cpp
SHELL_UTILS_SRC  := ../../../components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp
SHELL_HELP_HDR   := ../../../components/cyberdeck/include/features/shell/cyberdeck_shell_help.h
HISTORY_SRC      := ../../../components/cyberdeck/src/features/shell/cyberdeck_history.cpp
KEYBOARD_EVENT_SRC := ../../../components/cyberdeck/src/platform/input/tab5_keyboard_event.cpp
EVENT_LOG_RECENT_SRC := ../../../components/cyberdeck/src/platform/logging/event_log_recent.cpp
TERMINAL_FILTER_SRC := ../../../components/cyberdeck/src/features/shell/cyberdeck_terminal_filter.cpp
EDIT_LINE_SRC    := ../../../components/cyberdeck/src/features/shell/cyberdeck_edit_line.cpp
NET_COORDINATOR_SRC := ../../../components/cyberdeck/src/platform/networking/cyberdeck_net_coordinator.cpp
ECHO_GUARD_SRC   := ../../../components/cyberdeck/src/features/shell/cyberdeck_ssh_echo_guard.cpp
LINE_COMPOSER_SRC := ../../../components/cyberdeck/src/features/shell/cyberdeck_ssh_line_composer.cpp
CLOCK_SRC        := ../../../components/cyberdeck/src/platform/display/cyberdeck_clock.cpp
SCREEN_PROTECTION_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp
SCREEN_PROTECTION_HDR := ../../../components/cyberdeck/include/platform/display/cyberdeck_screen_protection.h
SCREEN_PROTECTION_CONTRACT_HDR := contracts/cyberdeck_screen_protection.h
SCREEN_PROTECTION_TEST := test_screen_protection.cpp
SCREEN_PROTECTION_CONTRACT := test_screen_protection_contract.py
WIFI_INDICATOR_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_indicator.cpp
WIFI_ICON_SRC      := ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp
WIFI_ICON_HDR      := ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_icon.h
WIFI_MENU_SRC      := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_menu.cpp
WIFI_STATE_MACHINE_SRC := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_state_machine.cpp
WIFI_STATE_MACHINE_HDR := ../../../components/cyberdeck/include/features/wifi/cyberdeck_wifi_state_machine.h
WIFI_EVENT_DISPATCH_SRC := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_event_dispatch.cpp
WIFI_PERSISTENCE_QUEUE_SRC := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_persistence_queue.cpp
WIFI_PERSISTENCE_COORDINATOR_SRC := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_persistence_coordinator.cpp
WIFI_AUDIT_SRC := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_audit.cpp
WIFI_AUDIT_SAVE_TEST := test_wifi_audit_save.cpp
WIFI_AUDIT_SAVE_CONTRACT := test_wifi_audit_save_contract.py
WIFI_AUDIT_PERSISTENCE_SRC := ../../../components/cyberdeck/src/features/wifi/cyberdeck_wifi_audit_persistence.cpp
WIFI_AUDIT_PERSISTENCE_HDR := ../../../components/cyberdeck/include/features/wifi/cyberdeck_wifi_audit_persistence.h
WIFI_AUDIT_PERSISTENCE_CONTRACT_HDR := contracts/cyberdeck_wifi_audit_persistence.h
ORIENTATION_SRC := ../../../components/cyberdeck/src/platform/sensors/orientation.cpp
BATTERY_PURE_SRC := ../../../components/cyberdeck/src/platform/sensors/battery_status.cpp
BATTERY_PURE_HDR := ../../../components/cyberdeck/include/platform/sensors/battery_status.h
BATTERY_CONTRACT_HDR := contracts/cyberdeck_battery.h
BATTERY_READER_SRC := ../../../components/cyberdeck/src/platform/sensors/ina226_reader.cpp
BATTERY_READER_HDR := ../../../components/cyberdeck/include/platform/sensors/ina226_reader.h
BATTERY_ADAPTER_SRC := ../../../components/cyberdeck/src/platform/sensors/battery_protection.cpp
BATTERY_ADAPTER_HDR := ../../../components/cyberdeck/include/platform/sensors/battery_protection.h
BATTERY_PROTECTION_SRC := ../../../components/cyberdeck/src/platform/sensors/cyberdeck_battery_protection.cpp
BATTERY_PROTECTION_HDR := ../../../components/cyberdeck/include/platform/sensors/cyberdeck_battery_protection.h
BATTERY_PROTECTION_CONTRACT_HDR := contracts/cyberdeck_battery_protection.h
BATTERY_PROTECTION_BIN := test_battery_protection_bin
BATTERY_PROTECTION_CONTRACT := test_battery_protection_contract.py
BATTERY_BIN       := test_battery_contract
SERIAL_BRIDGE_SRC := ../../../components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp
SERIAL_BRIDGE_HDR := ../../../components/cyberdeck/include/features/serial/cyberdeck_serial_bridge.h
INCLUDES        := -Ishim -I../../../components/cyberdeck/include

# LVGL gerenciado pelo ESP-IDF (fonte da verdade p/ valores de LV_KEY_*).
LVGL_MANAGED := ../../../managed_components/lvgl__lvgl
LVGL_GROUP_H := $(LVGL_MANAGED)/include/lvgl/core/lv_group.h

SCREENSHOT_BMP_SRC := ../../../components/cyberdeck/src/features/screenshot/screenshot_bmp.cpp
LOCAL_SHELL_SRC := ../../../components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp
LOCAL_SHELL_HDR := ../../../components/cyberdeck/include/features/shell/cyberdeck_local_shell.h
CAT_MULTILINE_SRC := test_cat_multiline.cpp
HELP_UNIFICATION_TEST := test_help_unification.cpp
HELP_UNIFICATION_CONTRACT := help_unification_contract.py

BINS := test_keymap test_shell_utils test_history test_keyboard_event test_event_log_recent test_wifi_indicator_state test_wifi_icon_layout test_wifi_menu test_wifi_state_machine test_clock_formatter test_screen_protection test_terminal_filter test_edit_line test_net_coordinator test_ssh_echo_guard test_ssh_line_composer test_screenshot_bmp test_wifi_event_dispatch test_wifi_persistence_queue test_wifi_persistence_coordination test_local_shell test_cat_multiline test_help_unification test_orientation test_wifi_audit test_wifi_audit_persistence test_wifi_audit_save test_serial_ndjson_dispatch test_serial_screen_dump test_serial_cli_tolerance test_serial_sysinfo_wifi test_fs_write_dispatch $(BATTERY_BIN) $(BATTERY_PROTECTION_BIN)

.PHONY: all test verify clean test_boot_sequence imu_sensor_contract test_keyboard_input_contract test_ui_resource_contract test_local_prompt_contract test_prompt_behavior test_wifi_dispatch_contract test_wifi_enter_routing_contract test_wifi_audit_contract test_wifi_audit_persistence test_wifi_audit_persistence_contract test_wifi_audit_save test_wifi_audit_save_contract test_cat_contract test_cat_multiline_contract test_cat_lifecycle_sanitization_contract test_cat_start_teardown_start_contract test_cat_stack_footprint_contract test_cat_worker_path_safety_contract local_shell_security_contract local_shell_tokenizer_contract help_unification_contract test_serial_ndjson_dispatch test_serial_screen_dump test_serial_cli_tolerance test_serial_sysinfo_wifi test_fs_write_dispatch test_fs_write_cli test_fs_write_contract test_battery_contract test_battery_reader_contract test_battery_ui_contract test_battery_protection test_battery_protection_contract test_screen_protection test_screen_protection_contract

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

test_shell_utils: test_shell_utils.cpp contracts/cyberdeck_help.h $(SHELL_UTILS_SRC) shim/lvgl.h
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

test_wifi_audit: test_wifi_audit.cpp $(WIFI_AUDIT_SRC) ../../../components/cyberdeck/include/features/wifi/cyberdeck_wifi_audit.h $(WIFI_AUDIT_PERSISTENCE_HDR)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_audit.cpp $(WIFI_AUDIT_SRC) -o test_wifi_audit

# Host contract for the injectable Wi-Fi audit persistence transaction.
# The target links the real production header/source and exercises every
# behavioral fault, recovery, ACK, and concurrency case.
test_wifi_audit_persistence: test_wifi_audit_persistence.cpp $(WIFI_AUDIT_PERSISTENCE_SRC) $(WIFI_AUDIT_PERSISTENCE_HDR) $(WIFI_AUDIT_PERSISTENCE_CONTRACT_HDR)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_audit_persistence.cpp $(WIFI_AUDIT_PERSISTENCE_SRC) -pthread -o test_wifi_audit_persistence

test_wifi_audit_persistence_contract: test_wifi_audit_persistence_contract.py $(WIFI_AUDIT_PERSISTENCE_CONTRACT_HDR)
	$(PYTHON) test_wifi_audit_persistence_contract.py

# Simplified Wi-Fi audit save contract: the ordinary audit is display-only;
# the explicit save path owns directory/timestamp/collision behavior.
test_wifi_audit_save: $(WIFI_AUDIT_SAVE_TEST) $(WIFI_AUDIT_SRC) $(WIFI_AUDIT_PERSISTENCE_SRC) $(CLOCK_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_clock.h
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(WIFI_AUDIT_SAVE_TEST) $(WIFI_AUDIT_SRC) $(WIFI_AUDIT_PERSISTENCE_SRC) $(CLOCK_SRC) -pthread -o test_wifi_audit_save

test_wifi_audit_save_contract: $(WIFI_AUDIT_SAVE_CONTRACT) ../../../components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp ../../../components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(WIFI_AUDIT_SRC) $(WIFI_AUDIT_PERSISTENCE_SRC) $(WIFI_AUDIT_PERSISTENCE_HDR) ../../../components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp ../../../code-map.md ../../../.gitignore
	$(PYTHON) $(WIFI_AUDIT_SAVE_CONTRACT)


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

# TDD RED: politica/estado puro da protecao de tela. O contrato fixa o default
# de 2 min, a faixa 0..1440, zero desabilitando sem perder o ultimo valor
# positivo, snapshots NVS e as transicoes exatas do timer. A implementacao de
# producao esperada e cyberdeck_screen_protection.cpp; ate o coder cria-la, o
# alvo falha por fonte ausente sem tocar em LVGL/BSP/NVS/hardware.
test_screen_protection: $(SCREEN_PROTECTION_TEST) $(SCREEN_PROTECTION_SRC) $(SCREEN_PROTECTION_HDR) $(SCREEN_PROTECTION_CONTRACT_HDR)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include $(SCREEN_PROTECTION_TEST) $(SCREEN_PROTECTION_SRC) -o test_screen_protection

# TDD RED: contrato estrutural para comando/UI, timer, NVS e ui.type. Nao
# executa display, Serial Automation Bridge, simulador ou power-cycle.
test_screen_protection_contract: $(SCREEN_PROTECTION_CONTRACT) $(SCREEN_PROTECTION_SRC) $(SCREEN_PROTECTION_HDR) ../../../components/cyberdeck/src/platform/display/screen_off.cpp ../../../components/cyberdeck/include/platform/display/screen_off.h ../../../components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp ../../../components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp ../../../main/app_main.cpp ../../../components/cyberdeck/CMakeLists.txt ../../../code-map.md ../../../.gitignore
	$(PYTHON) $(SCREEN_PROTECTION_CONTRACT)

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

# Host regression for the dedicated cat API.  This deliberately bypasses the
# generic shell object so a multiline result is checked at the worker's source
# seam, including CRLF, tabs, UTF-8, exact 12288-byte output, embedded NUL,
# and a suffix after the first newline for the UI sanitizer.
test_cat_multiline: $(CAT_MULTILINE_SRC) $(LOCAL_SHELL_SRC) $(LOCAL_SHELL_HDR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(CAT_MULTILINE_SRC) $(LOCAL_SHELL_SRC) -o test_cat_multiline

# RED: the menu and local shell must render one shared help catalog.  The
# target links both real production implementations; no production edit is
# implied by this test registration.
test_help_unification: $(HELP_UNIFICATION_TEST) contracts/cyberdeck_help.h $(SHELL_UTILS_SRC) $(SHELL_HELP_HDR) ../../../components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h $(LOCAL_SHELL_SRC) $(LOCAL_SHELL_HDR) shim/lvgl.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(HELP_UNIFICATION_TEST) $(SHELL_UTILS_SRC) $(LOCAL_SHELL_SRC) -o test_help_unification

# RED structural companion: rejects private literal help lists in the two
# shells and in the non-linkable LVGL UI.  It performs source inspection only.
help_unification_contract: $(HELP_UNIFICATION_CONTRACT) contracts/cyberdeck_help.h $(SHELL_UTILS_SRC) $(SHELL_HELP_HDR) $(LOCAL_SHELL_SRC) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h ../../../code-map.md Makefile
	$(PYTHON) $(HELP_UNIFICATION_CONTRACT)

test_orientation: test_orientation.cpp $(ORIENTATION_SRC) ../../../components/cyberdeck/include/platform/sensors/orientation.h shim/lvgl.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_orientation.cpp $(ORIENTATION_SRC) -o test_orientation

# TDD RED: contrato puro de bateria. A API de teste exige tensao real
# (bus_voltage_mv, 6000..8230 mV), estados charging/discharging/neutral,
# zero/indeterminado como neutral e ausencia somente por presenca explicita;
# a producao correspondente ainda precisa da correcao do coder.
test_battery_contract: test_battery_contract.cpp $(BATTERY_PURE_SRC) $(BATTERY_PURE_HDR) $(BATTERY_CONTRACT_HDR)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include test_battery_contract.cpp $(BATTERY_PURE_SRC) -o $(BATTERY_BIN)
	@./$(BATTERY_BIN)

# TDD RED: contrato estrutural do reader INA226, sem I2C/simulador/hardware;
# tambem verifica o estado neutral, boot nao fatal, ausencia explicita,
# integracao 0x41 e a rastreabilidade REQ-BAT-006.
test_battery_reader_contract: test_battery_reader_contract.py $(BATTERY_READER_SRC) $(BATTERY_READER_HDR) $(BATTERY_ADAPTER_SRC) $(BATTERY_ADAPTER_HDR) $(BATTERY_PURE_SRC) $(BATTERY_PURE_HDR) $(BATTERY_CONTRACT_HDR) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../main/app_main.cpp ../../../components/cyberdeck/CMakeLists.txt ../../../code-map.md ../../../docs/ARCHITECTURE.md ../../../docs/ARCHITECTURE.pt-BR.md Makefile
	$(PYTHON) test_battery_reader_contract.py

# TDD RED: contrato estrutural do header/UI, incluindo neutral sem glyph/texto,
# boot nao fatal, ausencia explicita e a regressão de espaçamento (largura Wi-Fi
# compacta, flex grow zero, alinhamento final e gaps pequenos). A UI deve
# mostrar percentual numerico e um unico icone semantico nos estados ativos,
# sem qualquer icone de nivel.
test_battery_ui_contract: test_battery_ui_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(WIFI_ICON_SRC) $(WIFI_ICON_HDR) ../../../components/cyberdeck/include/platform/sensors/battery_status.h $(BATTERY_CONTRACT_HDR) ../../../main/app_main.cpp ../../../code-map.md ../../../docs/ARCHITECTURE.md ../../../docs/ARCHITECTURE.pt-BR.md Makefile
	$(PYTHON) test_battery_ui_contract.py

# Pure host contract for battery state/protection policy.  It links the real
# production policy and keeps the public target separate from its executable
# name, avoiding a circular make dependency while remaining hardware-free.
test_battery_protection: $(BATTERY_PROTECTION_BIN)
	@./$(BATTERY_PROTECTION_BIN)

$(BATTERY_PROTECTION_BIN): test_battery_protection.cpp $(BATTERY_PROTECTION_CONTRACT_HDR) $(BATTERY_PROTECTION_SRC) $(BATTERY_PROTECTION_HDR)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include test_battery_protection.cpp $(BATTERY_PROTECTION_SRC) -o $@

# Structural companion for expander-B, NVS, UI timer/snapshot, shell and
# ui.type.  It reads production source and fails closed when the feature is
# absent; it never opens hardware or a simulator.
test_battery_protection_contract: $(BATTERY_PROTECTION_CONTRACT) $(BATTERY_PROTECTION_CONTRACT_HDR) $(BATTERY_ADAPTER_SRC) $(BATTERY_ADAPTER_HDR) ../../../components/cyberdeck/CMakeLists.txt ../../../main/app_main.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp ../../../components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h ../../../components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp ../../../tests/host/keymap/test_serial_ndjson_dispatch.cpp ../../../code-map.md ../../../.gitignore ../../../docs/ARCHITECTURE.md ../../../docs/ARCHITECTURE.pt-BR.md Makefile
	$(PYTHON) $(BATTERY_PROTECTION_CONTRACT)

# Structural host-side contract for boot and initial IMU orientation. The
# sources are inspected directly rather than duplicated or linked against
# ESP-IDF: this checks boot order, safe initial fallback, and the handoff to
# the later rotation path.
test_boot_sequence: test_boot_sequence.py ../../../main/app_main.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(LOCAL_SHELL_HDR)
	$(PYTHON) test_boot_sequence.py

imu_sensor_contract: imu_sensor_contract.py ../../../components/cyberdeck/src/platform/sensors/imu_reader.cpp
	$(PYTHON) imu_sensor_contract.py

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

test_cat_contract: cat_contract.py ../../../components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp ../../../components/cyberdeck/include/features/shell/cyberdeck_cat_worker.h
	$(PYTHON) cat_contract.py

test_cat_multiline_contract: test_cat_multiline_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp
	$(PYTHON) test_cat_multiline_contract.py

test_cat_lifecycle_sanitization_contract: test_cat_lifecycle_sanitization_contract.py ../../../components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp ../../../components/cyberdeck/include/features/shell/cyberdeck_cat_worker.h
	$(PYTHON) test_cat_lifecycle_sanitization_contract.py

test_cat_start_teardown_start_contract: test_cat_start_teardown_start_contract.py ../../../components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp
	$(PYTHON) test_cat_start_teardown_start_contract.py

test_cat_stack_footprint_contract: test_cat_stack_footprint_contract.py ../../../components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp
	$(PYTHON) test_cat_stack_footprint_contract.py

test_cat_worker_path_safety_contract: test_cat_worker_path_safety_contract.py ../../../components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp ../../../components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp ../../../components/cyberdeck/include/features/shell/cyberdeck_local_shell.h
	$(PYTHON) test_cat_worker_path_safety_contract.py

local_shell_security_contract: local_shell_security_contract.py ../../../components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp
	$(PYTHON) local_shell_security_contract.py

local_shell_tokenizer_contract: local_shell_tokenizer_contract.py ../../../components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp ../../../components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp ../../../components/cyberdeck/include/features/shell/cyberdeck_cat_worker.h $(LOCAL_SHELL_HDR)
	$(PYTHON) local_shell_tokenizer_contract.py

test_prompt_behavior: test_prompt_behavior.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(LOCAL_SHELL_SRC) $(LOCAL_SHELL_HDR)
	$(PYTHON) test_prompt_behavior.py

test_wifi_dispatch_contract: test_wifi_dispatch_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) test_wifi_dispatch_contract.py

wifi_enter_dispatch_contract: wifi_enter_dispatch_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) wifi_enter_dispatch_contract.py

test_wifi_enter_routing_contract: test_wifi_enter_routing_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) test_wifi_enter_routing_contract.py

test_wifi_audit_contract: test_wifi_audit_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(WIFI_AUDIT_SRC) $(WIFI_AUDIT_PERSISTENCE_SRC) $(WIFI_AUDIT_PERSISTENCE_HDR) $(WIFI_AUDIT_PERSISTENCE_CONTRACT_HDR) ../../../sdkconfig.defaults ../../../components/cyberdeck/CMakeLists.txt ../../../code-map.md
	$(PYTHON) test_wifi_audit_contract.py

# USB Serial-JTAG NDJSON manual bridge (REQ-002/003/005/006/007/008/009) — TDD RED: produção ausente
test_serial_ndjson_dispatch: test_serial_ndjson_dispatch.cpp $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_serial_ndjson_dispatch.cpp $(SERIAL_BRIDGE_SRC) -o test_serial_ndjson_dispatch

test_serial_screen_dump: test_serial_screen_dump.cpp $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) $(SCREENSHOT_BMP_SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_serial_screen_dump.cpp $(SERIAL_BRIDGE_SRC) $(SCREENSHOT_BMP_SRC) -o test_serial_screen_dump

test_serial_cli_tolerance: test_serial_cli_tolerance.cpp $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_serial_cli_tolerance.cpp $(SERIAL_BRIDGE_SRC) -o test_serial_cli_tolerance

test_serial_sysinfo_wifi: test_serial_sysinfo_wifi.cpp $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_serial_sysinfo_wifi.cpp $(SERIAL_BRIDGE_SRC) -o test_serial_sysinfo_wifi

# fs.write seguro (REQ-001..REQ-011) — TDD RED: produção ausente
test_fs_write_dispatch: test_fs_write_dispatch.cpp $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_fs_write_dispatch.cpp $(SERIAL_BRIDGE_SRC) -o test_fs_write_dispatch

test_fs_write_cli: test_fs_write_cli.py ../../../tools/cyberdeck_cli.py
	$(PYTHON) test_fs_write_cli.py

test_fs_write_contract: test_fs_write_contract.py $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) ../../../tools/cyberdeck_cli.py
	$(PYTHON) test_fs_write_contract.py

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
	@echo "==> Executando contrato TDD da auditoria Wi-Fi passiva"
	@./test_wifi_audit
	@echo "==> Executando contrato TDD do fluxo simplificado de salvamento da auditoria Wi-Fi"
	@./test_wifi_audit_save
	@echo "==> Executando contrato estrutural do salvamento da auditoria Wi-Fi"
	@$(MAKE) --no-print-directory test_wifi_audit_save_contract
	@echo "==> Executando contrato TDD da persistencia injetavel da auditoria Wi-Fi"
	@$(MAKE) --no-print-directory test_wifi_audit_persistence_contract
	@echo "==> Executando binario comportamental da persistencia da auditoria Wi-Fi"
	@./test_wifi_audit_persistence
	@echo "==> Executando testes do formatador do relogio do header (GMT-3)"
	@./test_clock_formatter
	@echo "==> Executando contrato puro de politica/estado da protecao de tela"
	@./test_screen_protection
	@echo "==> Executando contrato estrutural da protecao de tela/NVS/ui.type"
	@$(MAKE) --no-print-directory test_screen_protection_contract
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
	@echo "==> Executando contrato de unificacao do help"
	@./test_help_unification
	@$(MAKE) --no-print-directory help_unification_contract
	@echo "==> Executando regressao host da API cat multiline"
	@./test_cat_multiline
	@echo "==> Executando testes de orientacao pura e debounce"
	@./test_orientation
	@echo "==> Executando contrato estrutural da sequencia de boot SD"
	@$(MAKE) --no-print-directory test_boot_sequence
	@echo "==> Executando contrato TDD do despacho assincrono do teclado fisico"
	@$(MAKE) --no-print-directory test_keyboard_input_contract
	@echo "==> Executando contrato de cleanup parcial da inicializacao da UI"
	@$(MAKE) --no-print-directory test_ui_resource_contract
	@echo "==> Executando contrato do prompt local contextual"
	@$(MAKE) --no-print-directory test_local_prompt_contract
	@echo "==> Executando contrato TDD do cat local confinado e assincrono"
	@$(MAKE) --no-print-directory test_cat_contract
	@$(MAKE) --no-print-directory test_cat_multiline_contract
	@$(MAKE) --no-print-directory test_cat_lifecycle_sanitization_contract
	@$(MAKE) --no-print-directory test_cat_start_teardown_start_contract
	@$(MAKE) --no-print-directory test_cat_stack_footprint_contract
	@$(MAKE) --no-print-directory test_cat_worker_path_safety_contract
	@$(MAKE) --no-print-directory local_shell_security_contract
	@$(MAKE) --no-print-directory local_shell_tokenizer_contract
	@echo "==> Executando testes comportamentais de prompt, truncamento e cursor"
	@$(MAKE) --no-print-directory test_prompt_behavior
	@echo "==> Executando contrato do despacho e menus Wi-Fi"
	@$(MAKE) --no-print-directory test_wifi_dispatch_contract
	@$(MAKE) --no-print-directory test_wifi_audit_contract
	@$(MAKE) --no-print-directory test_wifi_enter_routing_contract
	@echo "==> Executando regressao do Enter no menu Wi-Fi"
	@$(MAKE) --no-print-directory wifi_enter_dispatch_contract
	@echo "==> Executando contrato TDD da ponte manual USB Serial-JTAG NDJSON (REQ-002/003/005)"
	@$(MAKE) --no-print-directory test_serial_ndjson_dispatch
	@echo "==> Executando contrato TDD do screen.dump byte-identical com chunks/CRC/end (REQ-006/007)"
	@$(MAKE) --no-print-directory test_serial_screen_dump
	@echo "==> Executando contrato TDD da CLI tolerante a logs/leitura fragmentada (REQ-007/008)"
	@$(MAKE) --no-print-directory test_serial_cli_tolerance
	@echo "==> Executando contrato TDD de sys.info/wifi sem hardware (REQ-008/009)"
	@$(MAKE) --no-print-directory test_serial_sysinfo_wifi
	@echo "==> Executando contrato TDD do fs.write seguro (REQ-001..REQ-011)"
	@$(MAKE) --no-print-directory test_fs_write_dispatch
	@$(MAKE) --no-print-directory test_fs_write_cli
	@$(MAKE) --no-print-directory test_fs_write_contract
	@echo "==> Executando contratos de bateria (tensao/estados/UI)"
	@./$(BATTERY_BIN)
	@$(MAKE) --no-print-directory test_battery_reader_contract
	@$(MAKE) --no-print-directory test_battery_ui_contract
	@echo "==> Executando contrato TDD de estados/protecao de bateria"
	@$(MAKE) --no-print-directory test_battery_protection
	@$(MAKE) --no-print-directory test_battery_protection_contract
	# Keep the known IMU contract last so unrelated task tests run first;
	# it remains a separate, non-masked gate.
	@echo "==> Executando contrato IMU preexistente isolado (fora do escopo)"
	@$(MAKE) --no-print-directory imu_sensor_contract

clean:
	rm -f $(BINS) test_battery_protection
