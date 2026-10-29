# Host-side unit tests: cyberdeck5 (keymap, shell utils, historico local,
# parser de eventos Character do teclado,
# modelo puro da linha de edicao/sessao SSH, coordenador do teardown
# Wi-Fi/SSH com gating dos timers, guarda incremental do eco remoto do SSH e
# composicao local/remota da linha SSH).
#
# Sem dependencias externas alem de g++/make: compila os fontes de producao
# (components/cyberdeck/src/platform/input/tab5_keyboard_keys.cpp,
# components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp,
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
# Build remains serial unless the caller supplies -jN; do not override its
# jobserver because the aggregate runs coverage-producing scenarios in order.

KEYMAP_SRC       := ../../../components/cyberdeck/src/platform/input/tab5_keyboard_keys.cpp
SHELL_UTILS_SRC  := ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp
# Fase 7: console puro (prompt/linha + politica de despacho) e a aplicacao de
# primeiro plano que possui o console.  Ambos linkam no host sem LVGL/ESP-IDF
# quando ligado com os mesmos shims do harness de sessao.
SHELL_CONSOLE_SRC := ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp
SHELL_CONSOLE_HDR := ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_console.h
SHELL_APP_SRC     := ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_app.cpp
SHELL_APP_HDR     := ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_app.h
SHELL_HELP_HDR   := ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h
# Fase 8: politica pura do window manager (superficies, foco e notificacoes) e
# o adaptador LVGL dono da raiz composta (screen + barra de sistema + area de
# conteudo).  Os dois TUs linkam no host contra shim/full/lvgl.h, entao ambos
# entram no gcovr e nao podem estar na allowlist de hardware do scope guard.
WINDOW_MANAGER_SRC := ../../../components/cyberdeck/src/apps/runtime/cyberdeck_window_manager.cpp
WINDOW_MANAGER_HDR := ../../../components/cyberdeck/include/apps/runtime/cyberdeck_window_manager.h
WINDOW_MANAGER_ADAPTER_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_window_manager_adapter.cpp
WINDOW_MANAGER_ADAPTER_HDR := ../../../components/cyberdeck/include/platform/display/cyberdeck_window_manager_adapter.h
WINDOW_MANAGER_CONTRACT := test_window_manager_contract.py
HISTORY_SRC      := ../../../components/cyberdeck/src/apps/shell/cyberdeck_history.cpp
KEYBOARD_EVENT_SRC := ../../../components/cyberdeck/src/platform/input/tab5_keyboard_event.cpp
EVENT_LOG_RECENT_SRC := ../../../components/cyberdeck/src/platform/logging/event_log_recent.cpp
TERMINAL_FILTER_SRC := ../../../components/cyberdeck/src/apps/shell/cyberdeck_terminal_filter.cpp
TERMINAL_FILTER_HDR := ../../../components/cyberdeck/include/apps/shell/cyberdeck_terminal_filter.h
TERMINAL_OUTPUT_CONTRACT := test_terminal_output_contract.py
TERMINAL_SCROLLBACK_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_terminal_scrollback.cpp
TERMINAL_SCROLLBACK_HDR := ../../../components/cyberdeck/include/platform/display/cyberdeck_terminal_scrollback.h
LAT03_CONTRACT := test_lat03_contract.py
LAT04_CONTRACT := test_lat04_contract.py
SSH_OUTPUT_GAP_TEST := test_ssh_output_gap.py
EDIT_LINE_SRC    := ../../../components/cyberdeck/src/apps/shell/cyberdeck_edit_line.cpp
NET_COORDINATOR_SRC := ../../../components/cyberdeck/src/platform/networking/cyberdeck_net_coordinator.cpp
ECHO_GUARD_SRC   := ../../../components/cyberdeck/src/apps/shell/cyberdeck_ssh_echo_guard.cpp
LINE_COMPOSER_SRC := ../../../components/cyberdeck/src/apps/shell/cyberdeck_ssh_line_composer.cpp
CLOCK_SRC        := ../../../components/cyberdeck/src/platform/display/cyberdeck_clock.cpp
SCREEN_PROTECTION_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp
SCREEN_PROTECTION_HDR := ../../../components/cyberdeck/include/platform/display/cyberdeck_screen_protection.h
SCREEN_PROTECTION_CONTRACT_HDR := contracts/cyberdeck_screen_protection.h
SCREEN_PROTECTION_TEST := test_screen_protection.cpp
SCREEN_PROTECTION_CONTRACT := test_screen_protection_contract.py
SCREEN_PROTECTION_BIN := test_screen_protection_bin
SCREEN_PROTECTION_OBJS := test_screen_protection/test.o test_screen_protection/protection.o
WIFI_INDICATOR_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_indicator.cpp
WIFI_ICON_SRC      := ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp
WIFI_ICON_HDR      := ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_icon.h
WIFI_MENU_SRC      := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_menu.cpp
WIFI_STATE_MACHINE_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_state_machine.cpp
WIFI_STATE_MACHINE_HDR := ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_state_machine.h
WIFI_EVENT_DISPATCH_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_event_dispatch.cpp
WIFI_PERSISTENCE_QUEUE_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_persistence_queue.cpp
WIFI_PERSISTENCE_COORDINATOR_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_persistence_coordinator.cpp
WIFI_AUDIT_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_audit.cpp
WIFI_AUDIT_HDR := ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_audit.h
WIFI_AUDIT_BIN := test_wifi_audit_bin
WIFI_AUDIT_OBJS := test_wifi_audit/test.o test_wifi_audit/audit.o
TERMINAL_SCROLLBACK_BIN := test_terminal_scrollback_bin
TERMINAL_SCROLLBACK_OBJS := test_terminal_scrollback/test.o test_terminal_scrollback/scrollback.o
WIFI_SURVEY_BIN := test_wifi_survey_bin
WIFI_SURVEY_OBJS := test_wifi_survey/test.o test_wifi_survey/survey.o
RECOVERY_POLICY_BIN := test_recovery_policy_bin
RECOVERY_POLICY_OBJS := test_recovery_policy/test.o test_recovery_policy/policy.o
RECOVERY_RUNTIME_SRC := ../../../components/cyberdeck/src/apps/system/cyberdeck_recovery.cpp
RECOVERY_RUNTIME_POLICY_SRC := ../../../components/cyberdeck/src/apps/system/cyberdeck_recovery_policy.cpp
WIFI_AUDIT_SAVE_TEST := test_wifi_audit_save.cpp
WIFI_AUDIT_SAVE_CONTRACT := test_wifi_audit_save_contract.py
WIFI_AUDIT_PERSISTENCE_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_audit_persistence.cpp
WIFI_AUDIT_PERSISTENCE_HDR := ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_audit_persistence.h
WIFI_AUDIT_PERSISTENCE_CONTRACT_HDR := contracts/cyberdeck_wifi_audit_persistence.h
WIFI_SURVEY_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_survey.cpp
WIFI_SURVEY_HDR := ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_survey.h
WIFI_SURVEY_EXPORT_CONTROLLER_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_survey_export_controller.cpp
WIFI_SURVEY_EXPORT_CONTROLLER_HDR := ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_survey_export_controller.h
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
# Indicador de energia removivel (REQ-BAT-UI-001..005 / AC-BAT-UI-001..006).
# A view pura do header e host-linkable: o alvo liga cyberdeck_battery_view.cpp
# com a politica pura cyberdeck_battery_protection.cpp para provar a costura
# snapshot -> apresentacao (precedencia de ausencia, sinal CHG_STAT invalido,
# mapeamento total e clamp de percentual) sem ESP-IDF, FreeRTOS, LVGL, I2C,
# NVS, BSP, simulador, Serial Automation Bridge ou hardware.
BATTERY_VIEW_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_battery_view.cpp
BATTERY_VIEW_HDR := ../../../components/cyberdeck/include/platform/display/cyberdeck_battery_view.h
BATTERY_VIEW_CONTRACT_HDR := contracts/cyberdeck_battery_view.h
BATTERY_VIEW_TEST := test_battery_view.cpp
BATTERY_VIEW_BIN := test_battery_view_bin
BATTERY_VIEW_CONTRACT := test_battery_view_contract.py
SD_IO_COORDINATOR_SRC := ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp
SD_IO_COORDINATOR_HDR := ../../../components/cyberdeck/include/platform/storage/cyberdeck_sd_io_coordinator.h
SD_IO_COORDINATOR_TEST := test_sd_io_coordinator.cpp
SD_PACKAGE_HDR := ../../../components/cyberdeck/include/apps/runtime/cyberdeck_sd_package.h
# Binary artifacts are kept distinct from the .PHONY run targets (same pattern
# as BATTERY_*_BIN): the aggregate builds the binaries through BINS exactly
# once and the run target executes them once, so a clean checkout cannot call a
# missing/stale executable and `make clean` removes what the aggregate created.
SD_PACKAGE_BIN := test_sd_package_bin
RESOURCE_CATALOG_HDR := ../../../components/cyberdeck/include/apps/runtime/cyberdeck_resource_catalog.h
RESOURCE_CATALOG_BIN := test_resource_catalog_bin
SERIAL_BRIDGE_SRC := ../../../components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp
SERIAL_BRIDGE_HDR := ../../../components/cyberdeck/include/apps/serial/cyberdeck_serial_bridge.h
# BLE (REQ-BLE-001..011 / AC-BLE-001..011).  The radio lives on the ESP32-C6
# coprocessor reached through esp_hosted, so the host suite only ever links the
# four pure modules plus the host-linkable shell parser.  The contracts are
# test-owned ABI fixtures under contracts/ and the production headers must
# expose exactly the same symbols.
BLE_TYPES_SRC     := ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_types.cpp
BLE_STATE_SRC     := ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_state_machine.cpp
BLE_EVENTS_SRC    := ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_event_dispatch.cpp
BLE_STORE_SRC     := ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_store.cpp
BLE_TYPES_HDR     := ../../../components/cyberdeck/include/apps/bluetooth/cyberdeck_ble_types.h
BLE_STATE_HDR     := ../../../components/cyberdeck/include/apps/bluetooth/cyberdeck_ble_state_machine.h
BLE_EVENTS_HDR    := ../../../components/cyberdeck/include/apps/bluetooth/cyberdeck_ble_event_dispatch.h
BLE_STORE_HDR     := ../../../components/cyberdeck/include/apps/bluetooth/cyberdeck_ble_store.h
BLE_MGR_SRC       := ../../../components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp
BLE_MGR_HDR       := ../../../components/cyberdeck/include/apps/bluetooth/ble_mgr.h
BLE_BACKGROUND_SRC := ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_background.cpp
BLE_BACKGROUND_HDR := ../../../components/cyberdeck/include/apps/bluetooth/cyberdeck_ble_background.h
BLE_TYPES_CONTRACT_HDR     := contracts/cyberdeck_ble_types.h
BLE_STATE_CONTRACT_HDR     := contracts/cyberdeck_ble_state_machine.h
BLE_EVENTS_CONTRACT_HDR    := contracts/cyberdeck_ble_event_dispatch.h
BLE_STORE_CONTRACT_HDR     := contracts/cyberdeck_ble_store.h
NETDIAG_CONTROLLER_SRC := ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_controller.cpp
NETDIAG_CONTROLLER_HDR := ../../../components/cyberdeck/include/apps/network/cyberdeck_netdiag_controller.h
NETDIAG_DNS_ADAPTER_SRC := ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_dns_adapter.cpp
NETDIAG_DNS_ADAPTER_HDR := ../../../components/cyberdeck/include/apps/network/cyberdeck_netdiag_dns_adapter.h
NETDIAG_TCP_ADAPTER_SRC := ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_tcp_adapter.cpp
NETDIAG_TCP_ADAPTER_HDR := ../../../components/cyberdeck/include/apps/network/cyberdeck_netdiag_tcp_adapter.h
NETDIAG_HTTPS_ADAPTER_SRC := ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_https_adapter.cpp
NETDIAG_HTTPS_ADAPTER_HDR := ../../../components/cyberdeck/include/apps/network/cyberdeck_netdiag_https_adapter.h
NETDIAG_APP_SRC := ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_app.cpp
NETDIAG_APP_HDR := ../../../components/cyberdeck/include/apps/network/cyberdeck_netdiag_app.h
INCLUDES        := -Ishim -I../../../components/cyberdeck/include

# LVGL gerenciado pelo ESP-IDF (fonte da verdade p/ valores de LV_KEY_*).
LVGL_MANAGED := ../../../managed_components/lvgl__lvgl
LVGL_GROUP_H := $(LVGL_MANAGED)/include/lvgl/core/lv_group.h

SCREENSHOT_BMP_SRC := ../../../components/cyberdeck/src/apps/screenshot/screenshot_bmp.cpp
VFS_NAMESPACE_SRC := ../../../components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp
VFS_NAMESPACE_HDR := ../../../components/cyberdeck/include/apps/shell/cyberdeck_vfs_namespace.h
LOCAL_SHELL_SRC = ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp $(VFS_NAMESPACE_SRC)
SESSION_SRC     := ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp
SESSION_HDR     := ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_session.h
APP_RUNTIME_SRC := ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp
APP_RUNTIME_HDR := ../../../components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h
RESOURCE_CATALOG_SRC := ../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp
SD_PACKAGE_SRC := ../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp
APP_QUOTA_SRC := ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp
APP_STORAGE_SRC := ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_storage.cpp
APP_LOGGER_SRC := ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_logger.cpp
COMMAND_CATALOG_SRC := ../../../components/cyberdeck/src/apps/runtime/cyberdeck_command_catalog.cpp
RECOVERY_POLICY_SRC := ../../../components/cyberdeck/src/apps/system/cyberdeck_recovery_policy.cpp
RECOVERY_POLICY_HDR := ../../../components/cyberdeck/include/apps/system/cyberdeck_recovery_policy.h
PHASE10_CONTRACT := test_phase10_contract.py
APP_FACADES_SRC := ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_facades.cpp
APP_FACADES_HDR := ../../../components/cyberdeck/include/apps/runtime/cyberdeck_app_facades.h
PHASE9_CAPABILITIES_TEST := test_phase9_capabilities.cpp
# O contrato do limite de payload da saida SSH e derivado do produtor, entao os
# dois testes do seam dependem deste TU.
SSH_CLIENT_SRC := ../../../components/cyberdeck/src/apps/ssh/ssh_client.cpp
DEMO_APP_SRC := ../../../components/cyberdeck/src/apps/demo/cyberdeck_demo_app.cpp
DEMO_APP_HDR := ../../../components/cyberdeck/include/apps/demo/cyberdeck_demo_app.h
LOCAL_SHELL_HDR := ../../../components/cyberdeck/include/apps/shell/cyberdeck_local_shell.h
LOCAL_SHELL_NAMESPACE_CONTRACT := local_shell_namespace_contract.py
CAT_MULTILINE_SRC := test_cat_multiline.cpp
SHELL_TEST       := test_shell_utils.cpp
SERIAL_TEST      := test_serial_ndjson_dispatch.cpp
CAT_WORKER_SRC   := ../../../components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp
BLE_BACKGROUND_SRC := ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_background.cpp
KEYBOARD_DISPATCH_SRC := ../../../components/cyberdeck/src/platform/input/cyberdeck_keyboard_dispatch.cpp
HEADER_VIEW_SRC  := ../../../components/cyberdeck/src/platform/display/cyberdeck_header_view.cpp
TERMINAL_VIEW_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp
EDITOR_VIEW_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_editor_view.cpp
SCOPE_GUARD_TEST := test_coverage_scope_guard.py
HELP_UNIFICATION_TEST := test_help_unification.cpp
HELP_UNIFICATION_CONTRACT := help_unification_contract.py

EVENT_LOG_RUNTIME_BIN := test_event_log_runtime_bin
SD_IO_COORDINATOR_BIN := test_sd_io_coordinator_bin
BATTERY_BIN := test_battery_contract_bin
BATTERY_VIEW_BIN := test_battery_view_bin
BLE_TYPES_BIN := test_ble_types_bin
BLE_STATE_BIN := test_ble_state_machine_bin
BLE_EVENTS_BIN := test_ble_event_dispatch_bin
BLE_STORE_BIN := test_ble_store_bin
BLE_COMMAND_BIN := test_ble_command_parse_bin
BINS := test_shell_help_bin test_shell_app_bin test_keymap_bin test_shell_utils_bin test_history_bin test_keyboard_event_bin test_event_log_recent_bin test_event_log_shim test_wifi_indicator_state_bin test_wifi_icon_layout_bin test_wifi_menu_bin test_wifi_state_machine_bin test_clock_formatter_bin test_screen_protection_bin test_terminal_filter_bin test_terminal_scrollback_bin test_edit_line_bin test_editor_model_bin test_net_coordinator_bin test_ssh_echo_guard_bin test_ssh_line_composer_bin test_screenshot_bmp_bin test_wifi_event_dispatch_bin test_wifi_persistence_queue_bin test_wifi_persistence_coordination_bin test_local_shell_bin test_save_sidecar_recovery test_cat_multiline_bin test_help_unification_bin test_orientation_bin $(WIFI_AUDIT_BIN) test_wifi_audit_persistence_bin test_wifi_audit_save_bin test_wifi_survey_bin test_wifi_survey_export_controller test_serial_ndjson_dispatch_bin test_serial_screen_dump_bin test_serial_cli_tolerance_bin test_serial_sysinfo_wifi_bin test_serial_term_dump_bin test_fs_write_dispatch_bin $(BLE_TYPES_BIN) $(BLE_STATE_BIN) $(BLE_EVENTS_BIN) $(BLE_STORE_BIN) $(BLE_COMMAND_BIN) test_shell_session_bin test_app_runtime_bin test_runtime_sdk_bin test_storage_facade_bin test_phase9_capabilities_bin test_cat_worker_bin test_ble_background_bin test_keyboard_dispatch_bin test_display_views_bin test_demo_app_bin test_window_manager_bin test_recovery_policy_bin test_recovery_runtime_bin test_time_sync_bin $(BATTERY_PROTECTION_BIN)
SAVE_SIDECAR_BIN := test_save_sidecar_recovery_bin
WIFI_SURVEY_EXPORT_BIN := test_wifi_survey_export_controller_bin
NETDIAG_CONTROLLER_BIN := test_netdiag_controller_bin
NETDIAG_TCP_BIN := test_netdiag_tcp_adapter_bin
NETDIAG_DNS_BIN := test_netdiag_dns_adapter_bin
NETDIAG_HTTPS_BIN := test_netdiag_https_adapter_bin
NETDIAG_APP_BIN := test_netdiag_app_bin
BINS := $(filter-out test_save_sidecar_recovery test_wifi_survey_export_controller test_netdiag_controller test_netdiag_tcp_adapter,$(BINS))
BINS += $(SAVE_SIDECAR_BIN) $(WIFI_SURVEY_EXPORT_BIN) $(NETDIAG_CONTROLLER_BIN) $(NETDIAG_TCP_BIN) $(NETDIAG_DNS_BIN) $(NETDIAG_HTTPS_BIN) $(NETDIAG_APP_BIN) $(EVENT_LOG_RUNTIME_BIN) $(SD_IO_COORDINATOR_BIN) $(BATTERY_BIN) $(BATTERY_VIEW_BIN) test_music_model_bin test_music_audio_output_bin test_music_app_bin test_music_manager_bin test_music_player_bin test_music_decoder_scratch_bin test_music_command_controller_bin test_lvgl_dispatch_state_bin

MUSIC_MODEL_SRC := ../../../components/cyberdeck/src/apps/music/cyberdeck_music_model.cpp
MUSIC_MODEL_HDR := ../../../components/cyberdeck/include/apps/music/cyberdeck_music_model.h
MUSIC_MODEL_TEST := test_music_model.cpp
MUSIC_MODEL_BIN := test_music_model_bin
MUSIC_AUDIO_TEST := test_music_audio_output.cpp
MUSIC_AUDIO_SRC := ../../../components/cyberdeck/src/platform/audio/cyberdeck_audio_output.cpp
MUSIC_AUDIO_HDR := ../../../components/cyberdeck/include/platform/audio/cyberdeck_audio_output.h
MUSIC_AUDIO_SHIMS := music_runtime_shim/bsp/m5stack_tab5.h \
	music_runtime_shim/esp_codec_dev.h \
	music_runtime_shim/freertos/FreeRTOS.h \
	music_runtime_shim/freertos/task.h
