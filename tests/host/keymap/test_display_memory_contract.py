#!/usr/bin/env python3
"""Structural regressions for Tab5 LVGL memory allocation and cleanup."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


def main() -> None:
    bsp = (ROOT / "components/m5stack_tab5/src/bsp_display.c").read_text(encoding="utf-8")
    require("SOC_PSRAM_DMA_CAPABLE" in bsp and
            ".buff_spiram = draw_buffers_in_spiram" in bsp and
            ".sw_rotate = true" in bsp,
            "Tab5 buffers must use DMA-capable PSRAM where supported and retain rotation")
    require('"LVGL buffers: bytes=%lu buffer_free=%lu buffer_largest=%lu dma_free=%lu dma_largest=%lu internal_free=%lu"' in bsp,
            "BSP must log compatible buffer and internal heap budgets")
    require('"LVGL display initialization failed; touch init skipped"' in bsp and
            "lvgl_port_remove_disp(disp);" in bsp and
            "lvgl_port_deinit();" in bsp,
            "BSP failures must skip touch and tear down partial LVGL/display resources")

    port = (ROOT / "components/esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c").read_text(encoding="utf-8")
    error_path = port[port.find("err:"):]
    require("ret = ESP_ERR_NO_MEM;" in port and
            '"Not enough memory for LVGL buffer (rotation buffer) allocation!"' in port,
            "LVGL rotation allocation failure must set an explicit error")
    require("lv_display_delete(disp);" in error_path and "disp = NULL;" in error_path and
            "disp_ctx != NULL && disp_ctx->draw_buffs[2]" in error_path and
            "return NULL;" in error_path,
            "LVGL port failure must release partial display and buffers safely")

    music = (ROOT / "components/cyberdeck/src/apps/music/cyberdeck_music_player.cpp").read_text(encoding="utf-8")
    require("StaticTask_t *task_control = nullptr;" in music and
            "heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)" in music and
            "impl_->task_stack, impl_->task_control" in music,
            "Music static-task TCB must be allocated in internal memory")
    require(music.count("heap_caps_free(impl_->task_control);") >= 2,
            "Music TCB allocation must be freed on rollback and worker completion")
    print("PASS: display memory, LVGL cleanup, and music TCB contracts")


if __name__ == "__main__":
    main()
