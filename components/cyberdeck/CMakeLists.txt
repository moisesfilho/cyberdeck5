idf_component_register(
    SRCS
        "src/platform/display/cyberdeck_ui.cpp"
        "src/platform/display/cyberdeck_header_view.cpp"
        "src/platform/display/cyberdeck_display_port.cpp"
        "src/platform/display/cyberdeck_terminal_view.cpp"
        "src/platform/display/cyberdeck_terminal_scrollback.cpp"
        "src/apps/shell/cyberdeck_edit_line.cpp"
        "src/platform/networking/cyberdeck_net_coordinator.cpp"
        "src/platform/storage/cyberdeck_sd_io_coordinator.cpp"
        "src/platform/power/cyberdeck_poweroff.cpp"
        "src/apps/shell/cyberdeck_terminal_filter.cpp"
        "src/apps/shell/cyberdeck_ssh_echo_guard.cpp"
        # Stateful local/remote compositor; keep it in the firmware target so
        # cyberdeck_ui and the host-side contract use the same implementation.
        "src/apps/shell/cyberdeck_ssh_line_composer.cpp"
        # Stateful shell session (line dispatch + key routing); firmware-only,
        # like cyberdeck_ui: the host suite pins it through structural contracts.
        "src/apps/shell/cyberdeck_shell_session.cpp"
        # Prompt/line composition and command dispatch owned by the foreground
        # shell application (Fase 7), plus the application that owns the console.
        "src/apps/shell/cyberdeck_shell_console.cpp"
        "src/apps/shell/cyberdeck_shell_app.cpp"
        "src/platform/display/cyberdeck_wifi_indicator.cpp"
        "src/platform/display/cyberdeck_wifi_icon.cpp"
        "src/apps/wifi/cyberdeck_wifi_menu.cpp"
        "src/apps/wifi/cyberdeck_wifi_survey.cpp"
        "src/apps/wifi/cyberdeck_wifi_survey_export_controller.cpp"
        "src/apps/wifi/cyberdeck_wifi_state_machine.cpp"
        "src/platform/display/cyberdeck_clock.cpp"
        "src/platform/display/cyberdeck_screen_protection.cpp"
        # Pure header-view mapping: keeps the battery rendering rule out of LVGL.
        "src/platform/display/cyberdeck_battery_view.cpp"
        "src/apps/shell/cyberdeck_history.cpp"
        "src/apps/shell/cyberdeck_shell_utils.cpp"
        "src/apps/shell/cyberdeck_local_shell.cpp"
        "src/apps/shell/cyberdeck_vfs_namespace.cpp"
        "src/apps/shell/cyberdeck_cat_worker.cpp"
         "src/apps/runtime/cyberdeck_app_runtime.cpp"
         "src/apps/runtime/cyberdeck_resource_catalog.cpp"
         "src/apps/runtime/cyberdeck_sd_package.cpp"
         "src/apps/runtime/cyberdeck_app_quota.cpp"
          "src/apps/runtime/cyberdeck_app_logger.cpp"
         "src/apps/runtime/cyberdeck_app_storage.cpp"
         "src/apps/runtime/cyberdeck_command_catalog.cpp"
        "src/apps/runtime/cyberdeck_app_facades.cpp"
         "src/apps/runtime/cyberdeck_window_manager.cpp"
         "src/apps/editor/cyberdeck_editor_model.cpp"
         "src/apps/music/cyberdeck_music_model.cpp"
         "src/apps/music/cyberdeck_music_player.cpp"
         "src/apps/music/cyberdeck_music_app.cpp"
         "src/apps/music/cyberdeck_music_manager.cpp"
         "src/platform/audio/cyberdeck_audio_output.cpp"
        "src/apps/editor/cyberdeck_editor_app.cpp"
           "src/apps/network/cyberdeck_netdiag_app.cpp"
            "src/apps/network/cyberdeck_netdiag_controller.cpp"
            "src/apps/network/cyberdeck_netdiag_dns_adapter.cpp"
             "src/apps/network/cyberdeck_netdiag_tcp_adapter.cpp"
             "src/apps/network/cyberdeck_netdiag_https_adapter.cpp"
         "src/platform/display/cyberdeck_editor_view.cpp"
         "src/platform/display/cyberdeck_music_view.cpp"
        "src/apps/demo/cyberdeck_demo_app.cpp"
          "src/apps/system/cyberdeck_system_apps.cpp"
          "src/apps/system/cyberdeck_time_sync.cpp"
         "src/apps/system/cyberdeck_recovery_policy.cpp"
         "src/apps/system/cyberdeck_recovery.cpp"
        "src/apps/system/cyberdeck_service_ports.cpp"
        "src/platform/logging/event_log.cpp"
        "src/platform/logging/event_log_recent.cpp"
        "src/platform/sensors/imu_reader.cpp"
        "src/platform/sensors/orientation.cpp"
        "src/platform/sensors/battery_status.cpp"
        "src/platform/sensors/ina226_reader.cpp"
        "src/platform/sensors/cyberdeck_battery_protection.cpp"
        "src/platform/sensors/battery_protection.cpp"
        "src/platform/display/screen_off.cpp"
        "src/apps/ssh/ssh_client.cpp"
        "src/apps/ssh/cyberdeck_known_hosts_file.cpp"
        "src/platform/input/tab5_keyboard.cpp"
        "src/platform/input/cyberdeck_keyboard_dispatch.cpp"
        "src/platform/input/tab5_keyboard_event.cpp"
        "src/platform/input/tab5_keyboard_keys.cpp"
        "src/apps/wifi/wifi_mgr.cpp"
        "src/apps/wifi/cyberdeck_wifi_event_dispatch.cpp"
        "src/apps/wifi/cyberdeck_wifi_persistence_queue.cpp"
        "src/apps/wifi/cyberdeck_wifi_persistence_coordinator.cpp"
        "src/apps/wifi/cyberdeck_wifi_audit.cpp"
        "src/apps/wifi/cyberdeck_wifi_audit_persistence.cpp"
        "src/apps/wifi/wifi_storage.cpp"
        "src/apps/screenshot/screenshot_bmp.cpp"
        "src/apps/screenshot/screenshot_server.cpp"
        "src/apps/serial/cyberdeck_serial_bridge.cpp"
        "src/apps/bluetooth/cyberdeck_ble_types.cpp"
        "src/apps/bluetooth/cyberdeck_ble_state_machine.cpp"
        "src/apps/bluetooth/cyberdeck_ble_event_dispatch.cpp"
        "src/apps/bluetooth/cyberdeck_ble_background.cpp"
        "src/apps/bluetooth/cyberdeck_ble_store.cpp"
        "src/apps/bluetooth/ble_mgr.cpp"
        "src/platform/display/cyberdeck_font.c"
        "src/platform/display/cyberdeck_window_manager_adapter.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}"
                       "${IDF_PATH}/components/bt/host/nimble/nimble/porting/nimble/include"
                       "${IDF_PATH}/components/bt/host/nimble/port/include"
                       "${IDF_PATH}/components/bt/porting/include"
                       "${IDF_PATH}/components/bt/host/nimble/nimble/nimble/transport/include"
                       "${IDF_PATH}/components/bt/host/nimble/nimble/nimble/host/util/include"
                       "${IDF_PATH}/components/bt/host/nimble/nimble/nimble/host/services/gap/include"
                       "${IDF_PATH}/components/bt/host/nimble/nimble/nimble/include"
                       "${IDF_PATH}/components/bt/porting/npl/freertos/include"
                       "${IDF_PATH}/components/bt/host/nimble/nimble/nimble/host/include"
    REQUIRES
        lvgl
        m5stack_tab5
        esp_wifi
        esp_netif
        esp_event
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_usb_serial_jtag
        esp_app_format
        esp_timer
        esp_system
        freertos
        sensor_hub
        nvs_flash
        fatfs
        sdmmc
        libssh
         esp_http_server
         esp_http_client
         esp-tls
         lwip
    cjson
        bt
        esp_hosted
)

if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    idf_component_get_property(libssh_target david-cermak__libssh COMPONENT_LIB)
    if(NOT libssh_target)
        message(FATAL_ERROR "libssh component target is unavailable")
    endif()
    target_include_directories(
        ${libssh_target}
        BEFORE PRIVATE "${CMAKE_CURRENT_LIST_DIR}/libssh_idf6_compat"
    )
    target_compile_definitions(${libssh_target} PRIVATE HAVE_TERMIOS_H=1)
endif()

# Observe public FatFS disk-I/O errors even where upstream masks FSInfo status.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=ff_disk_write" "-Wl,--wrap=ff_disk_ioctl")