MUSIC_APP_TEST := test_music_app.cpp
MUSIC_APP_MAIN_SRC := ../../../components/cyberdeck/src/apps/music/cyberdeck_music_app.cpp
MUSIC_APP_OBJ := test_music_app/music_app.o
MUSIC_METADATA_SRC := ../../../components/cyberdeck/src/apps/music/cyberdeck_music_metadata.cpp
MUSIC_METADATA_HDR := ../../../components/cyberdeck/include/apps/music/cyberdeck_music_metadata.h
MUSIC_APP_SOURCES := \
	../../../components/cyberdeck/src/apps/music/cyberdeck_music_model.cpp \
	$(MUSIC_METADATA_SRC) \
	../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp \
	../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp \
	../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp \
	../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp \
	../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp \
	../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp \
	../../../components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp \
	music_runtime_shim/music_freertos_shim.cpp

MUSIC_APP_HDRS := ../../../components/cyberdeck/include/apps/music/cyberdeck_music_app.h \
	../../../components/cyberdeck/include/platform/display/cyberdeck_music_command_input.h \
	../../../components/cyberdeck/include/apps/music/cyberdeck_music_model.h \
	$(MUSIC_METADATA_HDR) \
	../../../components/cyberdeck/include/apps/music/cyberdeck_music_player.h \
	../../../components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h \
	../../../components/cyberdeck/include/apps/runtime/cyberdeck_app_storage.h \
	../../../components/cyberdeck/include/apps/shell/cyberdeck_local_shell.h \
	../../../components/cyberdeck/include/platform/audio/cyberdeck_audio_output.h \
	../../../components/cyberdeck/include/platform/storage/cyberdeck_sd_io_coordinator.h
MUSIC_PLAYER_TEST := test_music_player.cpp
MUSIC_COMMAND_INPUT_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_music_command_input.cpp
MUSIC_COMMAND_INPUT_HDR := ../../../components/cyberdeck/include/platform/display/cyberdeck_music_command_input.h
MUSIC_PLAYER_SOURCES := ../../../components/cyberdeck/src/apps/music/cyberdeck_music_player.cpp \
	../../../components/cyberdeck/src/apps/music/cyberdeck_music_model.cpp \
	$(MUSIC_METADATA_SRC) \
	../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_storage.cpp \
	../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp \
	../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp \
	../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp \
	../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp \
	../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp \
	music_runtime_shim/music_freertos_shim.cpp
MUSIC_PLAYER_SHIMS := music_runtime_shim/esp_log.h \
	music_runtime_shim/esp_heap_caps.h \
	music_runtime_shim/freertos/idf_additions.h \
	music_runtime_shim/bsp/m5stack_tab5.h \
	music_runtime_shim/esp_codec_dev.h \
	music_runtime_shim/freertos/FreeRTOS.h \
	music_runtime_shim/freertos/queue.h \
	music_runtime_shim/freertos/semphr.h \
	music_runtime_shim/freertos/task.h

# Incremental compilation for the first migrated coverage-producing targets.
SAVE_SIDECAR_OBJS := test_save_sidecar_recovery/test.o test_save_sidecar_recovery/local-shell.o test_save_sidecar_recovery/vfs.o test_save_sidecar_recovery/sd-io.o
WIFI_SURVEY_EXPORT_OBJS := test_wifi_survey_export_controller/test.o test_wifi_survey_export_controller/controller.o
NETDIAG_CONTROLLER_OBJS := test_netdiag_controller/test.o test_netdiag_controller/controller.o
NETDIAG_TCP_OBJS := test_netdiag_tcp_adapter/test.o test_netdiag_tcp_adapter/adapter.o
EVENT_LOG_RUNTIME_OBJS := test_event_log_runtime/test.o test_event_log_runtime/shim.o test_event_log_runtime/event-log.o test_event_log_runtime/recent.o test_event_log_runtime/clock.o
SD_IO_COORDINATOR_OBJS := test_sd_io_coordinator/test.o test_sd_io_coordinator/coordinator.o
BATTERY_CONTRACT_OBJS := test_battery_contract/test.o test_battery_contract/status.o
BATTERY_VIEW_OBJS := test_battery_view/test.o test_battery_view/view.o test_battery_view/protection.o
BLE_TYPES_OBJS := test_ble_types/test.o test_ble_types/types.o
BLE_STATE_OBJS := test_ble_state_machine/test.o test_ble_state_machine/state.o test_ble_state_machine/types.o
BLE_EVENTS_OBJS := test_ble_event_dispatch/test.o test_ble_event_dispatch/events.o test_ble_event_dispatch/types.o
BLE_STORE_OBJS := test_ble_store/test.o test_ble_store/store.o test_ble_store/types.o
BLE_COMMAND_OBJS := test_ble_command_parse/test.o test_ble_command_parse/shell-utils.o
WIFI_AUDIT_PERSISTENCE_OBJS := test_wifi_audit_persistence/test.o test_wifi_audit_persistence/persistence.o
WIFI_AUDIT_SAVE_OBJS := test_wifi_audit_save/test.o test_wifi_audit_save/audit.o test_wifi_audit_save/persistence.o test_wifi_audit_save/clock.o
SERIAL_NDJSON_OBJS := test_serial_ndjson_dispatch/test.o test_serial_ndjson_dispatch/bridge.o
SERIAL_SCREEN_DUMP_OBJS := test_serial_screen_dump/test.o test_serial_screen_dump/bridge.o test_serial_screen_dump/screenshot-bmp.o
SERIAL_CLI_TOLERANCE_OBJS := test_serial_cli_tolerance/test.o test_serial_cli_tolerance/bridge.o
SERIAL_SYSINFO_WIFI_OBJS := test_serial_sysinfo_wifi/test.o test_serial_sysinfo_wifi/bridge.o
SERIAL_TERM_DUMP_OBJS := test_serial_term_dump/test.o test_serial_term_dump/bridge.o
FS_WRITE_DISPATCH_OBJS := test_fs_write_dispatch/test.o test_fs_write_dispatch/bridge.o
WIFI_EVENT_DISPATCH_OBJS := test_wifi_event_dispatch/test.o test_wifi_event_dispatch/event-dispatch.o
WIFI_PERSISTENCE_QUEUE_OBJS := test_wifi_persistence_queue/test.o test_wifi_persistence_queue/persistence-queue.o test_wifi_persistence_queue/event-dispatch.o
WIFI_PERSISTENCE_COORDINATION_OBJS := test_wifi_persistence_coordination/test.o test_wifi_persistence_coordination/persistence-coordination.o test_wifi_persistence_coordination/persistence-queue.o test_wifi_persistence_coordination/event-dispatch.o
CLOCK_FORMATTER_OBJS := test_clock_formatter/test.o test_clock_formatter/clock.o
TERMINAL_FILTER_OBJS := test_terminal_filter/test.o test_terminal_filter/terminal-filter.o
EDIT_LINE_OBJS := test_edit_line/test.o test_edit_line/edit-line.o
WIFI_INDICATOR_OBJS := test_wifi_indicator_state/test.o test_wifi_indicator_state/indicator.o
WIFI_ICON_OBJS := test_wifi_icon_layout/test.o test_wifi_icon_layout/icon.o test_wifi_icon_layout/indicator.o
WIFI_MENU_OBJS := test_wifi_menu/test.o test_wifi_menu/menu.o
HISTORY_OBJS := test_history/test.o test_history/history.o
KEYBOARD_EVENT_OBJS := test_keyboard_event/test.o test_keyboard_event/event.o
EVENT_LOG_RECENT_OBJS := test_event_log_recent/test.o test_event_log_recent/recent.o
WIFI_STATE_MACHINE_OBJS := test_wifi_state_machine/test.o test_wifi_state_machine/state.o
NET_COORDINATOR_OBJS := test_net_coordinator/test.o test_net_coordinator/coordinator.o
SSH_ECHO_GUARD_OBJS := test_ssh_echo_guard/test.o test_ssh_echo_guard/guard.o
SSH_LINE_COMPOSER_OBJS := test_ssh_line_composer/test.o test_ssh_line_composer/composer.o
KEYMAP_OBJS := test_keymap/test.o test_keymap/keys.o
SCREENSHOT_BMP_OBJS := test_screenshot_bmp/test.o test_screenshot_bmp/bmp.o
ORIENTATION_OBJS := test_orientation/test.o test_orientation/orientation.o

# Per-TU object sets for the final migration batch.
EDITOR_MODEL_MIG_OBJS := test_editor_model/test.o test_editor_model/editor_model.o
TIME_SYNC_MIG_OBJS := test_time_sync/test.o test_time_sync/time_sync.o test_time_sync/cJSON.o
SHELL_UTILS_MIG_OBJS := test_shell_utils/test.o test_shell_utils/shell_utils.o
SD_PACKAGE_MIG_OBJS := test_sd_package/test.o test_sd_package/sd_package.o
RESOURCE_CATALOG_MIG_OBJS := test_resource_catalog/test.o test_resource_catalog/resource_catalog.o
NETDIAG_DNS_ADAPTER_MIG_OBJS := test_netdiag_dns_adapter/test.o test_netdiag_dns_adapter/netdiag_dns_adapter.o
NETDIAG_HTTPS_ADAPTER_MIG_OBJS := test_netdiag_https_adapter/test.o test_netdiag_https_adapter/netdiag_https_adapter.o
NETDIAG_APP_MIG_OBJS := test_netdiag_app/test.o test_netdiag_app/netdiag_app.o test_netdiag_app/netdiag_controller.o test_netdiag_app/netdiag_dns_adapter.o test_netdiag_app/netdiag_tcp_adapter.o test_netdiag_app/netdiag_https_adapter.o test_netdiag_app/app_runtime.o test_netdiag_app/resource_catalog.o test_netdiag_app/sd_package.o test_netdiag_app/app_quota.o test_netdiag_app/sd_io_coordinator.o
SHELL_SESSION_MIG_OBJS := test_shell_session/test.o test_shell_session/shell_session.o test_shell_session/shell_console.o test_shell_session/edit_line.o test_shell_session/history.o test_shell_session/shell_utils.o test_shell_session/local_shell.o test_shell_session/vfs_namespace.o test_shell_session/sd_io_coordinator.o test_shell_session/ssh_line_composer.o test_shell_session/screen_protection.o test_shell_session/app_runtime.o test_shell_session/resource_catalog.o test_shell_session/sd_package.o test_shell_session/app_quota.o test_shell_session/battery_protection.o test_shell_session/battery_status.o test_shell_session/wifi_menu.o test_shell_session/wifi_state_machine.o test_shell_session/wifi_survey.o test_shell_session/ble_types.o test_shell_session/ble_state_machine.o
SHELL_HELP_MIG_OBJS := test_shell_help/test.o test_shell_help/shell_session.o test_shell_help/shell_console.o test_shell_help/edit_line.o test_shell_help/history.o test_shell_help/shell_utils.o test_shell_help/local_shell.o test_shell_help/vfs_namespace.o test_shell_help/sd_io_coordinator.o test_shell_help/ssh_line_composer.o test_shell_help/screen_protection.o test_shell_help/app_runtime.o test_shell_help/resource_catalog.o test_shell_help/sd_package.o test_shell_help/app_quota.o test_shell_help/battery_protection.o test_shell_help/battery_status.o test_shell_help/wifi_menu.o test_shell_help/wifi_state_machine.o test_shell_help/wifi_survey.o test_shell_help/ble_types.o test_shell_help/ble_state_machine.o test_shell_help/demo_app.o
APP_RUNTIME_MIG_OBJS := test_app_runtime/test.o test_app_runtime/app_runtime.o test_app_runtime/resource_catalog.o test_app_runtime/sd_package.o test_app_runtime/app_quota.o
RUNTIME_SDK_MIG_OBJS := test_runtime_sdk/test.o test_runtime_sdk/app_runtime.o test_runtime_sdk/resource_catalog.o test_runtime_sdk/sd_package.o test_runtime_sdk/app_quota.o test_runtime_sdk/app_storage.o test_runtime_sdk/app_logger.o test_runtime_sdk/command_catalog.o test_runtime_sdk/local_shell.o test_runtime_sdk/vfs_namespace.o test_runtime_sdk/sd_io_coordinator.o
STORAGE_FACADE_MIG_OBJS := test_storage_facade/test.o test_storage_facade/app_runtime.o test_storage_facade/resource_catalog.o test_storage_facade/sd_package.o test_storage_facade/app_quota.o test_storage_facade/app_storage.o test_storage_facade/local_shell.o test_storage_facade/vfs_namespace.o test_storage_facade/sd_io_coordinator.o
PHASE9_CAPABILITIES_MIG_OBJS := test_phase9_capabilities/test.o test_phase9_capabilities/app_runtime.o test_phase9_capabilities/app_facades.o test_phase9_capabilities/window_manager.o test_phase9_capabilities/resource_catalog.o test_phase9_capabilities/sd_package.o test_phase9_capabilities/app_quota.o
DEMO_APP_MIG_OBJS := test_demo_app/test.o test_demo_app/demo_app.o test_demo_app/app_runtime.o test_demo_app/resource_catalog.o test_demo_app/sd_package.o test_demo_app/app_quota.o
SHELL_APP_MIG_OBJS := test_shell_app/test.o test_shell_app/shell_console.o test_shell_app/shell_app.o test_shell_app/shell_session.o test_shell_app/edit_line.o test_shell_app/history.o test_shell_app/shell_utils.o test_shell_app/local_shell.o test_shell_app/vfs_namespace.o test_shell_app/sd_io_coordinator.o test_shell_app/ssh_line_composer.o test_shell_app/screen_protection.o test_shell_app/app_runtime.o test_shell_app/resource_catalog.o test_shell_app/sd_package.o test_shell_app/app_quota.o test_shell_app/battery_protection.o test_shell_app/battery_status.o test_shell_app/wifi_menu.o test_shell_app/wifi_state_machine.o test_shell_app/ble_types.o test_shell_app/ble_state_machine.o
WINDOW_MANAGER_MIG_OBJS := test_window_manager/test.o test_window_manager/window_manager.o test_window_manager/window_manager_adapter.o test_window_manager/header_view.o test_window_manager/wifi_icon.o test_window_manager/wifi_indicator.o test_window_manager/battery_view.o
LOCAL_SHELL_MIG_OBJS := test_local_shell/test.o test_local_shell/local_shell.o test_local_shell/vfs_namespace.o test_local_shell/sd_io_coordinator.o
CAT_MULTILINE_MIG_OBJS := test_cat_multiline/test.o test_cat_multiline/local_shell.o test_cat_multiline/vfs_namespace.o test_cat_multiline/sd_io_coordinator.o
HELP_UNIFICATION_MIG_OBJS := test_help_unification/test.o test_help_unification/shell_utils.o test_help_unification/local_shell.o test_help_unification/vfs_namespace.o test_help_unification/sd_io_coordinator.o
BATTERY_PROTECTION_MIG_OBJS := test_battery_protection/test.o test_battery_protection/battery_protection.o
CAT_WORKER_MIG_OBJS := test_cat_worker/test.o test_cat_worker/cat_worker.o test_cat_worker/local_shell.o test_cat_worker/vfs_namespace.o test_cat_worker/sd_io_coordinator.o
BLE_BACKGROUND_MIG_OBJS := test_ble_background/test.o test_ble_background/ble_background.o test_ble_background/ble_state_machine.o test_ble_background/ble_types.o
KEYBOARD_DISPATCH_MIG_OBJS := test_keyboard_dispatch/test.o test_keyboard_dispatch/keyboard_dispatch.o
DISPLAY_VIEWS_MIG_OBJS := test_display_views/test.o test_display_views/header_view.o test_display_views/terminal_view.o test_display_views/editor_view.o test_display_views/music_view.o test_display_views/wifi_icon.o test_display_views/wifi_indicator.o test_display_views/battery_view.o
KNOWN_HOSTS_FILE_MIG_OBJS := test_known_hosts_file/test.o test_known_hosts_file/known_hosts_file.o test_known_hosts_file/sd_io_coordinator.o
POWEROFF_MIG_OBJS := test_poweroff/test.o test_poweroff/poweroff.o
MIGRATION_FINAL_OBJS := $(EDITOR_MODEL_MIG_OBJS) $(TIME_SYNC_MIG_OBJS) $(SHELL_UTILS_MIG_OBJS) $(SD_PACKAGE_MIG_OBJS) $(RESOURCE_CATALOG_MIG_OBJS) $(NETDIAG_DNS_ADAPTER_MIG_OBJS) $(NETDIAG_HTTPS_ADAPTER_MIG_OBJS) $(NETDIAG_APP_MIG_OBJS) $(SHELL_SESSION_MIG_OBJS) $(SHELL_HELP_MIG_OBJS) $(APP_RUNTIME_MIG_OBJS) $(RUNTIME_SDK_MIG_OBJS) $(STORAGE_FACADE_MIG_OBJS) $(PHASE9_CAPABILITIES_MIG_OBJS) $(DEMO_APP_MIG_OBJS) $(SHELL_APP_MIG_OBJS) $(WINDOW_MANAGER_MIG_OBJS) $(LOCAL_SHELL_MIG_OBJS) $(CAT_MULTILINE_MIG_OBJS) $(HELP_UNIFICATION_MIG_OBJS) $(BATTERY_PROTECTION_MIG_OBJS) $(CAT_WORKER_MIG_OBJS) $(BLE_BACKGROUND_MIG_OBJS) $(KEYBOARD_DISPATCH_MIG_OBJS) $(DISPLAY_VIEWS_MIG_OBJS) $(KNOWN_HOSTS_FILE_MIG_OBJS) $(POWEROFF_MIG_OBJS)

