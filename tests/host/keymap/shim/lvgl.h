/*
 * Shim de teste HOST para "lvgl.h".
 *
 * O unico simbolo do LVGL usado por components/cyberdeck/src/platform/input/tab5_keyboard_keys.cpp
 * sao as constantes de teclas (enum lv_key_t), definidas no LVGL real em
 * managed_components/lvgl__lvgl/include/lvgl/core/lv_group.h.
 *
 * Este shim replica EXATAMENTE os valores do LVGL 9.6.0 (verificado na criacao).
 * O target `make verify` do Makefile compara este shim contra o header real do
 * LVGL gerenciado e falha o build em caso de divergencia (drift).
 *
 * Nao usar este shim em builds de firmware.
 */
#ifndef LVGL_H
#define LVGL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Espelho fiel de lv_key_t (LVGL 9.6.0, lv_group.h). */
typedef enum {
    LV_KEY_UP        = 17,  /* 0x11 */
    LV_KEY_DOWN      = 18,  /* 0x12 */
    LV_KEY_RIGHT     = 19,  /* 0x13 */
    LV_KEY_LEFT      = 20,  /* 0x14 */
    LV_KEY_ESC       = 27,  /* 0x1B */
    LV_KEY_DEL       = 127, /* 0x7F */
    LV_KEY_BACKSPACE = 8,   /* 0x08 */
    LV_KEY_ENTER     = 10,  /* 0x0A, '\n' */
    LV_KEY_NEXT      = 9,   /* 0x09, '\t' */
    LV_KEY_PREV      = 11,  /* 0x0B */
    LV_KEY_HOME      = 2,   /* 0x02, STX */
    LV_KEY_END       = 3,   /* 0x03, ETX */
} lv_key_t;

/* Espelho de lv_display_rotation_t (LVGL 9.x, lv_display.h). */
typedef enum {
    LV_DISPLAY_ROTATION_0 = 0,
    LV_DISPLAY_ROTATION_90,
    LV_DISPLAY_ROTATION_180,
    LV_DISPLAY_ROTATION_270
} lv_display_rotation_t;

#ifdef __cplusplus
}
#endif

#endif /* LVGL_H */
