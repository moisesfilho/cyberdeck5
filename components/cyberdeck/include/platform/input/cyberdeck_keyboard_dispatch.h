#pragma once

#include <cstddef>
#include <cstdint>

namespace cyberdeck_keyboard_dispatch {

using callback = void (*)(const char *text, std::size_t length, std::uint8_t modifier,
                          std::uint32_t special_key, void *context);

class dispatcher {
public:
    bool start(callback handler, void *context);
    void stop();
    bool invalidate_pending();
    /* Devolve true somente quando o evento foi entregue a fila e o callback
     * assincrono foi agendado; false quando foi descartado (fila cheia,
     * alocacao, mutex ou falha de agendamento). `report` habilita a confirmacao
     * de consumo por wait_delivered(): o LVGL 9.6 agenda cada lv_async_call em
     * um timer proprio, entao submissoes concorrentes podem inverter a ordem e
     * quem depende de texto+Enter deve esperar cada evento ser processado. */
    bool submit(const char *text, std::size_t length, std::uint8_t modifier,
                std::uint32_t special_key, bool report = false);
    /* Espera o consumo de um evento submetido com report=true. */
    bool wait_delivered(std::uint32_t timeout_ms);

private:
    callback handler_ = nullptr;
    void *context_ = nullptr;
};

} // namespace cyberdeck_keyboard_dispatch