MAKE_DEPFILES := $(addsuffix .d,$(MIGRATION_FINAL_OBJS) $(SCREEN_PROTECTION_OBJS) $(SAVE_SIDECAR_OBJS) $(WIFI_SURVEY_EXPORT_OBJS) $(NETDIAG_CONTROLLER_OBJS) $(NETDIAG_TCP_OBJS) $(EVENT_LOG_RUNTIME_OBJS) $(SD_IO_COORDINATOR_OBJS) $(BATTERY_CONTRACT_OBJS) $(BATTERY_VIEW_OBJS) $(BLE_TYPES_OBJS) $(BLE_STATE_OBJS) $(BLE_EVENTS_OBJS) $(BLE_STORE_OBJS) $(BLE_COMMAND_OBJS) $(WIFI_AUDIT_OBJS) $(TERMINAL_SCROLLBACK_OBJS) $(WIFI_SURVEY_OBJS) $(RECOVERY_POLICY_OBJS) $(WIFI_AUDIT_PERSISTENCE_OBJS) $(WIFI_AUDIT_SAVE_OBJS) $(SERIAL_NDJSON_OBJS) $(SERIAL_SCREEN_DUMP_OBJS) $(SERIAL_CLI_TOLERANCE_OBJS) $(SERIAL_SYSINFO_WIFI_OBJS) $(SERIAL_TERM_DUMP_OBJS) $(FS_WRITE_DISPATCH_OBJS) $(WIFI_EVENT_DISPATCH_OBJS) $(WIFI_PERSISTENCE_QUEUE_OBJS) $(WIFI_PERSISTENCE_COORDINATION_OBJS) $(CLOCK_FORMATTER_OBJS) $(TERMINAL_FILTER_OBJS) $(EDIT_LINE_OBJS) $(WIFI_INDICATOR_OBJS) $(WIFI_ICON_OBJS) $(WIFI_MENU_OBJS) $(HISTORY_OBJS) $(KEYBOARD_EVENT_OBJS) $(EVENT_LOG_RECENT_OBJS) $(WIFI_STATE_MACHINE_OBJS) $(NET_COORDINATOR_OBJS) $(SSH_ECHO_GUARD_OBJS) $(SSH_LINE_COMPOSER_OBJS) $(KEYMAP_OBJS) $(SCREENSHOT_BMP_OBJS) $(ORIENTATION_OBJS) $(MUSIC_APP_OBJ))
LEGACY_TEST_ARTIFACTS := test_editor_model test_shell_utils test_shell_session test_app_runtime test_runtime_sdk test_demo_app test_local_shell test_cat_multiline test_help_unification test_known_hosts_file test_poweroff  test_save_sidecar_recovery test_wifi_survey_export_controller test_netdiag_controller test_netdiag_tcp_adapter test_netdiag_dns_adapter test_netdiag_https_adapter test_netdiag_app test_event_log_runtime test_sd_io_coordinator test_battery_contract test_battery_view test_ble_types test_ble_state_machine test_ble_event_dispatch test_ble_store test_ble_command_parse test_shell_help test_shell_app test_screen_protection test_terminal_scrollback test_wifi_audit test_wifi_audit_persistence test_wifi_audit_save test_wifi_survey test_serial_ndjson_dispatch test_serial_screen_dump test_serial_cli_tolerance test_serial_sysinfo_wifi test_serial_term_dump test_fs_write_dispatch test_recovery_policy test_storage_facade test_phase9_capabilities test_cat_worker test_ble_background test_keyboard_dispatch test_display_views test_window_manager test_time_sync test_clock_formatter test_terminal_filter test_edit_line test_wifi_event_dispatch test_wifi_persistence_queue test_wifi_persistence_coordination test_wifi_indicator_state test_wifi_icon_layout test_wifi_menu test_history test_keyboard_event test_event_log_recent test_wifi_state_machine test_net_coordinator test_ssh_echo_guard test_ssh_line_composer test_keymap test_screenshot_bmp test_orientation

