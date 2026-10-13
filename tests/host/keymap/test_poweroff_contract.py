#!/usr/bin/env python3
"""Poweroff wiring/safety contracts; host tests do not prove physical rail cut."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "components/cyberdeck/src"


def read(relative: str) -> str:
    return (ROOT / relative).read_text()


def main() -> None:
    power = (SRC / "platform/power/cyberdeck_poweroff.cpp").read_text()
    main_source = read("main/app_main.cpp")
    assert main_source.index("bind_global(sd_io_coordinator)") < main_source.index("bsp_sdcard_mount()")
    assert "cyberdeck_poweroff_init(sd_io_coordinator)" in main_source
    assert "poweroff_watch" in power and "shutdown.expire()" in power
    assert "45000" in power and "state_.load(std::memory_order_acquire) != wanted" in power
    assert "compare_exchange_strong(running, state::signaling" in power
    assert "sd_io->state() != cyberdeck_sd_io::gate_state::closed" in power
    assert "sd_io->active_leases() != 0" in power and "sd_io->persistence_failed()" in power
    assert "__wrap_ff_disk_write" in power and "__real_ff_disk_write(drive, buffer, sector, count)" in power
    assert "command == CTRL_SYNC && result != RES_OK" in power
    cmake = read("components/cyberdeck/CMakeLists.txt")
    assert "--wrap=ff_disk_write" in cmake and "--wrap=ff_disk_ioctl" in cmake
    assert power.index('flush_metadata("/sdcard/.poweroff-sync")') < power.index("bsp_sdcard_unmount()")
    assert "bsp_io_expander1_init()" in power and "IO_EXPANDER_PIN_NUM_4" in power
    assert "IO_EXPANDER_OUTPUT_MODE_PUSH_PULL" in power
    assert "bsp_io_expander_init()" not in power and "bsp_display_brightness_set" not in power
    assert "esp_deep_sleep" not in power
    for operation in ("cyberdeck_cat_worker_quiesce", "cyberdeck_ui_audit_quiesce", "cyberdeck_time_sync_stop",
                      "ssh_client_disconnect_and_wait", "screenshot_server_stop", "wifi_mgr_stop",
                      "bridge_stop", "screen_off_quiesce", "ble_mgr_stop", "event_log_quiesce"):
        assert operation in power, operation
    storage = read("components/m5stack_tab5/src/bsp_storage.c")
    unmount = storage[storage.index("esp_err_t bsp_sdcard_unmount(void)"):]
    assert unmount.index("esp_vfs_fat_sdcard_unmount") < unmount.index("sd_pwr_ctrl_del_on_chip_ldo")
    assert "if (ret != ESP_OK) return ret;" in unmount
    expander = read("components/m5stack_tab5/src/bsp_io_expander.c")
    assert "BSP_ERROR_CHECK_RETURN_NULL" not in expander[expander.index("bsp_io_expander1_init(void)"):]
    board = read("components/m5stack_tab5/include/bsp/m5stack_tab5.h")
    assert "BSP_IO_EXPANDER_ADDRESS_1  (ESP_IO_EXPANDER_I2C_PI4IOE5V6408_ADDRESS_HIGH)" in board
    assert "static_assert(BSP_IO_EXPANDER_ADDRESS_1 == 0x44" in power
    for relative in ("apps/wifi/wifi_storage.cpp", "apps/wifi/cyberdeck_wifi_audit.cpp",
                     "apps/serial/cyberdeck_serial_bridge.cpp", "apps/ssh/cyberdeck_known_hosts_file.cpp"):
        source = (SRC / relative).read_text()
        assert "acquire_global" in source, relative
        assert "record_global_persistence_failure" in source, relative
    ssh = (SRC / "apps/ssh/ssh_client.cpp").read_text()
    assert "cyberdeck_known_hosts::read" in ssh and "cyberdeck_known_hosts::append" in ssh
    assert "ssh_key_cmp" in ssh and "ssh_known_hosts_parse_line" in ssh
    assert "ssh_session_is_known_server(" not in ssh and "ssh_session_update_known_hosts(" not in ssh
    cat = (SRC / "apps/shell/cyberdeck_cat_worker.cpp").read_text()
    bounded = cat[cat.index("static bool stop_cat_worker"):cat.index("#else", cat.index("static bool stop_cat_worker"))]
    assert "portMAX_DELAY" not in bounded
    assert "xSemaphoreTake(s_stopped, remaining()) != pdTRUE) return false" in bounded
    assert bounded.index("xSemaphoreTake(s_stopped, remaining())") < bounded.rindex("vQueueDelete(s_queue)")
    ui = (SRC / "platform/display/cyberdeck_ui.cpp").read_text()
    assert "lv_timer_pause(timer)" in ui and "request_poweroff() override" in ui
    assert ui.count("if (cyberdeck_poweroff_pending()) return;") >= 4
    screen = (SRC / "platform/display/screen_off.cpp").read_text()
    assert "s_persistence_closing.compare_exchange_strong" in screen
    assert "xSemaphoreTake(s_persistence_stopped, remaining)" in screen
    assert "s_persistence_failed.store(true" in screen
    print("PASS: poweroff global SD/NVS fences, bounded quarantine, unmount-before-LDO and expander-B wiring")


if __name__ == "__main__":
    main()
