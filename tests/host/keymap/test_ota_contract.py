#!/usr/bin/env python3
"""Integration boundaries which need ESP-IDF/LVGL and cannot link on host."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
firmware = (ROOT / "components/cyberdeck/src/apps/ota/cyberdeck_ota.cpp").read_text()
serial = (ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp").read_text()
boot = (ROOT / "main/app_main.cpp").read_text()
ui = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp").read_text()
apps = (ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp").read_text()
partition_lines = [line.split(",")[:5] for line in (ROOT / "partitions.csv").read_text().splitlines()
                   if line.strip() and not line.lstrip().startswith("#")]
parts = {columns[0].strip(): (columns[1].strip(), columns[2].strip(), columns[4].strip())
         for columns in partition_lines}
assert parts["otadata"] == ("data", "ota", "0x2000")
for name, subtype in (("factory", "factory"), ("ota_0", "ota_0"), ("ota_1", "ota_1")):
    assert parts[name] == ("app", subtype, "4M")
assert "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" in (ROOT / "sdkconfig.defaults").read_text()
assert "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y" in (ROOT / "sdkconfig.defaults").read_text()

for boundary in ("mbedtls_md_info_from_type(MBEDTLS_MD_SHA256)", "s_nonce_deadline = 0",
                 "X-OTA-Version", "X-OTA-Size", "valid_package_mac(package)",
                 "s_phase = phase::approved_upload", "s_phase = phase::awaiting_confirmation",
                 "mbedtls_md_update(&hash", "same_bytes(received_sha, package.sha.data()",
                 "esp_ota_end(handle)", "esp_ota_set_boot_partition(partition)",
                 "esp_ota_mark_app_valid_cancel_rollback()", "esp_ota_mark_app_invalid_rollback_and_reboot()"):
    assert boundary in firmware, boundary
prepare = firmware[firmware.index("esp_err_t prepare("):firmware.index("esp_err_t upload(")]
upload = firmware[firmware.index("esp_err_t upload("):firmware.index("esp_err_t load_key(")]
assert "esp_ota_begin(" not in prepare, "physical confirmation must precede transfer/flash writes"
assert prepare.index("s_phase = phase::awaiting_confirmation") < prepare.index("reply(req, \"202 Accepted\"")
assert upload.index("s_phase != phase::approved_upload") < upload.index("esp_ota_begin(partition")
assert "esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle)" in upload
assert "esp_ota_begin(partition, package.size" not in upload
assert "std::strcmp(description.version, package.version) == 0" in upload
assert '"/ota/prepare"' in firmware
assert "Autorizar firmware %s?" in ui and "Enviando firmware %s" in ui
assert "bytes_sent\\\":0" in prepare
assert "esp_ota_set_boot_partition" not in ui
assert "cyberdeck_ota::decide(key == LV_KEY_ENTER)" in ui
assert '"ota.key"' in serial and "cyberdeck_ota::serial_key" in serial
assert "ota.key" not in firmware
assert "&s_ota" in apps and '"cyberdeck.wifi"' in apps
checkpoint = boot.index("cyberdeck_system_apps_commit_ready()")
assert boot.index("cyberdeck_ota::confirm_boot()") > checkpoint
assert boot.index("cyberdeck_ota::reject_pending_boot()") > checkpoint
assert 'app_runtime.state("cyberdeck.ota") == cyberdeck_apps::app_state::running' in boot
assert re.search(r'config\.server_port\s*=\s*8080', firmware)
assert '"/ota/challenge"' in firmware and '"/ota/status"' in firmware
diag_header = (ROOT / "components/esp_lvgl_port/include/esp_lvgl_port_dsi_diag.h").read_text()
diag_port = (ROOT / "components/esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c").read_text()
assert "lvgl_port_dsi_diag_begin(void)" in diag_header
assert "lvgl_port_dsi_diag_read(void)" in diag_header
assert "lvgl_port_dsi_diag_end(void)" in diag_header
assert "portENTER_CRITICAL(&s_dsi_diag_lock)" in diag_port
assert "dsi_diag_record_flush(" in diag_port
assert "xSemaphoreTake(disp_ctx->dsi_flush_sem, portMAX_DELAY)" in diag_port
assert diag_port.index("xSemaphoreTake(disp_ctx->dsi_flush_sem, portMAX_DELAY)") < diag_port.index("dsi_diag_record_flush((uint32_t)elapsed_us")
assert "new (std::nothrow) upload_diagnostics(" in firmware, "diagnostics must not grow HTTP task stack"
assert "std::array<diag_second, k_diag_seconds> seconds" in firmware
assert "constexpr size_t k_diag_seconds = 96" in firmware
overlay = (ROOT / "components/esp_lcd/dsi/esp_lcd_panel_dpi.c").read_text()
overlay_cmake = (ROOT / "components/esp_lcd/CMakeLists.txt").read_text()
assert "ESP-IDF esp_lcd DPI source changed" in overlay_cmake
assert "ESP-IDF esp_lcd Kconfig changed" in overlay_cmake
assert "ESP-IDF esp_lcd component wiring changed" in overlay_cmake
assert "if(NOT CMAKE_BUILD_EARLY_EXPANSION AND" in overlay_cmake
assert "(NOT CONFIG_LCD_DSI_ISR_CACHE_SAFE OR NOT CONFIG_LCD_DSI_ISR_HANDLER_IN_IRAM)" in overlay_cmake
assert "config LCD_DSI_ISR_CACHE_SAFE" in (ROOT / "components/esp_lcd/Kconfig").read_text()
assert "dsi/esp_lcd_panel_dpi.c" in overlay_cmake
assert overlay.index("atomic_fetch_add_explicit(&s_bridge_underruns") < overlay.index(
    "mipi_dsi_brg_ll_clear_interrupt_status(hal->bridge, intr_status)")
assert "atomic_fetch_add_explicit(&s_dma_frames" in overlay
assert "esp_lcd_dpi_diagnostics_read()" in firmware
assert "bridge_underruns=%lu dma_frames=%lu" in firmware
assert '"diagnostics unavailable: no memory"' in firmware
assert "s_diag_ui_active.store(false" in firmware and "lvgl_port_dsi_diag_end();" in firmware
assert upload.index("diagnostics->report(") < upload.index("esp_ota_get_partition_description(")
assert "cyberdeck_ota::note_ui_refresh(" in ui and "s_ota_timer = lv_timer_create(process_ota_confirmation, 250" in ui
# The default theme runs in light mode: every `lv_btn` inherits a rounded radius
# and a grey drop shadow offset downwards, which paints a light band along the
# bottom edge of the dark OTA panel.  Both the panel and its decision buttons
# must flatten that inherited chrome, and the panel must not set a lone border
# color without a width (dead style that hinted at the old bottom line).
flatten = ui[ui.index("void flatten_btn_chrome(lv_obj_t *obj) {"):]
flatten = flatten[:flatten.index("}")]
for channel in ("lv_obj_set_style_radius(obj, 0, 0)",
                "lv_obj_set_style_border_width(obj, 0, 0)",
                "lv_obj_set_style_outline_width(obj, 0, 0)",
                "lv_obj_set_style_shadow_width(obj, 0, 0)",
                "lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0)"):
    assert channel in flatten, channel
modal = ui[ui.index("s_ota_modal = lv_btn_create(s_menu);"):ui.index("lv_label_set_text(s_ota_title, message);")]
assert "flatten_btn_chrome(s_ota_modal);" in modal
assert "flatten_btn_chrome(button);" in modal
# The light theme paints `lv_btn` with `bg_color_primary` (blue); the panel
# buttons must use the greyscale palette fill shared with the music controls.
assert "const lv_color_t BUTTON = lv_color_hex(0x303030);" in ui
assert "lv_obj_set_style_bg_color(button, BUTTON, 0);" in modal
assert "border_color(s_ota_modal" not in ui
assert modal.index("s_ota_modal = lv_btn_create(s_menu);") < modal.index("flatten_btn_chrome(s_ota_modal);")
print("OTA partition/auth/boot/serial/UI/pre-confirm-upload contracts: PASS")