test_save_sidecar_recovery/%.o: test_save_sidecar_recovery.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_save_sidecar_recovery/local-shell.o: $(LOCAL_SHELL_SRC) $(LOCAL_SHELL_HDR) $(VFS_NAMESPACE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $(firstword $(LOCAL_SHELL_SRC)) -o $@

test_save_sidecar_recovery/vfs.o: $(VFS_NAMESPACE_SRC) $(VFS_NAMESPACE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_save_sidecar_recovery/sd-io.o: $(SD_IO_COORDINATOR_SRC) $(SD_IO_COORDINATOR_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_wifi_survey_export_controller/%.o: test_wifi_survey_export_controller.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_wifi_survey_export_controller/controller.o: $(WIFI_SURVEY_EXPORT_CONTROLLER_SRC) $(WIFI_SURVEY_EXPORT_CONTROLLER_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_controller/%.o: test_netdiag_controller.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_controller/controller.o: $(NETDIAG_CONTROLLER_SRC) $(NETDIAG_CONTROLLER_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_tcp_adapter/%.o: test_netdiag_tcp_adapter.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_tcp_shim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_tcp_adapter/adapter.o: $(NETDIAG_TCP_ADAPTER_SRC) $(NETDIAG_TCP_ADAPTER_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_tcp_shim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

%.o.d: ;
-include $(MAKE_DEPFILES)
BINS += $(SD_PACKAGE_BIN) $(RESOURCE_CATALOG_BIN)

# Tier-A per-translation-unit migrations from REQ-MIG-59. Keep each compile
# command's baseline flags/includes and the original source/link order.
test_wifi_event_dispatch/test.o: test_wifi_event_dispatch.cpp contracts/cyberdeck_wifi_event_dispatch.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_event_dispatch/event-dispatch.o: $(WIFI_EVENT_DISPATCH_SRC) ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_event_dispatch.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_event_dispatch_bin: $(WIFI_EVENT_DISPATCH_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(WIFI_EVENT_DISPATCH_OBJS) -o $@

.PHONY: test_wifi_event_dispatch
test_wifi_event_dispatch: test_wifi_event_dispatch_bin
	@./test_wifi_event_dispatch_bin

test_wifi_persistence_queue/test.o: test_wifi_persistence_queue.cpp contracts/cyberdeck_wifi_persistence_queue.h contracts/cyberdeck_wifi_event_dispatch.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_persistence_queue/persistence-queue.o: $(WIFI_PERSISTENCE_QUEUE_SRC) ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_persistence_queue.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_persistence_queue/event-dispatch.o: $(WIFI_EVENT_DISPATCH_SRC) ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_event_dispatch.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_persistence_queue_bin: $(WIFI_PERSISTENCE_QUEUE_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(WIFI_PERSISTENCE_QUEUE_OBJS) -o $@

.PHONY: test_wifi_persistence_queue
test_wifi_persistence_queue: test_wifi_persistence_queue_bin
	@./test_wifi_persistence_queue_bin

test_wifi_persistence_coordination/test.o: test_wifi_persistence_coordination.cpp contracts/cyberdeck_wifi_persistence_coordinator.h contracts/cyberdeck_wifi_persistence_queue.h contracts/cyberdeck_wifi_event_dispatch.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -Icontracts -MMD -MP -MF $@.d -c $< -o $@

test_wifi_persistence_coordination/persistence-coordination.o: $(WIFI_PERSISTENCE_COORDINATOR_SRC) ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_persistence_coordinator.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -Icontracts -MMD -MP -MF $@.d -c $< -o $@

test_wifi_persistence_coordination/persistence-queue.o: $(WIFI_PERSISTENCE_QUEUE_SRC) ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_persistence_queue.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -Icontracts -MMD -MP -MF $@.d -c $< -o $@

test_wifi_persistence_coordination/event-dispatch.o: $(WIFI_EVENT_DISPATCH_SRC) ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_event_dispatch.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -Icontracts -MMD -MP -MF $@.d -c $< -o $@

test_wifi_persistence_coordination_bin: $(WIFI_PERSISTENCE_COORDINATION_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -Icontracts $(WIFI_PERSISTENCE_COORDINATION_OBJS) -o $@

.PHONY: test_wifi_persistence_coordination
test_wifi_persistence_coordination: test_wifi_persistence_coordination_bin
	@./test_wifi_persistence_coordination_bin

test_clock_formatter/test.o: test_clock_formatter.cpp ../../../components/cyberdeck/include/platform/display/cyberdeck_clock.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_clock_formatter/clock.o: $(CLOCK_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_clock.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_clock_formatter_bin: $(CLOCK_FORMATTER_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(CLOCK_FORMATTER_OBJS) -o $@

.PHONY: test_clock_formatter
test_clock_formatter: test_clock_formatter_bin
	@./test_clock_formatter_bin

test_terminal_filter/test.o: test_terminal_filter.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_terminal_filter.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_terminal_filter/terminal-filter.o: $(TERMINAL_FILTER_SRC) ../../../components/cyberdeck/include/apps/shell/cyberdeck_terminal_filter.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_terminal_filter_bin: $(TERMINAL_FILTER_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(TERMINAL_FILTER_OBJS) -o $@

.PHONY: test_terminal_filter
test_terminal_filter: test_terminal_filter_bin
	@./test_terminal_filter_bin

test_edit_line/test.o: test_edit_line.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_edit_line.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_edit_line/edit-line.o: $(EDIT_LINE_SRC) ../../../components/cyberdeck/include/apps/shell/cyberdeck_edit_line.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_edit_line_bin: $(EDIT_LINE_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(EDIT_LINE_OBJS) -o $@

.PHONY: test_edit_line
test_edit_line: test_edit_line_bin
	@./test_edit_line_bin

test_screen_protection/test.o: $(SCREEN_PROTECTION_TEST) $(SCREEN_PROTECTION_HDR) $(SCREEN_PROTECTION_CONTRACT_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_screen_protection/protection.o: $(SCREEN_PROTECTION_SRC) $(SCREEN_PROTECTION_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

$(SCREEN_PROTECTION_BIN): $(SCREEN_PROTECTION_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include $(SCREEN_PROTECTION_OBJS) -o $@

test_terminal_scrollback/test.o: test_terminal_scrollback.cpp $(TERMINAL_SCROLLBACK_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -fsanitize=address,undefined -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_terminal_scrollback/scrollback.o: $(TERMINAL_SCROLLBACK_SRC) $(TERMINAL_SCROLLBACK_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -fsanitize=address,undefined -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

$(TERMINAL_SCROLLBACK_BIN): $(TERMINAL_SCROLLBACK_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -fsanitize=address,undefined -I../../../components/cyberdeck/include $(TERMINAL_SCROLLBACK_OBJS) $(LDFLAGS) $(LDLIBS) -o $@

.PHONY: test_screen_protection
test_screen_protection: $(SCREEN_PROTECTION_BIN)
	@./$(SCREEN_PROTECTION_BIN)

test_editor_model/test.o: test_editor_model.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_editor_model/editor_model.o: ../../../components/cyberdeck/src/apps/editor/cyberdeck_editor_model.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_editor_model_bin: $(EDITOR_MODEL_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(EDITOR_MODEL_MIG_OBJS) -o $@

.PHONY: test_editor_model
test_editor_model: test_editor_model_bin
	@./test_editor_model_bin


test_editor_runtime_contract: test_editor_runtime_contract.py
	$(PYTHON) test_editor_runtime_contract.py

test_editor_save_contract: test_editor_save_contract.py \
		test_storage_facade.cpp \
		../../../components/cyberdeck/src/apps/editor/cyberdeck_editor_app.cpp \
		../../../components/cyberdeck/include/apps/editor/cyberdeck_editor_app.h \
		../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_storage.cpp \
		../../../components/cyberdeck/include/apps/runtime/cyberdeck_app_storage.h \
		../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp \
		../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp \
		editor_traceability.md Makefile
	$(PYTHON) test_editor_save_contract.py

test_editor_surface_contract: test_editor_surface_contract.py \
		../../../components/cyberdeck/src/platform/display/cyberdeck_editor_view.cpp \
		../../../components/cyberdeck/include/platform/display/cyberdeck_editor_view.h \
		../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp \
		../../../components/cyberdeck/src/apps/editor/cyberdeck_editor_app.cpp \
		../../../components/cyberdeck/src/apps/editor/cyberdeck_editor_model.cpp \
		editor_traceability.md Makefile
	$(PYTHON) test_editor_surface_contract.py

TIME_SYNC_SRC := ../../../components/cyberdeck/src/apps/system/cyberdeck_time_sync.cpp
TIME_SYNC_SHIM := time_sync_shim

test_time_sync_contract: test_time_sync_contract.py time_sync_traceability.md ../../../components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp $(TIME_SYNC_SRC) ../../../components/cyberdeck/src/platform/logging/event_log.cpp ../../../components/cyberdeck/include/platform/logging/event_log.h Makefile
	$(PYTHON) test_time_sync_contract.py

.PHONY: test_time_sync
test_time_sync/test.o: test_time_sync.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -I$(TIME_SYNC_SHIM) -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_time_sync/time_sync.o: ../../../components/cyberdeck/src/apps/system/cyberdeck_time_sync.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -I$(TIME_SYNC_SHIM) -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_time_sync/cJSON.o: time_sync_shim/cJSON.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -I$(TIME_SYNC_SHIM) -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_time_sync_bin: $(TIME_SYNC_MIG_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -I$(TIME_SYNC_SHIM) -Ishim -I../../../components/cyberdeck/include $(TIME_SYNC_MIG_OBJS) -o $@

test_time_sync: test_time_sync_bin
	@./test_time_sync_bin

.PHONY: test_phase9_capabilities test_phase9_capabilities_contract
.PHONY: test_time_sync test_time_sync_contract
test_phase9_capabilities_contract: test_phase9_capabilities_contract.py $(PHASE9_CAPABILITIES_TEST) Makefile
	$(PYTHON) test_phase9_capabilities_contract.py

.PHONY: all test verify clean test_shell_help test_shell_help_contract test_screenshot_lifecycle_contract test_event_log_contract test_event_log_config_contract test_boot_sequence imu_sensor_contract test_keyboard_input_contract test_ui_resource_contract test_terminal_output_contract test_ssh_output_gap test_local_prompt_contract test_prompt_behavior test_wifi_dispatch_contract test_wifi_enter_routing_contract test_wifi_audit_contract test_wifi_audit_persistence test_wifi_audit_persistence_contract test_wifi_audit_save test_wifi_audit_save_contract test_wifi_evt_worker_lifecycle_contract test_cat_contract test_cat_multiline_contract test_cat_lifecycle_sanitization_contract test_cat_start_teardown_start_contract test_cat_stack_footprint_contract test_cat_worker_path_safety_contract local_shell_security_contract local_shell_tokenizer_contract help_unification_contract test_serial_lifecycle_contract test_serial_ndjson_dispatch test_serial_screen_dump test_serial_cli_tolerance test_serial_sysinfo_wifi test_serial_term_dump test_term_dump_contract test_term_dump_cli test_fs_write_dispatch test_fs_write_cli test_fs_write_contract test_battery_contract test_battery_reader_contract test_battery_ui_contract test_battery_protection test_battery_protection_contract test_battery_view test_battery_view_contract test_screen_protection test_screen_protection_contract test_ble_types test_ble_state_machine test_ble_event_dispatch test_ble_store test_ble_command_parse test_ble_integration_contract test_ble_ui_timeout_contract test_ble_adv_types_contract test_ble_scan_contract test_ble_pre_sync_contract test_ble_review_regressions test_ble_runtime_regressions test_ble_ui_render_contract test_ble_header_contract test_ble_auth_contract test_ble_hid_contract test_ble_connection_lifecycle_contract test_ble_background_contract test_ble_bond_name_contract test_ble_bond_reconnect test_cat_worker test_ble_background test_keyboard_dispatch test_display_views test_coverage_scope_guard test_shell_app test_shell_app_contract test_window_manager test_window_manager_contract

.PHONY: test_wifi_survey test_wifi_survey_integration_contract test_wifi_survey_export_controller test_wifi_survey_export_result_contract
.PHONY: test_recovery_policy test_phase10_contract test_event_projection_contract test_save_sidecar_recovery
.PHONY: test_terminal_scrollback test_lat03_contract test_lat04_contract
.PHONY: test_display_lock_contract
.PHONY: test_editor_save_contract

.PHONY: test_sd_io_coordinator
test_sd_io_coordinator: $(SD_IO_COORDINATOR_BIN)
	./$(SD_IO_COORDINATOR_BIN)

test_sd_io_coordinator/test.o: $(SD_IO_COORDINATOR_TEST) $(SD_IO_COORDINATOR_HDR) sd_io_traceability.md Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_sd_io_coordinator/coordinator.o: $(SD_IO_COORDINATOR_SRC) $(SD_IO_COORDINATOR_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

$(SD_IO_COORDINATOR_BIN): $(SD_IO_COORDINATOR_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -pthread $(SD_IO_COORDINATOR_OBJS) -o $@

# Pure host contract for sd_package (REQ-COV-01 / AC-COV-01): FNV-1a
# checksum reference vectors, null/empty semantics, exact media-type
# acceptance, and independent rejection of every validate() rule — each rule
# violated alone on an otherwise-valid package, with exact size/version
# boundaries and checksum mismatch detection.  The real TU is linked directly.
# The .PHONY run target only executes the BINS-built binary so the aggregate
# runs it exactly once.
test_sd_package/test.o: test_sd_package.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_sd_package/sd_package.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_sd_package_bin: $(SD_PACKAGE_MIG_OBJS) sd_package_traceability.md Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(SD_PACKAGE_MIG_OBJS) -o $@


.PHONY: test_sd_package
test_sd_package: $(SD_PACKAGE_BIN)
	./$(SD_PACKAGE_BIN)

# Behavioral host contract for resource_catalog (REQ-RES-01 / AC-RES-01): the
# real TU is linked and exercised through its public surface — asset_checksum
# (FNV-1a reference vectors, null/empty semantics), verify_asset (id, size and
# null-byte rules isolated per property plus checksum accept/reject at the
# exact 12288-byte boundary) and the compiled catalog's empty/absent
# semantics.  No production code is altered; coverage percentages are never
# asserted.
test_resource_catalog/test.o: test_resource_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_resource_catalog/resource_catalog.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_resource_catalog_bin: $(RESOURCE_CATALOG_MIG_OBJS) resource_catalog_traceability.md Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(RESOURCE_CATALOG_MIG_OBJS) -o $@


.PHONY: test_resource_catalog
test_resource_catalog: $(RESOURCE_CATALOG_BIN)
	./$(RESOURCE_CATALOG_BIN)

.PHONY: test_netdiag_controller
$(NETDIAG_CONTROLLER_BIN): $(NETDIAG_CONTROLLER_OBJS) netdiag_traceability.md Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -I../../../components/cyberdeck/include $(NETDIAG_CONTROLLER_OBJS) -o $@

test_netdiag_controller: $(NETDIAG_CONTROLLER_BIN)
	./$(NETDIAG_CONTROLLER_BIN)

.PHONY: test_netdiag_dns_adapter
test_netdiag_dns_adapter/test.o: test_netdiag_dns_adapter.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_dns_shim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_dns_adapter/netdiag_dns_adapter.o: ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_dns_adapter.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_dns_shim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_dns_adapter_bin: $(NETDIAG_DNS_ADAPTER_MIG_OBJS) netdiag_traceability.md Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_dns_shim -I../../../components/cyberdeck/include $(NETDIAG_DNS_ADAPTER_MIG_OBJS) -o $@

test_netdiag_dns_adapter: $(NETDIAG_DNS_BIN)
	@./$(NETDIAG_DNS_BIN)

.PHONY: test_netdiag_tcp_adapter
$(NETDIAG_TCP_BIN): $(NETDIAG_TCP_OBJS) netdiag_traceability.md Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread $(NETDIAG_TCP_OBJS) -Wl,--wrap=socket -Wl,--wrap=close -Wl,--wrap=fcntl -Wl,--wrap=connect -Wl,--wrap=select -Wl,--wrap=getsockopt -o $(NETDIAG_TCP_BIN)

test_netdiag_tcp_adapter: $(NETDIAG_TCP_BIN)
	./$(NETDIAG_TCP_BIN)

.PHONY: test_netdiag_https_adapter
test_netdiag_https_adapter/test.o: test_netdiag_https_adapter.cpp shim/esp_err.h Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_https_adapter/netdiag_https_adapter.o: ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_https_adapter.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_https_adapter_bin: $(NETDIAG_HTTPS_ADAPTER_MIG_OBJS) netdiag_traceability.md Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -g --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include $(NETDIAG_HTTPS_ADAPTER_MIG_OBJS) -o $@

test_netdiag_https_adapter: $(NETDIAG_HTTPS_BIN)
	@./$(NETDIAG_HTTPS_BIN)

.PHONY: test_netdiag_app
test_netdiag_app/test.o: test_netdiag_app.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_app/netdiag_app.o: ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_app.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_app/netdiag_controller.o: ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_controller.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_app/netdiag_dns_adapter.o: ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_dns_adapter.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_app/netdiag_tcp_adapter.o: ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_tcp_adapter.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_app/netdiag_https_adapter.o: ../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_https_adapter.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_app/app_runtime.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_app/resource_catalog.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_app/sd_package.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_app/app_quota.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_app/sd_io_coordinator.o: ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_netdiag_app_bin: $(NETDIAG_APP_MIG_OBJS) netdiag_traceability.md Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Inetdiag_app_shim -Inetdiag_dns_shim -Inetdiag_tcp_shim -Inetdiag_https_shim -Ishim -I../../../components/cyberdeck/include $(NETDIAG_APP_MIG_OBJS) -Wl,--wrap=socket -Wl,--wrap=close -Wl,--wrap=fcntl -Wl,--wrap=connect -Wl,--wrap=select -Wl,--wrap=getsockopt -o $@

test_netdiag_app: $(NETDIAG_APP_BIN)
	@./$(NETDIAG_APP_BIN)

.PHONY: test_sd_io_integration_contract
test_sd_io_integration_contract: test_sd_io_integration_contract.py sd_io_traceability.md Makefile
	$(PYTHON) test_sd_io_integration_contract.py

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
test_keymap/test.o: test_keymap.cpp shim/lvgl.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_keymap/keys.o: $(KEYMAP_SRC) shim/lvgl.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_keymap_bin: $(KEYMAP_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(KEYMAP_OBJS) -o $@

.PHONY: test_keymap
test_keymap: test_keymap_bin
	@./test_keymap_bin

test_shell_utils/test.o: test_shell_utils.cpp contracts/cyberdeck_help.h shim/lvgl.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_utils/shell_utils.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_utils_bin: $(SHELL_UTILS_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SHELL_UTILS_MIG_OBJS) -o $@

.PHONY: test_shell_utils
test_shell_utils: test_shell_utils_bin
	@./test_shell_utils_bin


# Logica pura (sem LVGL): nao precisa de shim.
# O header entra como dependencia para que mudancas no contrato (ex.: limite)
# forcem recompilacao e a validacao do static_assert de paridade.
test_history/test.o: test_history.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_history.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_history/history.o: $(HISTORY_SRC) ../../../components/cyberdeck/include/apps/shell/cyberdeck_history.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_history_bin: $(HISTORY_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(HISTORY_OBJS) -o $@

.PHONY: test_history
test_history: test_history_bin
	@./test_history_bin

# Logica pura (sem LVGL): nao precisa de shim.
# O parser e uma unidade pura de producao; o teste fixa o contrato de
# tab5_char_event_parse sem depender de LVGL, FreeRTOS ou hardware.
test_keyboard_event/test.o: test_keyboard_event.cpp ../../../components/cyberdeck/include/platform/input/tab5_keyboard_event.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_keyboard_event/event.o: $(KEYBOARD_EVENT_SRC) ../../../components/cyberdeck/include/platform/input/tab5_keyboard_event.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_keyboard_event_bin: $(KEYBOARD_EVENT_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(KEYBOARD_EVENT_OBJS) -o $@

.PHONY: test_keyboard_event
test_keyboard_event: test_keyboard_event_bin
	@./test_keyboard_event_bin

# Logica pura extraida de event_log_latest(): selecao de indices do anel
# "recent" (contrato em event_log_recent.h, sem FreeRTOS/LVGL/SD).
# A implementacao e compilada junto ao teste para validar o contrato sem
# depender de FreeRTOS, LVGL ou acesso ao SD.
test_event_log_recent/test.o: test_event_log_recent.cpp ../../../components/cyberdeck/include/platform/logging/event_log_recent.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_event_log_recent/recent.o: $(EVENT_LOG_RECENT_SRC) ../../../components/cyberdeck/include/platform/logging/event_log_recent.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_event_log_recent_bin: $(EVENT_LOG_RECENT_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(EVENT_LOG_RECENT_OBJS) -o $@

.PHONY: test_event_log_recent
test_event_log_recent: test_event_log_recent_bin
	@./test_event_log_recent_bin

test_event_log_contract: test_event_log_contract.py ../../../components/cyberdeck/src/platform/logging/event_log.cpp Makefile
	$(PYTHON) test_event_log_contract.py

EVENT_LOG_SRC := ../../../components/cyberdeck/src/platform/logging/event_log.cpp
.PHONY: test_event_log_runtime
EVENT_LOG_RUNTIME_TEST := test_event_log_runtime.cpp
EVENT_LOG_SHIM := event_log_shim
test_event_log_shim: test_event_log_shim.cpp $(EVENT_LOG_SHIM)/event_log_shim.cpp $(EVENT_LOG_SHIM)/event_log_shim.h
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 -pthread -I$(EVENT_LOG_SHIM) -Ishim test_event_log_shim.cpp $(EVENT_LOG_SHIM)/event_log_shim.cpp -o $@
	./$@

test_event_log_runtime/test.o: $(EVENT_LOG_RUNTIME_TEST) $(EVENT_LOG_SHIM)/event_log_shim.h event_log_traceability.md Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -Wno-format -O0 --coverage -fsanitize=address,undefined -pthread -DCYBERDECK_EVENT_LOG_TEST -I$(EVENT_LOG_SHIM) -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_event_log_runtime/shim.o: $(EVENT_LOG_SHIM)/event_log_shim.cpp $(EVENT_LOG_SHIM)/event_log_shim.h Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -Wno-format -O0 --coverage -fsanitize=address,undefined -pthread -DCYBERDECK_EVENT_LOG_TEST -I$(EVENT_LOG_SHIM) -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_event_log_runtime/event-log.o: $(EVENT_LOG_SRC) $(EVENT_LOG_SHIM)/event_log_shim.h Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -Wno-format -O0 --coverage -fsanitize=address,undefined -pthread -DCYBERDECK_EVENT_LOG_TEST -I$(EVENT_LOG_SHIM) -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_event_log_runtime/recent.o: ../../../components/cyberdeck/src/platform/logging/event_log_recent.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -Wno-format -O0 --coverage -fsanitize=address,undefined -pthread -DCYBERDECK_EVENT_LOG_TEST -I$(EVENT_LOG_SHIM) -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_event_log_runtime/clock.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_clock.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -Wno-format -O0 --coverage -fsanitize=address,undefined -pthread -DCYBERDECK_EVENT_LOG_TEST -I$(EVENT_LOG_SHIM) -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

$(EVENT_LOG_RUNTIME_BIN): $(EVENT_LOG_RUNTIME_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -Wno-format -O0 --coverage -fsanitize=address,undefined -pthread $(EVENT_LOG_RUNTIME_OBJS) -Wl,--wrap=fsync -Wl,--wrap=fwrite -Wl,--wrap=fread -Wl,--wrap=fclose -o $@

test_event_log_runtime: $(EVENT_LOG_RUNTIME_BIN)
	@set -e; for scenario in $(if $(SCENARIOS),$(SCENARIOS),default preinit init-text-failure partial-init quiesce-race quiesce-fence durable-timeout-quiesce worker-completion projection-fwrite projection-fsync projection-fclose sticky-close sticky-write sticky-fsync worker-timeout-retry); do runtime_root=$$(mktemp -d); EVENT_LOG_TEST_ROOT="$$runtime_root" ./$(EVENT_LOG_RUNTIME_BIN) "$$scenario"; result=$$?; rm -rf "$$runtime_root"; test $$result -eq 0; done

test_event_projection_contract: test_event_projection_contract.py event_projection_traceability.md ../../../components/cyberdeck/src/platform/logging/event_log.cpp ../../../components/cyberdeck/include/platform/logging/event_log.h ../../../components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp ../../../components/cyberdeck/src/apps/screenshot/screenshot_server.cpp Makefile
	$(PYTHON) test_event_projection_contract.py

# Fase 10: policy pura de recovery, incluindo boot pendente, checkpoint,
# contador de três interrupções, latch e último erro bounded por app.
.PHONY: test_recovery_policy
test_recovery_policy_bin: $(RECOVERY_POLICY_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(RECOVERY_POLICY_OBJS) -o $@

test_recovery_policy/test.o: test_recovery_policy.cpp $(RECOVERY_POLICY_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_recovery_policy/policy.o: $(RECOVERY_POLICY_SRC) $(RECOVERY_POLICY_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@
test_recovery_policy: test_recovery_policy_bin
	@./test_recovery_policy_bin

test_recovery_runtime_bin: test_recovery_runtime.cpp $(RECOVERY_RUNTIME_SRC) $(RECOVERY_RUNTIME_POLICY_SRC) \
		recovery_runtime_shim/esp_err.h recovery_runtime_shim/esp_log.h recovery_runtime_shim/nvs.h \
		recovery_runtime_shim/nvs_flash.h Makefile
	$(CXX) $(CXXFLAGS) -pthread -Irecovery_runtime_shim -I../../../components/cyberdeck/include \
		test_recovery_runtime.cpp $(RECOVERY_RUNTIME_SRC) $(RECOVERY_RUNTIME_POLICY_SRC) -o $@

.PHONY: test_recovery_runtime
test_recovery_runtime: test_recovery_runtime_bin
	@./test_recovery_runtime_bin

test_phase10_contract: $(PHASE10_CONTRACT) $(RECOVERY_POLICY_SRC) $(RECOVERY_POLICY_HDR) \
		../../../components/cyberdeck/src/apps/system/cyberdeck_recovery.cpp \
		../../../components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp \
		../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp \
		../../../components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp \
		../../../main/app_main.cpp Makefile phase10_traceability.md
	$(PYTHON) $(PHASE10_CONTRACT)

test_event_log_config_contract: test_event_log_config_contract.py ../../../components/cyberdeck/Kconfig ../../../components/cyberdeck/include/platform/logging/event_log_recent.h ../../../components/cyberdeck/src/platform/logging/event_log.cpp ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp Makefile
	$(PYTHON) test_event_log_config_contract.py

# Logica pura (sem LVGL): indicador de estado do Wi-Fi (contrato em
# cyberdeck_wifi_indicator.h; plano aprovado: o indicador eh UNICO icono,
# LIT somente quando enabled && connected && has_ip, APAGADO nos demais
# estados; o contrato NAO recebe parametro ssid). A implementacao pura e
# compilada junto ao teste para manter o contrato host-side independente do
# LVGL.
test_wifi_indicator_state/test.o: test_wifi_indicator_state.cpp ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_indicator.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_indicator_state/indicator.o: $(WIFI_INDICATOR_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_indicator.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_indicator_state_bin: $(WIFI_INDICATOR_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(WIFI_INDICATOR_OBJS) -o $@

.PHONY: test_wifi_indicator_state
test_wifi_indicator_state: test_wifi_indicator_state_bin
	@./test_wifi_indicator_state_bin

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
test_wifi_icon_layout/test.o: test_wifi_icon_layout.cpp ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_icon.h ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_indicator.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_icon_layout/icon.o: $(WIFI_ICON_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_icon.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_icon_layout/indicator.o: $(WIFI_INDICATOR_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_indicator.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_icon_layout_bin: $(WIFI_ICON_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(WIFI_ICON_OBJS) -o $@

.PHONY: test_wifi_icon_layout
test_wifi_icon_layout: test_wifi_icon_layout_bin
	@./test_wifi_icon_layout_bin

# Logica pura (sem LVGL): modelo e renderizacao do menu Wi-Fi (scan/saved/deduplicacao/navegacao).
test_wifi_menu/test.o: test_wifi_menu.cpp ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_menu.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_menu/menu.o: $(WIFI_MENU_SRC) ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_menu.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_menu_bin: $(WIFI_MENU_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(WIFI_MENU_OBJS) -o $@

.PHONY: test_wifi_menu
test_wifi_menu: test_wifi_menu_bin
	@./test_wifi_menu_bin

# Host-side contract coverage for the pure search/saved state machine:
# timeout/cancel recovery, stale/future callback tokens, UI lifecycle state,
# and persistence only after the IP event. No hardware, callbacks, or
# production adapters are faked; the production pure adapter is linked.
test_wifi_state_machine/test.o: test_wifi_state_machine.cpp $(WIFI_STATE_MACHINE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_state_machine/state.o: $(WIFI_STATE_MACHINE_SRC) $(WIFI_STATE_MACHINE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_state_machine_bin: $(WIFI_STATE_MACHINE_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(WIFI_STATE_MACHINE_OBJS) -o $@

.PHONY: test_wifi_state_machine
test_wifi_state_machine: test_wifi_state_machine_bin
	@./test_wifi_state_machine_bin

test_wifi_audit/test.o: test_wifi_audit.cpp $(WIFI_AUDIT_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_audit/audit.o: $(WIFI_AUDIT_SRC) $(WIFI_AUDIT_HDR) $(WIFI_AUDIT_PERSISTENCE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

$(WIFI_AUDIT_BIN): $(WIFI_AUDIT_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(WIFI_AUDIT_OBJS) -o $@

.PHONY: test_wifi_audit
test_wifi_audit: $(WIFI_AUDIT_BIN)
	@./$(WIFI_AUDIT_BIN)

# Host contract for the injectable Wi-Fi audit persistence transaction.
# The target links the real production header/source and exercises every
# behavioral fault, recovery, ACK, and concurrency case.
.PHONY: test_wifi_audit_persistence
test_wifi_audit_persistence/test.o: test_wifi_audit_persistence.cpp $(WIFI_AUDIT_PERSISTENCE_HDR) $(WIFI_AUDIT_PERSISTENCE_CONTRACT_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -pthread -MMD -MP -MF $@.d -c $< -o $@

test_wifi_audit_persistence/persistence.o: $(WIFI_AUDIT_PERSISTENCE_SRC) $(WIFI_AUDIT_PERSISTENCE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -pthread -MMD -MP -MF $@.d -c $< -o $@

test_wifi_audit_persistence_bin: $(WIFI_AUDIT_PERSISTENCE_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -pthread $(WIFI_AUDIT_PERSISTENCE_OBJS) -o $@
test_wifi_audit_persistence: test_wifi_audit_persistence_bin
	@./test_wifi_audit_persistence_bin

test_wifi_audit_persistence_contract: test_wifi_audit_persistence_contract.py $(WIFI_AUDIT_PERSISTENCE_CONTRACT_HDR)
	$(PYTHON) test_wifi_audit_persistence_contract.py

# Simplified Wi-Fi audit save contract: the ordinary audit is display-only;
# the explicit save path owns directory/timestamp/collision behavior.
.PHONY: test_wifi_audit_save
test_wifi_audit_save/test.o: $(WIFI_AUDIT_SAVE_TEST) $(WIFI_AUDIT_HDR) $(WIFI_AUDIT_PERSISTENCE_HDR) ../../../components/cyberdeck/include/platform/display/cyberdeck_clock.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -pthread -MMD -MP -MF $@.d -c $< -o $@

test_wifi_audit_save/audit.o: $(WIFI_AUDIT_SRC) $(WIFI_AUDIT_HDR) $(WIFI_AUDIT_PERSISTENCE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -pthread -MMD -MP -MF $@.d -c $< -o $@

test_wifi_audit_save/persistence.o: $(WIFI_AUDIT_PERSISTENCE_SRC) $(WIFI_AUDIT_PERSISTENCE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -pthread -MMD -MP -MF $@.d -c $< -o $@

test_wifi_audit_save/clock.o: $(CLOCK_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_clock.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -pthread -MMD -MP -MF $@.d -c $< -o $@

test_wifi_audit_save_bin: $(WIFI_AUDIT_SAVE_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -pthread $(WIFI_AUDIT_SAVE_OBJS) -o $@
test_wifi_audit_save: test_wifi_audit_save_bin
	@./test_wifi_audit_save_bin

.PHONY: test_wifi_survey
test_wifi_survey/test.o: test_wifi_survey.cpp $(WIFI_SURVEY_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_wifi_survey/survey.o: $(WIFI_SURVEY_SRC) $(WIFI_SURVEY_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

$(WIFI_SURVEY_BIN): $(WIFI_SURVEY_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(WIFI_SURVEY_OBJS) -o $@
test_wifi_survey: test_wifi_survey_bin
	@./test_wifi_survey_bin

test_wifi_survey_integration_contract: test_wifi_survey_integration_contract.py wifi_survey_traceability.md ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp ../../../components/cyberdeck/src/apps/wifi/wifi_mgr.cpp ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_audit.cpp $(WIFI_AUDIT_PERSISTENCE_SRC) Makefile
	$(PYTHON) test_wifi_survey_integration_contract.py

$(WIFI_SURVEY_EXPORT_BIN): $(WIFI_SURVEY_EXPORT_OBJS) wifi_survey_traceability.md Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(WIFI_SURVEY_EXPORT_OBJS) -o $@

test_wifi_survey_export_controller: $(WIFI_SURVEY_EXPORT_BIN)
	./$(WIFI_SURVEY_EXPORT_BIN)

test_wifi_survey_export_result_contract: test_wifi_survey_export_result_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(WIFI_SURVEY_EXPORT_CONTROLLER_HDR) wifi_survey_traceability.md Makefile
	$(PYTHON) test_wifi_survey_export_result_contract.py

test_wifi_audit_save_contract: $(WIFI_AUDIT_SAVE_CONTRACT) ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(WIFI_AUDIT_SRC) $(WIFI_AUDIT_PERSISTENCE_SRC) $(WIFI_AUDIT_PERSISTENCE_HDR) ../../../components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp ../../../code-map.md ../../../.gitignore
	$(PYTHON) $(WIFI_AUDIT_SAVE_CONTRACT)

test_wifi_evt_worker_lifecycle_contract: test_wifi_evt_worker_lifecycle_contract.py ../../../components/cyberdeck/src/apps/wifi/wifi_mgr.cpp ../../../code-map.md Makefile
	$(PYTHON) test_wifi_evt_worker_lifecycle_contract.py

test_wifi_mgr_lifecycle_contract: test_wifi_mgr_lifecycle_contract.py ../../../components/cyberdeck/src/apps/wifi/wifi_mgr.cpp ../../../components/cyberdeck/include/apps/wifi/wifi_mgr.h ../../../components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp Makefile
	$(PYTHON) test_wifi_mgr_lifecycle_contract.py


# TDD RED: contrato estrutural para comando/UI, timer, NVS e ui.type. Nao
# executa display, Serial Automation Bridge, simulador ou power-cycle. O catalogo
# compartilhado de ajuda, a fixture e os dois testes comportamentais que
# comparam a ajuda de producao sao pre-requisitos: o contrato inspeciona todos
# eles e nao pode ser invalidado por arquivo obsoleto.
test_screen_protection_contract: $(SCREEN_PROTECTION_CONTRACT) $(SCREEN_PROTECTION_SRC) $(SCREEN_PROTECTION_HDR) $(SHELL_HELP_HDR) contracts/cyberdeck_help.h $(SHELL_TEST) $(HELP_UNIFICATION_TEST) $(SERIAL_TEST) ../../../components/cyberdeck/src/platform/display/screen_off.cpp ../../../components/cyberdeck/include/platform/display/screen_off.h ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp ../../../main/app_main.cpp ../../../components/cyberdeck/CMakeLists.txt ../../../code-map.md ../../../.gitignore
	$(PYTHON) $(SCREEN_PROTECTION_CONTRACT)

# REQ-LAT-03-01..04/11: real bounded scrollback model, UTF-8 pruning and
# independent 4096-byte viewport.  Sanitizers remain enabled locally while
# external compile/link flags (including CI coverage) are preserved.
.PHONY: test_terminal_scrollback
test_terminal_scrollback_bin: $(TERMINAL_SCROLLBACK_OBJS) Makefile
test_terminal_scrollback: test_terminal_scrollback_bin
	@./$(TERMINAL_SCROLLBACK_BIN)

test_lat03_contract: $(LAT03_CONTRACT) test_terminal_scrollback.cpp $(TERMINAL_SCROLLBACK_SRC) $(TERMINAL_SCROLLBACK_HDR) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp Makefile
	$(PYTHON) $(LAT03_CONTRACT)

test_lat04_contract: $(LAT04_CONTRACT) ../../../components/cyberdeck/include/platform/display/cyberdeck_terminal_view.h $(TERMINAL_VIEW_SRC) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp Makefile
	$(PYTHON) $(LAT04_CONTRACT)

test_terminal_output_contract: $(TERMINAL_OUTPUT_CONTRACT) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(SSH_CLIENT_SRC) test_terminal_filter.cpp $(TERMINAL_FILTER_SRC) $(TERMINAL_FILTER_HDR) $(LINE_COMPOSER_SRC) test_ssh_line_composer.cpp $(SESSION_SRC) $(SHELL_CONSOLE_HDR) $(SSH_OUTPUT_GAP_TEST) Makefile
	$(PYTHON) $(TERMINAL_OUTPUT_CONTRACT)

# Comportamental do seam de gap da saida SSH (REQ-01/02/03/04). A UI nao e
# linkavel no host, entao o harness extrai *verbatim* as sentencas de producao
# da guarda de epoch, dos dois callbacks e do timer LVGL, e as compila contra os
# TUs reais do filtro ANSI, do compositor de linha, do console e do runtime. So a
# fila bounded e os efeitos de LVGL sao duplos do harness. O limite de payload e
# derivado do buffer de leitura de ssh_client.cpp e entra como `static_assert`.
test_ssh_output_gap: $(SSH_OUTPUT_GAP_TEST) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(SSH_CLIENT_SRC) $(TERMINAL_FILTER_SRC) $(LINE_COMPOSER_SRC) $(SHELL_CONSOLE_SRC) $(SHELL_CONSOLE_HDR) $(APP_RUNTIME_SRC) $(RESOURCE_CATALOG_SRC) $(SD_PACKAGE_SRC) $(APP_QUOTA_SRC) $(APP_STORAGE_SRC) $(APP_LOGGER_SRC) $(LOCAL_SHELL_SRC) $(VFS_NAMESPACE_SRC) $(SD_IO_COORDINATOR_SRC) $(TERMINAL_SCROLLBACK_SRC) $(TERMINAL_SCROLLBACK_HDR) Makefile
	$(PYTHON) $(SSH_OUTPUT_GAP_TEST)

# Logica pura (sem LVGL): modelo da linha de edicao + sessao SSH (contrato em
# cyberdeck_edit_line.h; bug aprovado A: cada caractere aparece exatamente uma
# vez, entrada fisica/virtual com paridade, backspace/setas UTF-8 na posicao
# real do cursor e Enter que envia comando+newline uma unica vez e limpa a
# linha sem duplicacao). A implementacao (cyberdeck_edit_line.cpp) existe e os
# testes de payload passam; a regressao E (politica de eco explicita:
# cyberdeck_enter_result::echo — MENU ecoa "$ cmd\n", CONNECTED/PASSWORD/
# HOST_KEY nao ecoam) fica vermelha EM RUNTIME ate o coder preencher `echo`
# na implementacao (TDD; nenhuma implementacao fake e usada).
# Sessao de shell extraida: e livre de LVGL e alcanca a plataforma apenas
# pela interface host, portanto linka no host contra um host falso e permite
# testar handle_key/execute_line/insercao de texto como comportamento.
# Depende apenas de modulos puros ja testados (edit_line, history, modelos
# BLE/Wi-Fi, shell_utils, local_shell, protecao de tela e bateria).
test_shell_session/test.o: test_shell_session.cpp shim/esp_err.h shim/esp_wifi_types.h shim/freertos/FreeRTOS.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/shell_session.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/shell_console.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/edit_line.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_edit_line.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/history.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_history.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/shell_utils.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/local_shell.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/vfs_namespace.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/sd_io_coordinator.o: ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/ssh_line_composer.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_ssh_line_composer.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/screen_protection.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/app_runtime.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/resource_catalog.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/sd_package.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/app_quota.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/battery_protection.o: ../../../components/cyberdeck/src/platform/sensors/cyberdeck_battery_protection.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/battery_status.o: ../../../components/cyberdeck/src/platform/sensors/battery_status.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/wifi_menu.o: ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_menu.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/wifi_state_machine.o: ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_state_machine.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/wifi_survey.o: ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_survey.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/ble_types.o: ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_types.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session/ble_state_machine.o: ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_state_machine.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_session_bin: $(SHELL_SESSION_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) $(SHELL_SESSION_MIG_OBJS) -o $@

.PHONY: test_shell_session
test_shell_session: test_shell_session_bin
	@./test_shell_session_bin


# REQ-HELP-DETAILS / AC-1..AC-6 + REQ-HELP-05 (expanded): `<command> help`
# contract for legacy terms, registered app commands and aliases without
# execution, unknown commands, syntax rejection, general help flags, poweroff
# isolation and normal dispatch preservation; plus the usage-metadata contract
# (edit/demo/netdiag help reveal every declared usage/subcommand), the
# compiled-in inventory filter (3 command-bearing / 7 commandless services /
# shell excluded) and alias/collision resolution.  Links the same production
# TUs as test_shell_session plus the real demo application, whose declared
# usage metadata is exercised as-is.
.PHONY: test_shell_help
test_shell_help/test.o: test_shell_help.cpp shim/esp_err.h shim/esp_wifi_types.h shim/freertos/FreeRTOS.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/shell_session.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/shell_console.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/edit_line.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_edit_line.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/history.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_history.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/shell_utils.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/local_shell.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/vfs_namespace.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/sd_io_coordinator.o: ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/ssh_line_composer.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_ssh_line_composer.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/screen_protection.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/app_runtime.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/resource_catalog.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/sd_package.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/app_quota.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/battery_protection.o: ../../../components/cyberdeck/src/platform/sensors/cyberdeck_battery_protection.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/battery_status.o: ../../../components/cyberdeck/src/platform/sensors/battery_status.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/wifi_menu.o: ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_menu.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/wifi_state_machine.o: ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_state_machine.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/wifi_survey.o: ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_survey.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/ble_types.o: ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_types.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/ble_state_machine.o: ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_state_machine.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help/demo_app.o: $(DEMO_APP_SRC) $(DEMO_APP_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_help_bin: $(SHELL_HELP_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) $(SHELL_HELP_MIG_OBJS) -o $@

test_shell_help: test_shell_help_bin
	@./test_shell_help_bin

# Structural companion of test_shell_help: parses the real registration array
# and the real manifests (system apps, shell, demo, editor, netdiag) and
# reconciles the fixtures/goldens of test_shell_help.cpp against them, so a
# mirrored manifest or a dropped usage line fails even while the behavioral
# golden stays independent.  Source inspection only; no firmware execution.
test_shell_help_contract: test_shell_help_contract.py test_shell_help.cpp \
		../../../components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp \
		../../../components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h \
		../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_app.cpp \
		../../../components/cyberdeck/src/apps/demo/cyberdeck_demo_app.cpp \
		../../../components/cyberdeck/src/apps/editor/cyberdeck_editor_app.cpp \
		../../../components/cyberdeck/src/apps/network/cyberdeck_netdiag_app.cpp \
		help_traceability.md Makefile
	$(PYTHON) test_shell_help_contract.py

test_app_runtime/test.o: test_app_runtime.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_app_runtime/app_runtime.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_app_runtime/resource_catalog.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_app_runtime/sd_package.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_app_runtime/app_quota.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_app_runtime_bin: $(APP_RUNTIME_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -pthread -I../../../components/cyberdeck/include $(APP_RUNTIME_MIG_OBJS) -o $@

.PHONY: test_app_runtime
test_app_runtime: test_app_runtime_bin
	@./test_app_runtime_bin


test_app_runtime_tsan: test_app_runtime.cpp $(APP_RUNTIME_SRC) $(APP_RUNTIME_HDR) \
		$(RESOURCE_CATALOG_SRC) $(SD_PACKAGE_SRC) $(APP_QUOTA_SRC)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=thread -fno-omit-frame-pointer -pthread \
		-I../../../components/cyberdeck/include test_app_runtime.cpp $(APP_RUNTIME_SRC) \
		$(RESOURCE_CATALOG_SRC) $(SD_PACKAGE_SRC) $(APP_QUOTA_SRC) -o test_app_runtime_tsan
	./test_app_runtime_tsan

test_runtime_sdk/test.o: test_runtime_sdk.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_runtime_sdk/app_runtime.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_runtime_sdk/resource_catalog.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_runtime_sdk/sd_package.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_runtime_sdk/app_quota.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_runtime_sdk/app_storage.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_storage.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_runtime_sdk/app_logger.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_logger.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_runtime_sdk/command_catalog.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_command_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_runtime_sdk/local_shell.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_runtime_sdk/vfs_namespace.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_runtime_sdk/sd_io_coordinator.o: ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_runtime_sdk_bin: $(RUNTIME_SDK_MIG_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include $(RUNTIME_SDK_MIG_OBJS) -o $@

.PHONY: test_runtime_sdk
test_runtime_sdk: test_runtime_sdk_bin
	@./test_runtime_sdk_bin


.PHONY: test_storage_facade
test_storage_facade/test.o: test_storage_facade.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_storage_facade/app_runtime.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_storage_facade/resource_catalog.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_storage_facade/sd_package.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_storage_facade/app_quota.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_storage_facade/app_storage.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_storage.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_storage_facade/local_shell.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_storage_facade/vfs_namespace.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_storage_facade/sd_io_coordinator.o: ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_storage_facade_bin: $(STORAGE_FACADE_MIG_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include $(STORAGE_FACADE_MIG_OBJS) -Wl,--wrap=_Z40cyberdeck_local_shell_storage_write_tempPKcS0_St17basic_string_viewIcSt11char_traitsIcEES0_m -Wl,--wrap=_Z35cyberdeck_local_shell_storage_flushPKcS0_St17basic_string_viewIcSt11char_traitsIcEE -Wl,--wrap=_Z36cyberdeck_local_shell_storage_renamePKcS0_St17basic_string_viewIcSt11char_traitsIcEES4_ -Wl,--wrap=_Z45cyberdeck_local_shell_storage_fsync_directoryPKcS0_St17basic_string_viewIcSt11char_traitsIcEE -Wl,--wrap=link -Wl,--wrap=_Z33cyberdeck_local_shell_cat_boundedPKcS0_S0_m -o $@

test_storage_facade: test_storage_facade_bin
	@./test_storage_facade_bin

.PHONY: test_storage_facade

.PHONY: test_phase9_capabilities
test_phase9_capabilities/test.o: test_phase9_capabilities.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_phase9_capabilities/app_runtime.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_phase9_capabilities/app_facades.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_facades.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_phase9_capabilities/window_manager.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_window_manager.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_phase9_capabilities/resource_catalog.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_phase9_capabilities/sd_package.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_phase9_capabilities/app_quota.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_phase9_capabilities_bin: $(PHASE9_CAPABILITIES_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(PHASE9_CAPABILITIES_MIG_OBJS) -o $@

test_phase9_capabilities: test_phase9_capabilities_bin
	@./test_phase9_capabilities_bin

test_demo_app/test.o: test_demo_app.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_demo_app/demo_app.o: ../../../components/cyberdeck/src/apps/demo/cyberdeck_demo_app.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_demo_app/app_runtime.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_demo_app/resource_catalog.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_demo_app/sd_package.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_demo_app/app_quota.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_demo_app_bin: $(DEMO_APP_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(DEMO_APP_MIG_OBJS) -o $@

.PHONY: test_demo_app
test_demo_app: test_demo_app_bin
	@./test_demo_app_bin


# Aplicacao shell de primeiro plano (Fase 7): ownership do console, modos de
# sessao (SSH como modo), politica de despacho e composicao do prompt.  Liga o
# console puro, a aplicacao e o runtime reais contra um host de sessao falso.
.PHONY: test_shell_app
test_shell_app/test.o: test_shell_app.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/shell_console.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/shell_app.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_app.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/shell_session.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/edit_line.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_edit_line.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/history.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_history.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/shell_utils.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/local_shell.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/vfs_namespace.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/sd_io_coordinator.o: ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/ssh_line_composer.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_ssh_line_composer.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/screen_protection.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/app_runtime.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/resource_catalog.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/sd_package.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/app_quota.o: ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/battery_protection.o: ../../../components/cyberdeck/src/platform/sensors/cyberdeck_battery_protection.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/battery_status.o: ../../../components/cyberdeck/src/platform/sensors/battery_status.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/wifi_menu.o: ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_menu.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/wifi_state_machine.o: ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_state_machine.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/ble_types.o: ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_types.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app/ble_state_machine.o: ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_state_machine.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_shell_app_bin: $(SHELL_APP_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -Ishim $(INCLUDES) $(SHELL_APP_MIG_OBJS) -o $@

test_shell_app: test_shell_app_bin
	@./test_shell_app_bin

test_shell_app_contract: test_shell_app_contract.py $(SHELL_APP_SRC) $(SHELL_APP_HDR) \
		$(SHELL_CONSOLE_SRC) $(SHELL_CONSOLE_HDR) ../../../components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp ../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h
	$(PYTHON) test_shell_app_contract.py

.PHONY: test_lvgl_shell_lifecycle_contract
test_lvgl_shell_lifecycle_contract: test_lvgl_shell_lifecycle_contract.py $(SHELL_APP_SRC) \
		../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp \
		../../../components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp
	$(PYTHON) test_lvgl_shell_lifecycle_contract.py

# Fase 8 (REQ/AC-8.1..8.7 / TEST-WM-01..07): politica pura de superficies, foco,
# notificacoes e teardown, mais o adaptador LVGL dono da raiz composta.  O
# alvo liga os TUs reais de producao contra o shim LVGL completo, sem stub.
# A view do header entra porque TEST-REG-8-CHROME prova, com o TU real que a UI
# anexa a system_bar(), que o cromo zero da barra e o que faz o header ocupar a
# linha inteira; ver TEST-REG-8-BAR para as demais regressoes de layout.
.PHONY: test_window_manager
test_window_manager/test.o: test_window_manager.cpp shim/full/lvgl.h Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_window_manager/window_manager.o: $(WINDOW_MANAGER_SRC) $(WINDOW_MANAGER_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_window_manager/window_manager_adapter.o: $(WINDOW_MANAGER_ADAPTER_SRC) $(WINDOW_MANAGER_ADAPTER_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_window_manager/header_view.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_header_view.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_window_manager/wifi_icon.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_window_manager/wifi_indicator.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_indicator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_window_manager/battery_view.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_battery_view.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_window_manager_bin: $(WINDOW_MANAGER_MIG_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include $(WINDOW_MANAGER_MIG_OBJS) -o $@

test_window_manager: test_window_manager_bin
	@./test_window_manager_bin

test_window_manager_contract: test_window_manager_contract.py $(WINDOW_MANAGER_SRC) \
		$(WINDOW_MANAGER_HDR) $(WINDOW_MANAGER_ADAPTER_SRC) $(WINDOW_MANAGER_ADAPTER_HDR) \
		../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp \
		../../../components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp \
		../../../components/cyberdeck/src/platform/display/cyberdeck_header_view.cpp \
		../../../components/cyberdeck/src/apps/demo/cyberdeck_demo_app.cpp \
		../../../components/cyberdeck/CMakeLists.txt \
		../../../tools/coverage_scope_guard.py ../../../code-map.md Makefile \
		shim/full/lvgl.h
	$(PYTHON) test_window_manager_contract.py

test_system_apps_contract: test_system_apps_contract.py ../../../components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp ../../../components/cyberdeck/src/apps/ssh/ssh_client.cpp
	$(PYTHON) test_system_apps_contract.py

test_screenshot_lifecycle_contract: test_screenshot_lifecycle_contract.py ../../../components/cyberdeck/src/apps/screenshot/screenshot_server.cpp ../../../components/cyberdeck/include/apps/screenshot/screenshot_server.h ../../../components/cyberdeck/src/apps/wifi/wifi_mgr.cpp ../../../components/cyberdeck/include/apps/wifi/wifi_mgr.h ../../../components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp
	$(PYTHON) test_screenshot_lifecycle_contract.py

test_serial_lifecycle_contract: test_serial_lifecycle_contract.py ../../../components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp ../../../components/cyberdeck/include/apps/serial/cyberdeck_serial_bridge.h ../../../components/cyberdeck/src/apps/system/cyberdeck_service_ports.cpp ../../../components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp Makefile
	$(PYTHON) test_serial_lifecycle_contract.py

# Logica pura (sem LVGL): coordenador do teardown Wi-Fi/SSH e do gating dos
# timers (contrato em cyberdeck_net_coordinator.h; bug aprovado B: desconexao
# idempotente, offline por erro de socket notificado uma unica vez, callbacks
# inertes apos o teardown e timers que apenas SINALIZAM scan/retry sem
# executar APIs Wi-Fi pesadas dentro do callback). A implementacao
# (cyberdeck_net_coordinator.cpp) sera criada pelo coder junto com a
# integracao em wifi_mgr/ssh_client. Ate la, este target fica vermelho por
# design (TDD): o build falha por MODULO AUSENTE, com a mensagem
# "No rule to make target '.../cyberdeck_net_coordinator.cpp'".
test_net_coordinator/test.o: test_net_coordinator.cpp ../../../components/cyberdeck/include/platform/networking/cyberdeck_net_coordinator.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_net_coordinator/coordinator.o: $(NET_COORDINATOR_SRC) ../../../components/cyberdeck/include/platform/networking/cyberdeck_net_coordinator.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_net_coordinator_bin: $(NET_COORDINATOR_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(NET_COORDINATOR_OBJS) -o $@

.PHONY: test_net_coordinator
test_net_coordinator: test_net_coordinator_bin
	@./test_net_coordinator_bin

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
test_ssh_echo_guard/test.o: test_ssh_echo_guard.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_ssh_echo_guard.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ssh_echo_guard/guard.o: $(ECHO_GUARD_SRC) ../../../components/cyberdeck/include/apps/shell/cyberdeck_ssh_echo_guard.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ssh_echo_guard_bin: $(SSH_ECHO_GUARD_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(SSH_ECHO_GUARD_OBJS) -o $@

.PHONY: test_ssh_echo_guard
test_ssh_echo_guard: test_ssh_echo_guard_bin
	@./test_ssh_echo_guard_bin

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
test_ssh_line_composer/test.o: test_ssh_line_composer.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_ssh_line_composer.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ssh_line_composer/composer.o: $(LINE_COMPOSER_SRC) ../../../components/cyberdeck/include/apps/shell/cyberdeck_ssh_line_composer.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ssh_line_composer_bin: $(SSH_LINE_COMPOSER_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(SSH_LINE_COMPOSER_OBJS) -o $@

.PHONY: test_ssh_line_composer
test_ssh_line_composer: test_ssh_line_composer_bin
	@./test_ssh_line_composer_bin

# Logica pura (sem LVGL): API pura de geracao de BMP 24-bit top-down
# (contrato em screenshot_bmp.h; plano aprovado: cabecalho BMP,
# padding/stride, conversao RGB565->BGR888, chunking sem gaps/
# sobreposicao, validacoes de dimensoes/capacidade). A implementacao
# (screenshot_bmp.cpp) sera criada pelo coder. Ate la, este target
# fica vermelho por design (TDD): o build falha por MODULO AUSENTE,
# com a mensagem "No rule to make target '.../screenshot_bmp.cpp'".
test_screenshot_bmp/test.o: test_screenshot_bmp.cpp ../../../components/cyberdeck/include/apps/screenshot/screenshot_bmp.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_screenshot_bmp/bmp.o: $(SCREENSHOT_BMP_SRC) ../../../components/cyberdeck/include/apps/screenshot/screenshot_bmp.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_screenshot_bmp_bin: $(SCREENSHOT_BMP_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SCREENSHOT_BMP_OBJS) -o $@

.PHONY: test_screenshot_bmp
test_screenshot_bmp: test_screenshot_bmp_bin
	@./test_screenshot_bmp_bin

# Host test for the confined /sdcard local shell.  Include the production
# header directly: a duplicate test-only ABI here can make the test compile
# while linking a different class layout/API than the implementation.
test_local_shell/test.o: test_local_shell.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_local_shell/local_shell.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_local_shell/vfs_namespace.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_local_shell/sd_io_coordinator.o: ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_local_shell_bin: $(LOCAL_SHELL_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LOCAL_SHELL_MIG_OBJS) -pthread -Wl,--wrap=opendir -o $@

.PHONY: test_local_shell
test_local_shell: test_local_shell_bin
	@./test_local_shell_bin
	@python3 test_music_scan_opendir_contract.py


$(SAVE_SIDECAR_BIN): $(SAVE_SIDECAR_OBJS) save_sidecar_traceability.md Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(SAVE_SIDECAR_OBJS) -pthread -o $@

test_save_sidecar_recovery: $(SAVE_SIDECAR_BIN)
	./$(SAVE_SIDECAR_BIN)

# Host regression for the dedicated cat API.  This deliberately bypasses the
# generic shell object so a multiline result is checked at the worker's source
# seam, including CRLF, tabs, UTF-8, exact 12288-byte output, embedded NUL,
# and a suffix after the first newline for the UI sanitizer.
test_cat_multiline/test.o: test_cat_multiline.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_cat_multiline/local_shell.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_cat_multiline/vfs_namespace.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_cat_multiline/sd_io_coordinator.o: ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_cat_multiline_bin: $(CAT_MULTILINE_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(CAT_MULTILINE_MIG_OBJS) -pthread -o $@

.PHONY: test_cat_multiline
test_cat_multiline: test_cat_multiline_bin
	@./test_cat_multiline_bin


# RED: the menu and local shell must render one shared help catalog.  The
# target links both real production implementations; no production edit is
# implied by this test registration.
test_help_unification/test.o: test_help_unification.cpp contracts/cyberdeck_help.h shim/lvgl.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_help_unification/shell_utils.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_help_unification/local_shell.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_help_unification/vfs_namespace.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_help_unification/sd_io_coordinator.o: ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -MF $@.d -c $< -o $@

test_help_unification_bin: $(HELP_UNIFICATION_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(HELP_UNIFICATION_MIG_OBJS) -pthread -o $@

.PHONY: test_help_unification
test_help_unification: test_help_unification_bin
	@./test_help_unification_bin


# RED structural companion: rejects private literal help lists in the two
# shells and in the non-linkable LVGL UI.  It performs source inspection only.
help_unification_contract: $(HELP_UNIFICATION_CONTRACT) contracts/cyberdeck_help.h $(SHELL_UTILS_SRC) $(SHELL_HELP_HDR) $(LOCAL_SHELL_SRC) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h ../../../code-map.md Makefile
	$(PYTHON) $(HELP_UNIFICATION_CONTRACT)

test_orientation/test.o: test_orientation.cpp ../../../components/cyberdeck/include/platform/sensors/orientation.h shim/lvgl.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_orientation/orientation.o: $(ORIENTATION_SRC) ../../../components/cyberdeck/include/platform/sensors/orientation.h shim/lvgl.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_orientation_bin: $(ORIENTATION_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(ORIENTATION_OBJS) -o $@

.PHONY: test_orientation
test_orientation: test_orientation_bin
	@./test_orientation_bin

# TDD RED: contrato puro de bateria. A API de teste exige tensao real
# (bus_voltage_mv, 6000..8230 mV), estados charging/discharging/neutral,
# zero/indeterminado como neutral e ausencia somente por presenca explicita;
# a producao correspondente ainda precisa da correcao do coder.
test_battery_contract: test_battery_contract.cpp $(BATTERY_PURE_SRC) $(BATTERY_PURE_HDR) $(BATTERY_CONTRACT_HDR)
	$(MAKE) --no-print-directory $(BATTERY_BIN)
	@./$(BATTERY_BIN)

test_battery_contract/test.o: test_battery_contract.cpp $(BATTERY_CONTRACT_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_battery_contract/status.o: $(BATTERY_PURE_SRC) $(BATTERY_PURE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

$(BATTERY_BIN): $(BATTERY_CONTRACT_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(BATTERY_CONTRACT_OBJS) -o $@

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
test_battery_ui_contract: test_battery_ui_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(HEADER_VIEW_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_header_view.h $(WIFI_ICON_SRC) $(WIFI_ICON_HDR) ../../../components/cyberdeck/include/platform/sensors/battery_status.h $(BATTERY_CONTRACT_HDR) ../../../main/app_main.cpp ../../../code-map.md ../../../docs/ARCHITECTURE.md ../../../docs/ARCHITECTURE.pt-BR.md Makefile
	$(PYTHON) test_battery_ui_contract.py

# Pure host contract for battery state/protection policy.  It links the real
# production policy and keeps the public target separate from its executable
# name, avoiding a circular make dependency while remaining hardware-free.
test_battery_protection: $(BATTERY_PROTECTION_BIN)
	@./$(BATTERY_PROTECTION_BIN)

test_battery_protection/test.o: test_battery_protection.cpp contracts/cyberdeck_battery_protection.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_battery_protection/battery_protection.o: ../../../components/cyberdeck/src/platform/sensors/cyberdeck_battery_protection.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_battery_protection_bin: $(BATTERY_PROTECTION_MIG_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include $(BATTERY_PROTECTION_MIG_OBJS) -o $@


# Contrato host da view pura do indicador de energia: mapeamento total de
# battery_state x charge_signal x disponibilidade x percentual para visivel,
# percentual e glyph semantico. Ausencia/presenca vencem o sinal do carregador
# (um CHG_STAT preso em low nao fabrica bateria), `charging` usa glyph de carga
# com percentual, `absent` usa somente o glyph externo sem percentual, leitura
# invalida/`unknown` fica oculto e o glyph nunca e escolhido pelo nivel.
test_battery_view: $(BATTERY_VIEW_BIN)
	@./$(BATTERY_VIEW_BIN)

$(BATTERY_VIEW_BIN): $(BATTERY_VIEW_OBJS) $(BATTERY_VIEW_TEST) $(BATTERY_VIEW_CONTRACT_HDR) $(BATTERY_VIEW_SRC) $(BATTERY_VIEW_HDR) $(BATTERY_PROTECTION_CONTRACT_HDR) $(BATTERY_PROTECTION_SRC) $(BATTERY_PROTECTION_HDR) Makefile
	$(MAKE) --no-print-directory $(BATTERY_VIEW_OBJS)
	$(CXX) $(CXXFLAGS) $(BATTERY_VIEW_OBJS) -o $@

test_battery_view/test.o: $(BATTERY_VIEW_TEST) $(BATTERY_VIEW_CONTRACT_HDR) $(BATTERY_PROTECTION_CONTRACT_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_battery_view/view.o: $(BATTERY_VIEW_SRC) $(BATTERY_VIEW_HDR) $(BATTERY_VIEW_CONTRACT_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_battery_view/protection.o: $(BATTERY_PROTECTION_SRC) $(BATTERY_PROTECTION_HDR) $(BATTERY_PROTECTION_CONTRACT_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

# Companheiro estrutural da parte nao linkavel: a view continua pura (sem
# ESP-IDF/FreeRTOS/LVGL/I2C/NVS/BSP), esta registrada no build do firmware, e o
# LVGL so aplica o resultado (sem `charge_class::`/`battery_state::` no refresh,
# tabela de glyph fixo, percentual vazio quando show_percentage e falso, grupo
# oculto quando visible e falso, sem I2C/NVS/reader diretos).
test_battery_view_contract: $(BATTERY_VIEW_CONTRACT) $(BATTERY_VIEW_SRC) $(BATTERY_VIEW_HDR) $(BATTERY_VIEW_CONTRACT_HDR) $(BATTERY_VIEW_TEST) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/include/platform/sensors/cyberdeck_battery_protection.h ../../../components/cyberdeck/include/platform/sensors/battery_protection.h ../../../components/cyberdeck/CMakeLists.txt ../../../code-map.md Makefile
	$(PYTHON) $(BATTERY_VIEW_CONTRACT)

# Structural companion for expander-B, NVS, UI timer/snapshot, shell and
# ui.type.  It reads production source and fails closed when the feature is
# absent; it never opens hardware or a simulator.
test_battery_protection_contract: $(BATTERY_PROTECTION_CONTRACT) $(BATTERY_PROTECTION_CONTRACT_HDR) $(BATTERY_ADAPTER_SRC) $(BATTERY_ADAPTER_HDR) ../../../components/cyberdeck/CMakeLists.txt ../../../main/app_main.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h ../../../components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp ../../../tests/host/keymap/test_serial_ndjson_dispatch.cpp ../../../code-map.md ../../../.gitignore ../../../docs/ARCHITECTURE.md ../../../docs/ARCHITECTURE.pt-BR.md Makefile
	$(PYTHON) $(BATTERY_PROTECTION_CONTRACT)

# Structural host-side contract for boot and initial IMU orientation. The
# sources are inspected directly rather than duplicated or linked against
# ESP-IDF: this checks boot order, safe initial fallback, and the handoff to
# the later rotation path.
test_boot_sequence: test_boot_sequence.py ../../../main/app_main.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(LOCAL_SHELL_HDR) save_sidecar_traceability.md
	$(PYTHON) test_boot_sequence.py

# Host-only contracts for the approved remaining OS transformation backlog.
# The missing SDK/catalog modules intentionally make this target RED until the
# coder implements the reviewed contracts; no production shim is supplied.
.PHONY: test_os_transformation_backlog_contract
test_os_transformation_backlog_contract: test_os_transformation_backlog_contract.py os_transformation_traceability.md \
		../../../docs/OS-TRANSFORMATION-PLAN.pt-BR.md ../../../main/app_main.cpp \
		../../../components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h \
		../../../components/cyberdeck/include/apps/runtime/cyberdeck_app_logger.h \
		../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp \
		../../../components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp \
		../../../components/cyberdeck/src/apps/ssh/ssh_client.cpp \
		../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp \
		../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp
	$(PYTHON) test_os_transformation_backlog_contract.py

.PHONY: test_resource_sd_quota_contract
test_resource_sd_quota_contract: test_resource_sd_quota_contract.py os_transformation_traceability.md Makefile \
		../../../docs/OS-TRANSFORMATION-PLAN.pt-BR.md \
		../../../components/cyberdeck/include/apps/runtime/cyberdeck_resource_catalog.h \
		../../../components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp \
		../../../components/cyberdeck/include/apps/runtime/cyberdeck_sd_package.h \
		../../../components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp \
		../../../components/cyberdeck/include/apps/runtime/cyberdeck_app_quota.h \
		../../../components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp
	$(PYTHON) test_resource_sd_quota_contract.py

test_display_lock_contract: test_display_lock_contract.py ../../../main/app_main.cpp $(SERIAL_BRIDGE_SRC) Makefile
	$(PYTHON) test_display_lock_contract.py

.PHONY: test_music_player_ui_contract
test_music_player_ui_contract: test_music_player_ui_contract.py \
		lv_symbol_map.py lv_symbol_codepoints.json \
		../../../components/cyberdeck/src/platform/display/cyberdeck_music_view.cpp \
		../../../components/cyberdeck/src/platform/display/cyberdeck_music_command_input.cpp \
		../../../components/cyberdeck/src/platform/display/cyberdeck_header_view.cpp \
		../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp \
		../../../components/cyberdeck/src/apps/music/cyberdeck_music_app.cpp \
		../../../components/cyberdeck/src/platform/display/cyberdeck_font.c Makefile
	$(PYTHON) test_music_player_ui_contract.py

# Drift gate between the versioned `LV_SYMBOL_*` map and the pinned LVGL header.
# Deliberately NOT part of the aggregate `test`: `managed_components/` is
# gitignored and only materializes after a CMake warm-up, so the aggregate must
# stay runnable in a clean checkout. CI runs the strict variant after "Prepare
# ESP-Hosted CPU-staging"; without the header the local target prints SKIP and
# exits 0.
.PHONY: verify_lv_symbol_codepoints
verify_lv_symbol_codepoints: lv_symbol_map.py lv_symbol_codepoints.json Makefile
	$(PYTHON) lv_symbol_map.py --verify

# Strict variant used by CI: an absent `lv_symbol_def.h` is a failure instead of
# SKIP, so a warm-up that does not materialize `managed_components/` cannot turn
# this gate into a green no-op.
.PHONY: verify_lv_symbol_codepoints_strict
verify_lv_symbol_codepoints_strict: lv_symbol_map.py lv_symbol_codepoints.json Makefile
	$(PYTHON) lv_symbol_map.py --verify --strict

.PHONY: test_lvgl_dispatch_state
test_lvgl_dispatch_state: test_lvgl_dispatch_state_bin
	@./test_lvgl_dispatch_state_bin

test_lvgl_dispatch_state_bin: test_lvgl_dispatch_state.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_lvgl_dispatch_state.cpp ../../../components/cyberdeck/include/platform/display/cyberdeck_lvgl_dispatch_state.h Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_lvgl_dispatch_state.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_lvgl_dispatch_state.cpp -o $@

imu_sensor_contract: imu_sensor_contract.py ../../../components/cyberdeck/src/platform/sensors/imu_reader.cpp
	$(PYTHON) imu_sensor_contract.py

# Structural host-side TDD contract for the physical keyboard/UI seam.  The
# UI is not linkable on host; this test checks the real source and intentionally
# remains RED until the bounded lv_async_call handoff is implemented.
test_keyboard_input_contract: test_keyboard_input_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/platform/input/cyberdeck_keyboard_dispatch.cpp
	$(PYTHON) test_keyboard_input_contract.py

# Structural host-side contract for every partial cyberdeck_ui_init resource
# allocation path.  It checks complete cleanup and zeroing of all handles.
test_ui_resource_contract: test_ui_resource_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/platform/input/cyberdeck_keyboard_dispatch.cpp Makefile
	$(PYTHON) test_ui_resource_contract.py

test_local_prompt_contract: local_prompt_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(SHELL_CONSOLE_SRC) $(SHELL_CONSOLE_HDR) $(SESSION_SRC) $(TERMINAL_SCROLLBACK_SRC) $(TERMINAL_SCROLLBACK_HDR)
	$(PYTHON) local_prompt_contract.py

test_cat_contract: cat_contract.py ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_cat_worker.h
	$(PYTHON) cat_contract.py

test_cat_multiline_contract: test_cat_multiline_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp
	$(PYTHON) test_cat_multiline_contract.py

test_cat_lifecycle_sanitization_contract: test_cat_lifecycle_sanitization_contract.py ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_cat_worker.h
	$(PYTHON) test_cat_lifecycle_sanitization_contract.py

test_cat_start_teardown_start_contract: test_cat_start_teardown_start_contract.py ../../../components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp
	$(PYTHON) test_cat_start_teardown_start_contract.py

test_cat_stack_footprint_contract: test_cat_stack_footprint_contract.py ../../../components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp
	$(PYTHON) test_cat_stack_footprint_contract.py

test_cat_worker_path_safety_contract: test_cat_worker_path_safety_contract.py ../../../components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_local_shell.h
	$(PYTHON) test_cat_worker_path_safety_contract.py

local_shell_security_contract: local_shell_security_contract.py ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp
	$(PYTHON) local_shell_security_contract.py

local_shell_tokenizer_contract: local_shell_tokenizer_contract.py ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp ../../../components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_cat_worker.h $(LOCAL_SHELL_HDR)
	$(PYTHON) local_shell_tokenizer_contract.py

local_shell_namespace_contract: $(LOCAL_SHELL_NAMESPACE_CONTRACT) $(LOCAL_SHELL_SRC) $(LOCAL_SHELL_HDR) $(VFS_NAMESPACE_SRC) $(VFS_NAMESPACE_HDR) ../../../components/cyberdeck/CMakeLists.txt ../../../code-map.md
	$(PYTHON) $(LOCAL_SHELL_NAMESPACE_CONTRACT)

test_prompt_behavior: test_prompt_behavior.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(SHELL_CONSOLE_SRC) $(SHELL_CONSOLE_HDR) $(APP_RUNTIME_SRC) $(RESOURCE_CATALOG_SRC) $(SD_PACKAGE_SRC) $(APP_QUOTA_SRC) $(LOCAL_SHELL_SRC) $(LOCAL_SHELL_HDR) $(SD_IO_COORDINATOR_SRC) $(TERMINAL_FILTER_SRC) $(TERMINAL_FILTER_HDR) $(TERMINAL_SCROLLBACK_SRC) $(TERMINAL_SCROLLBACK_HDR)
	$(PYTHON) test_prompt_behavior.py

test_wifi_dispatch_contract: test_wifi_dispatch_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/platform/input/cyberdeck_keyboard_dispatch.cpp
	$(PYTHON) test_wifi_dispatch_contract.py

wifi_enter_dispatch_contract: wifi_enter_dispatch_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/platform/input/cyberdeck_keyboard_dispatch.cpp
	$(PYTHON) wifi_enter_dispatch_contract.py

test_wifi_enter_routing_contract: test_wifi_enter_routing_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) test_wifi_enter_routing_contract.py

test_wifi_audit_contract: test_wifi_audit_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(WIFI_AUDIT_SRC) $(WIFI_AUDIT_PERSISTENCE_SRC) $(WIFI_AUDIT_PERSISTENCE_HDR) $(WIFI_AUDIT_PERSISTENCE_CONTRACT_HDR) ../../../sdkconfig.defaults ../../../components/cyberdeck/CMakeLists.txt ../../../code-map.md Makefile
	$(PYTHON) test_wifi_audit_contract.py

# Structural contract for the eight-target incremental migration batch.
.PHONY: test_incremental_batch_contract
test_incremental_batch_contract: test_incremental_batch_contract.py Makefile ../../../code-map.md
	$(PYTHON) test_incremental_batch_contract.py

.PHONY: test_incremental_final_contract
test_incremental_final_contract: test_incremental_final_contract.py incremental_final_baseline.json Makefile
	$(PYTHON) test_incremental_final_contract.py

# USB Serial-JTAG NDJSON manual bridge (REQ-002/003/005/006/007/008/009) — TDD RED: produção ausente
.PHONY: test_serial_ndjson_dispatch
test_serial_ndjson_dispatch/test.o: test_serial_ndjson_dispatch.cpp $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_serial_ndjson_dispatch/bridge.o: $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_serial_ndjson_dispatch_bin: $(SERIAL_NDJSON_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) $(SERIAL_NDJSON_OBJS) -o $@
test_serial_ndjson_dispatch: test_serial_ndjson_dispatch_bin
	@./test_serial_ndjson_dispatch_bin

.PHONY: test_serial_screen_dump
test_serial_screen_dump/test.o: test_serial_screen_dump.cpp $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_serial_screen_dump/bridge.o: $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_serial_screen_dump/screenshot-bmp.o: $(SCREENSHOT_BMP_SRC) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_serial_screen_dump_bin: $(SERIAL_SCREEN_DUMP_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) $(SERIAL_SCREEN_DUMP_OBJS) -o $@
test_serial_screen_dump: test_serial_screen_dump_bin
	@./test_serial_screen_dump_bin

.PHONY: test_serial_cli_tolerance
test_serial_cli_tolerance/test.o: test_serial_cli_tolerance.cpp $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_serial_cli_tolerance/bridge.o: $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_serial_cli_tolerance_bin: $(SERIAL_CLI_TOLERANCE_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) $(SERIAL_CLI_TOLERANCE_OBJS) -o $@
test_serial_cli_tolerance: test_serial_cli_tolerance_bin
	@./test_serial_cli_tolerance_bin

.PHONY: test_serial_sysinfo_wifi
test_serial_sysinfo_wifi/test.o: test_serial_sysinfo_wifi.cpp $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_serial_sysinfo_wifi/bridge.o: $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_serial_sysinfo_wifi_bin: $(SERIAL_SYSINFO_WIFI_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) $(SERIAL_SYSINFO_WIFI_OBJS) -o $@
test_serial_sysinfo_wifi: test_serial_sysinfo_wifi_bin
	@./test_serial_sysinfo_wifi_bin

# term.dump (REQ-TERM-001 / AC-TERM-001..005): type conhecido, envelope/rid,
# resultado bounded e truncamento UTF-8-safe na camada host-testavel.
.PHONY: test_serial_term_dump
test_serial_term_dump/test.o: test_serial_term_dump.cpp $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_serial_term_dump/bridge.o: $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_serial_term_dump_bin: $(SERIAL_TERM_DUMP_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) $(SERIAL_TERM_DUMP_OBJS) -o $@
test_serial_term_dump: test_serial_term_dump_bin
	@./test_serial_term_dump_bin

# exec_term_dump/cyberdeck_ui_term_dump vivem sob ESP_PLATFORM: o contrato e
# estrutural sobre as fontes reais (sem link de hardware).
test_term_dump_contract: test_term_dump_contract.py $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/include/platform/display/cyberdeck_ui.h ../../../tools/cyberdeck_cli.py Makefile
	$(PYTHON) test_term_dump_contract.py

test_term_dump_cli: test_term_dump_cli.py ../../../tools/cyberdeck_cli.py
	$(PYTHON) test_term_dump_cli.py

# fs.write seguro (REQ-001..REQ-011) — TDD RED: produção ausente
.PHONY: test_fs_write_dispatch
test_fs_write_dispatch/test.o: test_fs_write_dispatch.cpp $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_fs_write_dispatch/bridge.o: $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@
test_fs_write_dispatch_bin: $(FS_WRITE_DISPATCH_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) $(FS_WRITE_DISPATCH_OBJS) -o $@
test_fs_write_dispatch: test_fs_write_dispatch_bin
	@./test_fs_write_dispatch_bin

.PHONY: test_cat_worker
test_cat_worker/test.o: test_cat_worker.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -DCYBERDECK_CAT_CORE_ONLY -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_cat_worker/cat_worker.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -DCYBERDECK_CAT_CORE_ONLY -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_cat_worker/local_shell.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -DCYBERDECK_CAT_CORE_ONLY -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_cat_worker/vfs_namespace.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -DCYBERDECK_CAT_CORE_ONLY -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_cat_worker/sd_io_coordinator.o: ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -DCYBERDECK_CAT_CORE_ONLY -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_cat_worker_bin: $(CAT_WORKER_MIG_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -DCYBERDECK_CAT_CORE_ONLY -I../../../components/cyberdeck/include $(CAT_WORKER_MIG_OBJS) -o $@

test_cat_worker: test_cat_worker_bin
	@./test_cat_worker_bin

.PHONY: test_ble_background
test_ble_background/test.o: test_ble_background.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_background/ble_background.o: ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_background.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_background/ble_state_machine.o: ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_state_machine.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_background/ble_types.o: ../../../components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_types.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_background_bin: $(BLE_BACKGROUND_MIG_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim -I../../../components/cyberdeck/include $(BLE_BACKGROUND_MIG_OBJS) -o $@

test_ble_background: test_ble_background_bin
	@./test_ble_background_bin

.PHONY: test_keyboard_dispatch
test_keyboard_dispatch/test.o: test_keyboard_dispatch.cpp shim/full/lvgl.h shim/full/esp_lvgl_port.h shim/full/freertos/FreeRTOS.h shim/full/freertos/queue.h shim/full/freertos/semphr.h Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_keyboard_dispatch/keyboard_dispatch.o: ../../../components/cyberdeck/src/platform/input/cyberdeck_keyboard_dispatch.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_keyboard_dispatch_bin: $(KEYBOARD_DISPATCH_MIG_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -Ishim/full -I../../../components/cyberdeck/include $(KEYBOARD_DISPATCH_MIG_OBJS) -o $@

test_keyboard_dispatch: test_keyboard_dispatch_bin
	@./test_keyboard_dispatch_bin

.PHONY: test_display_views
test_display_views/test.o: test_display_views.cpp shim/full/lvgl.h Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_display_views/header_view.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_header_view.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_display_views/terminal_view.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_display_views/editor_view.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_editor_view.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_display_views/music_view.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_music_view.cpp ../../../components/cyberdeck/include/platform/display/cyberdeck_music_view.h Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_display_views/wifi_icon.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_display_views/wifi_indicator.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_indicator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_display_views/battery_view.o: ../../../components/cyberdeck/src/platform/display/cyberdeck_battery_view.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_display_views_bin: $(DISPLAY_VIEWS_MIG_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full -I../../../components/cyberdeck/include $(DISPLAY_VIEWS_MIG_OBJS) -o $@

test_display_views: test_display_views_bin
	@./test_display_views_bin

test_coverage_scope_guard: $(SCOPE_GUARD_TEST) ../../../tools/coverage_scope_guard.py
	$(PYTHON) $(SCOPE_GUARD_TEST)

test_fs_write_cli: test_fs_write_cli.py ../../../tools/cyberdeck_cli.py
	$(PYTHON) test_fs_write_cli.py

test_fs_write_contract: test_fs_write_contract.py $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) ../../../tools/cyberdeck_cli.py
	$(PYTHON) test_fs_write_contract.py

# ---------------------------------------------------------------------------
# BLE (REQ-BLE-001..011 / AC-BLE-001..011).
#
# The BLE radio belongs to the ESP32-C6 coprocessor reached through esp_hosted,
# so none of these targets can link a stack header.  Each one links a pure
# production module against the matching test-owned contract in contracts/, and
# the contract header re-includes the production header as soon as it exists.
# No ESP-IDF, LVGL, NVS, simulator, Serial Automation Bridge or hardware is
# touched here. Isolated objects retain compiler-discovered header dependencies.
#
# -Icontracts comes first on purpose: the test includes the bare contract name,
#   and the contract then binds to the production ABI when it exists.
test_ble_types/test.o: test_ble_types.cpp $(BLE_TYPES_HDR) $(BLE_TYPES_CONTRACT_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_types/types.o: $(BLE_TYPES_SRC) $(BLE_TYPES_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_state_machine/test.o: test_ble_state_machine.cpp $(BLE_STATE_HDR) $(BLE_STATE_CONTRACT_HDR) $(BLE_TYPES_CONTRACT_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_state_machine/state.o: $(BLE_STATE_SRC) $(BLE_STATE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_state_machine/types.o: $(BLE_TYPES_SRC) $(BLE_TYPES_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_event_dispatch/test.o: test_ble_event_dispatch.cpp $(BLE_EVENTS_HDR) $(BLE_EVENTS_CONTRACT_HDR) $(BLE_STATE_CONTRACT_HDR) $(BLE_TYPES_CONTRACT_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_event_dispatch/events.o: $(BLE_EVENTS_SRC) $(BLE_EVENTS_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_event_dispatch/types.o: $(BLE_TYPES_SRC) $(BLE_TYPES_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_store/test.o: test_ble_store.cpp $(BLE_STORE_HDR) $(BLE_STORE_CONTRACT_HDR) $(BLE_TYPES_CONTRACT_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_store/store.o: $(BLE_STORE_SRC) $(BLE_STORE_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_store/types.o: $(BLE_TYPES_SRC) $(BLE_TYPES_HDR) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_ble_command_parse/test.o: test_ble_command_parse.cpp contracts/cyberdeck_help.h $(SHELL_HELP_HDR) ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h shim/lvgl.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

test_ble_command_parse/shell-utils.o: $(SHELL_UTILS_SRC) $(SHELL_HELP_HDR) ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -MF $@.d -c $< -o $@

$(BLE_TYPES_BIN): $(BLE_TYPES_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include $(BLE_TYPES_OBJS) -o $@

$(BLE_STATE_BIN): $(BLE_STATE_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include $(BLE_STATE_OBJS) -o $@

$(BLE_EVENTS_BIN): $(BLE_EVENTS_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include $(BLE_EVENTS_OBJS) -o $@

$(BLE_STORE_BIN): $(BLE_STORE_OBJS) Makefile
	$(CXX) $(CXXFLAGS) -Icontracts -I../../../components/cyberdeck/include $(BLE_STORE_OBJS) -o $@

$(BLE_COMMAND_BIN): $(BLE_COMMAND_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(BLE_COMMAND_OBJS) -o $@

.PHONY: test_ble_types test_ble_state_machine test_ble_event_dispatch test_ble_store test_ble_command_parse
test_ble_types: $(BLE_TYPES_BIN)
	./$(BLE_TYPES_BIN)

test_ble_state_machine: $(BLE_STATE_BIN)
	./$(BLE_STATE_BIN)

test_ble_event_dispatch: $(BLE_EVENTS_BIN)
	./$(BLE_EVENTS_BIN)

test_ble_store: $(BLE_STORE_BIN)
	./$(BLE_STORE_BIN)

# The shell parser is already host-linkable, so this target exercises the real
# production cyberdeck_parse_command()/cyberdeck_help_text() instead of
# inspecting the source.  It is RED until CYBERDECK_CMD_BLUETOOTH_SEARCH and
# CYBERDECK_CMD_BLUETOOTH_PAIRED exist and the catalog gains the bluetooth row.
test_ble_command_parse: $(BLE_COMMAND_BIN)
	./$(BLE_COMMAND_BIN)

# Structural companion for the non-linkable part: composition, CMake/app_main
# wiring, the FreeRTOS task boundary, the log-secret rule, the REQ-BLE-011
# classic-Bluetooth exclusion and the REQ/AC traceability.  Source inspection
# only; it never opens hardware, a radio or the Serial Automation Bridge.
test_ble_integration_contract: test_ble_integration_contract.py $(BLE_MGR_SRC) $(BLE_MGR_HDR) $(BLE_STATE_SRC) $(BLE_STATE_HDR) $(BLE_TYPES_CONTRACT_HDR) $(BLE_STATE_CONTRACT_HDR) $(BLE_EVENTS_CONTRACT_HDR) $(BLE_STORE_CONTRACT_HDR) ../../../components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h ../../../components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../main/app_main.cpp ../../../components/m5stack_tab5/src/bsp_display.c ../../../components/m5stack_tab5/include/bsp/m5stack_tab5.h ../../../components/esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c ../../../components/cyberdeck/src/apps/music/cyberdeck_music_player.cpp ../../../components/cyberdeck/CMakeLists.txt ../../../main/idf_component.yml ../../../sdkconfig.defaults ../../../docs/ARCHITECTURE.md ../../../docs/ARCHITECTURE.pt-BR.md ../../../README.md ../../../README.pt-BR.md ../../../code-map.md ../../../.gitignore
	$(PYTHON) test_ble_integration_contract.py

test_ble_scan_contract: test_ble_scan_contract.py $(BLE_MGR_SRC) $(BLE_MGR_HDR) $(BLE_TYPES_HDR) $(BLE_STORE_HDR) $(BLE_EVENTS_HDR)
	$(PYTHON) test_ble_scan_contract.py

test_ble_pre_sync_contract: test_ble_pre_sync_contract.py $(BLE_MGR_SRC) $(BLE_MGR_HDR) $(BLE_EVENTS_HDR)
	$(PYTHON) test_ble_pre_sync_contract.py

test_ble_ui_timeout_contract: test_ble_ui_timeout_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) test_ble_ui_timeout_contract.py

test_ble_adv_types_contract: test_ble_adv_types_contract.py $(BLE_MGR_SRC)
	$(PYTHON) test_ble_adv_types_contract.py

test_ble_review_regressions: test_ble_review_regressions.py $(BLE_MGR_SRC) $(BLE_MGR_HDR) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(BLE_STORE_SRC) $(BLE_STORE_HDR)
	$(PYTHON) test_ble_review_regressions.py

test_ble_runtime_regressions: test_ble_runtime_regressions.py $(BLE_MGR_SRC) $(BLE_MGR_HDR) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) test_ble_runtime_regressions.py

test_ble_ui_render_contract: test_ble_ui_render_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp $(BLE_TYPES_SRC) $(BLE_TYPES_HDR) $(BLE_STATE_SRC) $(BLE_STATE_HDR) $(BLE_EVENTS_SRC) $(BLE_EVENTS_HDR) test_ble_types.cpp Makefile ../../../code-map.md
	$(PYTHON) test_ble_ui_render_contract.py

test_ble_header_contract: test_ble_header_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp ../../../components/cyberdeck/src/platform/display/cyberdeck_font.c $(BLE_STATE_SRC) $(BLE_STATE_HDR) Makefile ../../../code-map.md
	$(PYTHON) test_ble_header_contract.py

test_ble_auth_contract: test_ble_auth_contract.py $(BLE_MGR_SRC) $(BLE_MGR_HDR) $(BLE_STATE_SRC) $(BLE_STATE_HDR) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp Makefile
	$(PYTHON) test_ble_auth_contract.py

test_ble_hid_contract: test_ble_hid_contract.py $(BLE_MGR_SRC) $(BLE_MGR_HDR) $(BLE_EVENTS_SRC) $(BLE_EVENTS_HDR) Makefile
	$(PYTHON) test_ble_hid_contract.py

test_ble_connection_lifecycle_contract: test_ble_connection_lifecycle_contract.py $(BLE_MGR_SRC) $(BLE_STATE_SRC) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp Makefile
	$(PYTHON) test_ble_connection_lifecycle_contract.py

test_ble_background_contract: test_ble_background_contract.py $(BLE_MGR_SRC) $(BLE_STATE_SRC) $(BLE_BACKGROUND_SRC) $(BLE_BACKGROUND_HDR) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp Makefile
	$(PYTHON) test_ble_background_contract.py

test_ble_bond_name_contract: test_ble_bond_name_contract.py $(BLE_MGR_SRC) $(BLE_TYPES_SRC) $(BLE_STORE_SRC) $(BLE_EVENTS_SRC) $(BLE_BACKGROUND_SRC) $(BLE_BACKGROUND_HDR) ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp Makefile
	$(PYTHON) test_ble_bond_name_contract.py

test_net_session_ipc_contract: test_net_session_ipc_contract.py ../../../components/cyberdeck/src/apps/wifi/wifi_mgr.cpp ../../../components/cyberdeck/src/apps/ssh/ssh_client.cpp Makefile
	$(PYTHON) test_net_session_ipc_contract.py

# BUG-SSH-001: a task SSH nao linka no host (ESP-IDF/FreeRTOS/libssh), portanto
# este contrato inspeciona o fonte de producao: stack externa em PSRAM
# (xTaskCreateWithCaps + MALLOC_CAP_SPIRAM), 12 autoexclusoes com
# vTaskDeleteWithCaps(NULL), ramo de falha observavel e documentacao no mapa.
test_ssh_task_alloc_contract: test_ssh_task_alloc_contract.py ../../../components/cyberdeck/src/apps/ssh/ssh_client.cpp ../../../code-map.md Makefile
	$(PYTHON) test_ssh_task_alloc_contract.py

# Focused aggregate for the boot/reconnect/name requirements; intentionally
# excludes the complete host suite.
test_ble_bond_reconnect: $(BLE_STATE_BIN) $(BLE_EVENTS_BIN) $(BLE_STORE_BIN)
	@./$(BLE_STATE_BIN)
	@./$(BLE_EVENTS_BIN)
	@./$(BLE_STORE_BIN)
	@$(MAKE) --no-print-directory test_ble_background_contract
	@$(MAKE) --no-print-directory test_ble_bond_name_contract


BINS += test_poweroff_bin
BINS += test_cat_worker_runtime
BINS += test_known_hosts_file_bin

test_known_hosts_file/test.o: test_known_hosts_file.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_known_hosts_file/known_hosts_file.o: ../../../components/cyberdeck/src/apps/ssh/cyberdeck_known_hosts_file.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_known_hosts_file/sd_io_coordinator.o: ../../../components/cyberdeck/src/platform/storage/cyberdeck_sd_io_coordinator.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_known_hosts_file_bin: $(KNOWN_HOSTS_FILE_MIG_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -I../../../components/cyberdeck/include $(KNOWN_HOSTS_FILE_MIG_OBJS) -Wl,--wrap=write -Wl,--wrap=fsync -Wl,--wrap=close -Wl,--wrap=fclose -o $@

.PHONY: test_known_hosts_file
test_known_hosts_file: test_known_hosts_file_bin
	@./test_known_hosts_file_bin


CAT_RUNTIME_OBJS := test_cat_runtime_local_shell.o test_cat_runtime_vfs.o test_cat_runtime_sd_io.o

# Keep dependency gcov instrumentation aligned with their other host targets.
# UBSan adds compiler-generated branches to filesystem/STL code; the real
# worker, task adapters and lifetime harness remain ASan/UBSan instrumented.
test_cat_runtime_local_shell.o: ../../../components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp $(LOCAL_SHELL_HDR) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -DESP_PLATFORM -I../../../components/cyberdeck/include -c $< -o $@

test_cat_runtime_vfs.o: $(VFS_NAMESPACE_SRC) $(VFS_NAMESPACE_HDR) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -I../../../components/cyberdeck/include -c $< -o $@

test_cat_runtime_sd_io.o: $(SD_IO_COORDINATOR_SRC) $(SD_IO_COORDINATOR_HDR) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -pthread -I../../../components/cyberdeck/include -c $< -o $@

test_cat_worker_runtime: test_cat_worker_runtime.cpp $(CAT_WORKER_SRC) $(CAT_RUNTIME_OBJS) event_log_shim/event_log_shim.cpp cat_runtime_shim/freertos/queue.h cat_runtime_shim/freertos/task.h cat_runtime_shim/freertos/semphr.h cat_runtime_shim/freertos/FreeRTOS.h cat_runtime_shim/lvgl.h cat_runtime_shim/esp_lvgl_port.h
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -DESP_PLATFORM -Icat_runtime_shim -Ievent_log_shim -Ishim -I../../../components/cyberdeck/include test_cat_worker_runtime.cpp $(CAT_WORKER_SRC) $(CAT_RUNTIME_OBJS) event_log_shim/event_log_shim.cpp -Wl,--wrap=read -o $@

test_poweroff/test.o: test_poweroff.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_poweroff/poweroff.o: ../../../components/cyberdeck/src/platform/power/cyberdeck_poweroff.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -I../../../components/cyberdeck/include -MMD -MP -MF $@.d -c $< -o $@

test_poweroff_bin: $(POWEROFF_MIG_OBJS) Makefile
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage -fsanitize=address,undefined -pthread -I../../../components/cyberdeck/include $(POWEROFF_MIG_OBJS) -Wl,--wrap=write -Wl,--wrap=fsync -Wl,--wrap=close -o $@

.PHONY: test_poweroff
test_poweroff: test_poweroff_bin
	@./test_poweroff_bin


test_wake_key_contract: test_wake_key_contract.py ../../../components/cyberdeck/src/platform/display/cyberdeck_ui.cpp
	$(PYTHON) test_wake_key_contract.py

.PHONY: test-build
test-build: $(BINS)

.PHONY: test_music_model
test_music_model: $(MUSIC_MODEL_BIN)
	@./$(MUSIC_MODEL_BIN)

$(MUSIC_MODEL_BIN): $(MUSIC_MODEL_TEST) $(MUSIC_MODEL_SRC) $(MUSIC_MODEL_HDR) music_traceability.md Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(MUSIC_MODEL_TEST) $(MUSIC_MODEL_SRC) -o $@

.PHONY: test_music_audio_output
test_music_audio_output: test_music_audio_output_bin
	@./test_music_audio_output_bin

test_music_audio_output_bin: $(MUSIC_AUDIO_TEST) $(MUSIC_AUDIO_SRC) $(MUSIC_AUDIO_HDR) $(MUSIC_AUDIO_SHIMS) music_traceability.md Makefile
	$(CXX) $(CXXFLAGS) -Imusic_runtime_shim -I../../../components/cyberdeck/include $(MUSIC_AUDIO_TEST) $(MUSIC_AUDIO_SRC) -o $@

.PHONY: test_music_app
test_music_app: test_music_app_bin
	@./test_music_app_bin

$(MUSIC_APP_OBJ): $(MUSIC_APP_MAIN_SRC) $(MUSIC_APP_HDRS) Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -ffunction-sections -fdata-sections -pthread -Imusic_runtime_shim \
		-I../../../components/cyberdeck/include -Dcyberdeck_local_shell_opendir=music_test_secure_opendir \
		-Dcyberdeck_local_shell_open_readonly=music_test_secure_open -MMD -MP -MF $@.d -c $< -o $@

test_music_app_bin: $(MUSIC_APP_TEST) $(MUSIC_APP_OBJ) $(MUSIC_APP_SOURCES) $(MUSIC_COMMAND_INPUT_SRC) $(MUSIC_APP_HDRS) $(MUSIC_AUDIO_SHIMS) music_traceability.md Makefile
	$(CXX) $(CXXFLAGS) -ffunction-sections -fdata-sections -pthread -Imusic_runtime_shim \
		-I../../../components/cyberdeck/include $(MUSIC_APP_TEST) $(MUSIC_APP_OBJ) $(MUSIC_APP_SOURCES) $(MUSIC_COMMAND_INPUT_SRC) \
		-Wl,--wrap=lstat -Wl,--wrap=readdir -Wl,--gc-sections -o $@

.PHONY: test_music_manager
test_music_manager: test_music_manager_bin
	@./test_music_manager_bin

test_music_manager_bin: test_music_manager.cpp ../../../components/cyberdeck/src/apps/music/cyberdeck_music_manager.cpp ../../../components/cyberdeck/include/apps/music/cyberdeck_music_manager.h Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $< ../../../components/cyberdeck/src/apps/music/cyberdeck_music_manager.cpp -o $@

.PHONY: test_music_player
test_music_player: test_music_player_bin
	@./test_music_player_bin

test_music_player_bin: $(MUSIC_PLAYER_TEST) $(MUSIC_PLAYER_SOURCES) $(MUSIC_PLAYER_SHIMS) $(MUSIC_APP_HDRS) $(MUSIC_METADATA_HDR) $(MUSIC_AUDIO_HDR) music_traceability.md Makefile
	$(CXX) $(CXXFLAGS) -Wno-error=stringop-truncation -ffunction-sections -fdata-sections -pthread -Imusic_runtime_shim \
		-I../../../components/cyberdeck/include -I../../../components/cyberdeck $(MUSIC_PLAYER_TEST) $(MUSIC_PLAYER_SOURCES) \
		-Wl,--wrap=close -Wl,--wrap=read -Wl,--gc-sections -o $@

.PHONY: test_esp_hosted_cpu_staging
test_esp_hosted_cpu_staging:
	@$(PYTHON) test_esp_hosted_cpu_staging.py

.PHONY: test_music_decoder_stack_contract
test_music_decoder_stack_contract:
	@$(PYTHON) test_music_decoder_stack_contract.py

.PHONY: test_music_decoder_scratch
test_music_decoder_scratch: test_music_decoder_scratch_bin
	@./test_music_decoder_scratch_bin

test_music_decoder_scratch_bin: test_music_decoder_scratch.cpp ../../../components/cyberdeck/third_party/minimp3/minimp3.h Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck $< -o $@

.PHONY: test_music_command_controller
test_music_command_controller: test_music_command_controller_bin
	@./test_music_command_controller_bin

test_music_command_controller_bin: test_music_command_controller.cpp $(MUSIC_COMMAND_INPUT_SRC) $(MUSIC_COMMAND_INPUT_HDR) music_traceability.md Makefile
	$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_music_command_controller.cpp $(MUSIC_COMMAND_INPUT_SRC) -o $@

test: test-build
	@$(MAKE) --no-print-directory test_lvgl_dispatch_state
	@$(MAKE) --no-print-directory test_esp_hosted_cpu_staging
	@$(MAKE) --no-print-directory test_music_decoder_stack_contract
	@$(MAKE) --no-print-directory test_music_decoder_scratch
	@$(MAKE) --no-print-directory test_music_model
	@$(MAKE) --no-print-directory test_music_audio_output
	@$(MAKE) --no-print-directory test_music_app
	@$(MAKE) --no-print-directory test_music_manager
	@$(MAKE) --no-print-directory test_music_player
	@$(MAKE) --no-print-directory test_music_command_controller
	@$(MAKE) --no-print-directory test_save_sidecar_recovery
	@$(MAKE) --no-print-directory test_wifi_survey_export_controller
	@$(MAKE) --no-print-directory test_netdiag_controller
	@$(MAKE) --no-print-directory test_netdiag_tcp_adapter
	@./test_wifi_survey_bin
	@$(MAKE) --no-print-directory test_wifi_survey_integration_contract
	@$(MAKE) --no-print-directory test_wifi_survey_export_result_contract
	@$(MAKE) --no-print-directory test_netdiag_dns_adapter
	@$(MAKE) --no-print-directory test_netdiag_https_adapter
	@$(MAKE) --no-print-directory test_netdiag_app
	@./test_poweroff_bin
	@./test_cat_worker_runtime
	@./test_known_hosts_file_bin
	@$(PYTHON) test_poweroff_contract.py
	@$(PYTHON) test_poweroff_link_guard.py
	@echo "==> Executando teste comportamental de sincronizacao UTC (TEST-TIME-01..08)"
	@./test_time_sync_bin
	@echo "==> Executando contrato de runtime da sincronizacao UTC (TEST-TIME-09 + TEST-TIME-STACK-01)"
	@$(MAKE) --no-print-directory test_time_sync_contract
	@echo "==> Executando contrato estrutural do wake-key (REQ-WAKE-KEY-01/02)"
	@$(MAKE) --no-print-directory test_wake_key_contract
	@echo "==> Executando testes do keymap do shell de entrada"
	@./test_keymap_bin
	@echo "==> Executando testes dos utilitarios de shell e SSH"
	@./test_shell_utils_bin
	@echo "==> Executando testes do historico local do terminal"
	@./test_history_bin
	@echo "==> Executando testes do parser de eventos Character do teclado"
	@./test_keyboard_event_bin
	@echo "==> Executando testes da selecao de indices do anel recente do log"
	@./test_event_log_recent_bin
	@./test_event_log_shim
	@$(MAKE) --no-print-directory test_event_log_runtime
	@echo "==> Executando contrato estrutural do event log"
	@$(MAKE) --no-print-directory test_event_log_contract
	@echo "==> Executando contrato estrutural da projecao textual de eventos"
	@$(MAKE) --no-print-directory test_event_projection_contract
	@echo "==> Executando contrato de configuracao do event log"
	@$(MAKE) --no-print-directory test_event_log_config_contract
	@echo "==> Executando testes do indicador de estado do Wi-Fi"
	@./test_wifi_indicator_state_bin
	@echo "==> Executando testes do layout do icono Wi-Fi"
	@./test_wifi_icon_layout_bin
	@echo "==> Executando testes do modelo e renderizacao do menu Wi-Fi"
	@./test_wifi_menu_bin
	@echo "==> Executando testes da maquina de estados pura de busca/salvas Wi-Fi"
	@./test_wifi_state_machine_bin
	@echo "==> Executando contrato TDD do produtor IP_EVENT"
	@./test_wifi_event_dispatch_bin
	@echo "==> Executando contrato TDD da fila de persistencia Wi-Fi"
	@./test_wifi_persistence_queue_bin
	@echo "==> Executando contrato TDD da coordenacao da persistencia Wi-Fi"
	@./test_wifi_persistence_coordination_bin
	@echo "==> Executando contrato TDD da auditoria Wi-Fi passiva"
	@./$(WIFI_AUDIT_BIN)
	@echo "==> Executando contrato TDD do fluxo simplificado de salvamento da auditoria Wi-Fi"
	@./test_wifi_audit_save_bin
	@echo "==> Executando contrato estrutural do salvamento da auditoria Wi-Fi"
	@$(MAKE) --no-print-directory test_wifi_audit_save_contract
	@echo "==> Executando contrato TDD da persistencia injetavel da auditoria Wi-Fi"
	@$(MAKE) --no-print-directory test_wifi_audit_persistence_contract
	@echo "==> Executando binario comportamental da persistencia da auditoria Wi-Fi"
	@./test_wifi_audit_persistence_bin
	@echo "==> Executando testes do formatador do relogio do header (GMT-3)"
	@./test_clock_formatter_bin
	@echo "==> Executando contrato puro de politica/estado da protecao de tela"
	@./test_screen_protection_bin
	@echo "==> Executando contrato estrutural da protecao de tela/NVS/ui.type"
	@$(MAKE) --no-print-directory test_screen_protection_contract
	@echo "==> Executando contrato estrutural do lote de migracao incremental"
	@$(MAKE) --no-print-directory test_incremental_batch_contract
	@$(MAKE) --no-print-directory test_incremental_final_contract
	@echo "==> Executando testes do filtro de saida SSH"
	@./test_terminal_filter_bin
	@echo "==> Executando testes do modelo de scrollback do terminal"
	@./test_terminal_scrollback_bin
	@$(MAKE) --no-print-directory test_lat03_contract
	@$(MAKE) --no-print-directory test_lat04_contract
	@echo "==> Executando testes do modelo da linha de edicao/sessao SSH"
	@./test_edit_line_bin
	@echo "==> Executando testes do modelo do editor"
	@./test_editor_model_bin
	@echo "==> Executando contrato de runtime do editor"
	@$(MAKE) --no-print-directory test_editor_runtime_contract
	@echo "==> Executando contrato determinístico de storage/save do editor"
	@$(MAKE) --no-print-directory test_editor_save_contract
	@echo "==> Executando contrato da superfície e2e do editor"
	@$(MAKE) --no-print-directory test_editor_surface_contract
	@echo "==> Executando testes comportamentais da sessao de shell"
	@./test_shell_session_bin
	@echo "==> Executando testes do contrato <command> help (REQ-HELP-DETAILS)"
	@./test_shell_help_bin
	@echo "==> Executando contrato estrutural dos manifestos de help (REQ-HELP-05)"
	@$(MAKE) --no-print-directory test_shell_help_contract
	@echo "==> Executando testes do runtime de aplicacoes compiladas"
	@./test_app_runtime_bin
	@./test_runtime_sdk_bin
	@echo "==> Executando teste host real da transacao storage confinada"
	@./test_storage_facade_bin
	@echo "==> Executando policy pura e contrato estrutural da Fase 10"
	@$(MAKE) --no-print-directory test_recovery_policy
	@$(MAKE) --no-print-directory test_recovery_runtime
	@$(MAKE) --no-print-directory test_phase10_contract
	@echo "==> Executando testes de capabilities da Fase 9"
	@./test_phase9_capabilities_bin
	@$(MAKE) --no-print-directory test_phase9_capabilities_contract
	@echo "==> Executando testes do app demo compilado"
	@./test_demo_app_bin
	@echo "==> Executando aplicacao shell de primeiro plano e seu contrato"
	@./test_shell_app_bin
	@$(MAKE) --no-print-directory test_shell_app_contract
	@echo "==> Executando contrato do handoff LVGL do shell"
	@$(MAKE) --no-print-directory test_lvgl_shell_lifecycle_contract
	@echo "==> Executando o window manager da Fase 8 e seu contrato"
	@./test_window_manager_bin
	@echo "==> Executando coordenador de I/O SD (Stage 0)"
	@$(MAKE) --no-print-directory test_sd_io_coordinator
	@echo "==> Executando testes do pacote SD puro (REQ-COV-01)"
	@$(MAKE) --no-print-directory test_sd_package
	@echo "==> Executando testes do catalogo de recursos compilados (REQ-RES-01)"
	@$(MAKE) --no-print-directory test_resource_catalog
	@echo "==> Executando contrato de integracao de I/O SD"
	@$(MAKE) --no-print-directory test_sd_io_integration_contract
	@$(MAKE) --no-print-directory test_window_manager_contract
	@echo "==> Executando contrato de lock do display"
	@$(MAKE) --no-print-directory test_display_lock_contract
	@echo "==> Executando contrato de UI do player de musica"
	@$(MAKE) --no-print-directory test_music_player_ui_contract
	@echo "==> Executando contrato dos system apps"
	@$(MAKE) --no-print-directory test_system_apps_contract
	@$(MAKE) --no-print-directory test_screenshot_lifecycle_contract
	@$(MAKE) --no-print-directory test_serial_lifecycle_contract
	@$(MAKE) --no-print-directory test_net_session_ipc_contract
	@echo "==> Executando contrato da alocacao externa da task SSH (PSRAM)"
	@$(MAKE) --no-print-directory test_ssh_task_alloc_contract
	@echo "==> Executando testes do coordenador de teardown Wi-Fi/SSH e timers"
	@./test_net_coordinator_bin
	@echo "==> Executando testes da guarda incremental de eco remoto do SSH"
	@./test_ssh_echo_guard_bin
	@echo "==> Executando testes da composicao local/remota da linha SSH"
	@./test_ssh_line_composer_bin
	@echo "==> Executando testes da API de geracao de BMP 24-bit"
	@./test_screenshot_bmp_bin
	@echo "==> Executando contrato do shell local seguro"
	@./test_local_shell_bin
	@echo "==> Executando contrato de unificacao do help"
	@./test_help_unification_bin
	@$(MAKE) --no-print-directory help_unification_contract
	@echo "==> Executando regressao host da API cat multiline"
	@./test_cat_multiline_bin
	@echo "==> Executando testes de orientacao pura e debounce"
	@./test_orientation_bin
	@echo "==> Executando contrato estrutural da sequencia de boot SD"
	@$(MAKE) --no-print-directory test_boot_sequence
	@echo "==> Executando contrato do backlog de transformacao do OS"
	@$(MAKE) --no-print-directory test_os_transformation_backlog_contract
	@echo "==> Executando contrato de recursos, pacote SD e quotas"
	@$(MAKE) --no-print-directory test_resource_sd_quota_contract
	@echo "==> Executando contrato TDD do despacho assincrono do teclado fisico"
	@$(MAKE) --no-print-directory test_keyboard_input_contract
	@echo "==> Executando contrato de cleanup parcial da inicializacao da UI"
	@$(MAKE) --no-print-directory test_ui_resource_contract
	@echo "==> Executando contrato estrutural da saida do terminal SSH"
	@$(MAKE) --no-print-directory test_terminal_output_contract
	@echo "==> Executando o seam de gap da saida SSH (REQ-01/02/03/04)"
	@$(MAKE) --no-print-directory test_ssh_output_gap
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
	@echo "==> Executando contrato TDD do term.dump bounded/truncado na ponte serial (REQ-TERM-001/AC-TERM-001)"
	@$(MAKE) --no-print-directory test_serial_term_dump
	@echo "==> Executando contrato estrutural de exec_term_dump/cyberdeck_ui_term_dump (AC-TERM-002..005)"
	@$(MAKE) --no-print-directory test_term_dump_contract
	@echo "==> Executando contrato da CLI term.dump sem argumentos (TEST-TERM-007/AC-TERM-005)"
	@$(MAKE) --no-print-directory test_term_dump_cli
	@echo "==> Executando contrato TDD do fs.write seguro (REQ-001..REQ-011)"
	@$(MAKE) --no-print-directory test_fs_write_dispatch
	@$(MAKE) --no-print-directory test_fs_write_cli
	@$(MAKE) --no-print-directory test_fs_write_contract
	@$(MAKE) --no-print-directory test_coverage_scope_guard
	@./test_cat_worker_bin
	@./test_ble_background_bin
	@./test_keyboard_dispatch_bin
	@./test_display_views_bin
	@echo "==> Executando contratos de bateria (tensao/estados/UI)"
	@$(MAKE) --no-print-directory test_battery_contract
	@$(MAKE) --no-print-directory test_battery_reader_contract
	@$(MAKE) --no-print-directory test_battery_ui_contract
	@echo "==> Executando contrato TDD de estados/protecao de bateria"
	@$(MAKE) --no-print-directory test_battery_protection
	@$(MAKE) --no-print-directory test_battery_protection_contract
	@echo "==> Executando contrato da view pura do indicador de energia"
	@$(MAKE) --no-print-directory test_battery_view
	@$(MAKE) --no-print-directory test_battery_view_contract
	@echo "==> Executando contratos BLE (tipos/list, state machine, eventos/tokens, store)"
	@$(MAKE) --no-print-directory test_ble_types
	@$(MAKE) --no-print-directory test_ble_state_machine
	@$(MAKE) --no-print-directory test_ble_event_dispatch
	@$(MAKE) --no-print-directory test_ble_store
	@echo "==> Executando contrato do parser/roteamento shell bluetooth"
	@$(MAKE) --no-print-directory test_ble_command_parse
	@echo "==> Executando contrato estrutural da integracao BLE"
	@$(MAKE) --no-print-directory test_ble_integration_contract
	@echo "==> Executando contrato de timeout/renderizacao da UI BLE"
	@$(MAKE) --no-print-directory test_ble_ui_timeout_contract
	@echo "==> Executando contrato dos tipos de anuncio BLE"
	@$(MAKE) --no-print-directory test_ble_adv_types_contract
	@echo "==> Executando contrato do scan BLE (parametros/parser/agregacao/addr_type)"
	@$(MAKE) --no-print-directory test_ble_scan_contract
	@echo "==> Executando contrato de comandos BLE antes do sync do host"
	@$(MAKE) --no-print-directory test_ble_pre_sync_contract
	@echo "==> Executando regressões BLE encontradas na revisão"
	@$(MAKE) --no-print-directory test_ble_review_regressions
	@echo "==> Executando regressões de runtime do scan BLE"
	@$(MAKE) --no-print-directory test_ble_runtime_regressions
	@echo "==> Executando contrato de renderizacao da UI BLE"
	@$(MAKE) --no-print-directory test_ble_ui_render_contract
	@echo "==> Executando contrato do indicador Bluetooth no header"
	@$(MAKE) --no-print-directory test_ble_header_contract
	@echo "==> Executando contrato de autenticacao BLE NimBLE 5.5.5"
	@$(MAKE) --no-print-directory test_ble_auth_contract
	@echo "==> Executando contrato de descoberta HID BLE"
	@$(MAKE) --no-print-directory test_ble_hid_contract
	@echo "==> Executando contrato do ciclo de vida da conexao BLE e disponibilidade da UI"
	@$(MAKE) --no-print-directory test_ble_connection_lifecycle_contract
	@echo "==> Executando contrato de restauracao de bonds e reconexao background BLE"
	@$(MAKE) --no-print-directory test_ble_background_contract
	@echo "==> Executando contrato de nomes de bonds BLE"
	@$(MAKE) --no-print-directory test_ble_bond_name_contract
	# Keep the known IMU contract last so unrelated task tests run first;
	# it remains a separate, non-masked gate.
	@echo "==> Executando contrato IMU preexistente isolado (fora do escopo)"
	@$(MAKE) --no-print-directory imu_sensor_contract
.PHONY: test_display_memory_contract
test_display_memory_contract: test_display_memory_contract.py ../../../components/m5stack_tab5/src/bsp_display.c ../../../components/esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c ../../../components/cyberdeck/src/apps/music/cyberdeck_music_player.cpp
	$(PYTHON) test_display_memory_contract.py

clean:
	rm -rf test_music_app
	rm -rf test_editor_model test_time_sync test_shell_utils test_sd_package test_resource_catalog test_netdiag_dns_adapter test_netdiag_https_adapter test_netdiag_app test_shell_session test_shell_help test_app_runtime test_runtime_sdk test_storage_facade test_phase9_capabilities test_demo_app test_shell_app test_window_manager test_local_shell test_cat_multiline test_help_unification test_battery_protection test_cat_worker test_ble_background test_keyboard_dispatch test_display_views test_known_hosts_file test_poweroff
	rm -f $(BINS) test_app_runtime_tsan $(CAT_RUNTIME_OBJS) ./*.gcda ./*.gcno ./*.gcov
	rm -rf test_keymap test_screenshot_bmp test_orientation
	rm -rf test_screen_protection test_wifi_audit test_wifi_audit_persistence test_wifi_audit_save test_serial_ndjson_dispatch test_serial_screen_dump test_serial_cli_tolerance test_serial_sysinfo_wifi test_serial_term_dump test_fs_write_dispatch test_save_sidecar_recovery test_wifi_survey_export_controller test_netdiag_controller test_netdiag_tcp_adapter test_netdiag_dns_adapter test_netdiag_app test_event_log_runtime test_sd_io_coordinator test_battery_contract test_battery_view test_ble_types test_ble_state_machine test_ble_event_dispatch test_ble_store test_ble_command_parse test_terminal_scrollback test_wifi_survey test_recovery_policy test_wifi_event_dispatch test_wifi_persistence_queue test_wifi_persistence_coordination test_clock_formatter test_terminal_filter test_edit_line test_wifi_indicator_state test_wifi_icon_layout test_wifi_menu test_history test_keyboard_event test_event_log_recent test_wifi_state_machine test_net_coordinator test_ssh_echo_guard test_ssh_line_composer .objects
	rm -f $(LEGACY_TEST_ARTIFACTS)
