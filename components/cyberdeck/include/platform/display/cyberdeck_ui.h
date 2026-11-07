#pragma once
#include "platform/storage/cyberdeck_sd_io_coordinator.h"

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
void cyberdeck_ui_bind_sd_io(cyberdeck_sd_io::coordinator &coordinator);
extern "C" {
#endif

esp_err_t cyberdeck_ui_init(void);
void cyberdeck_ui_deinit(void);
void cyberdeck_keyboard_input(const char *text, size_t length, uint8_t modifier, uint32_t special_key);
/* Submete um evento e espera o LVGL consumi-lo antes de retornar. Serializa
 * texto e Enter: o LVGL 9.6 agenda cada lv_async_call em um timer proprio, e
 * submissoes concorrentes podem inverter a ordem. */
bool cyberdeck_keyboard_wait_delivered(uint32_t timeout_ms);
/* Submete um evento com confirmacao de consumo, descartando um sinal residual
 * de um evento anterior que tenha expirado. */
bool cyberdeck_keyboard_submit_reporting(const char *text, size_t length, uint8_t modifier,
                                         uint32_t special_key);
esp_err_t cyberdeck_ui_term_dump(char *buffer, size_t capacity, size_t *out_bytes, int *out_truncated);
bool cyberdeck_ui_poweroff_freeze(uint32_t timeout_ms);
bool cyberdeck_ui_audit_quiesce(uint32_t timeout_ms);
void cyberdeck_ui_poweroff_message(const char *message);

#ifdef __cplusplus
}
#endif
