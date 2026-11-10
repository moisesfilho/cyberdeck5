#pragma once

/*
 * Porta estreita de despacho para a task do display (REQ-TAB-05, TEST-WM-05).
 *
 * Camadas de aplicacao precisam entregar trabalho concluido em segundo plano na
 * task que possui o LVGL, mas nao podem conhecer o LVGL (nem `lv_async_call`,
 * nem `esp_lvgl_port`).  Esta interface nao expoe nenhum tipo LVGL: recebe um
 * callback com payload opaco e devolve se o agendamento foi aceito.
 *
 * Contrato:
 *  - nunca executa `fn` na propria chamada e nunca bloqueia o chamador;
 *  - sucesso significa "agendado e task do display acordada";
 *  - falha significa "NAO entregue": o payload continua pertencendo ao chamador,
 *    que deve libera-lo, e nenhum wake e emitido;
 *  - o payload precisa continuar valido ate o callback rodar ou ate a falha ser
 *    reportada; a porta nao o copia nem o libera.
 */
namespace cyberdeck_ui_post {

using callback = void (*)(void *payload);

bool post(callback fn, void *payload);

} // namespace cyberdeck_ui_post
