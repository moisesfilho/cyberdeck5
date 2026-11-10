#include "platform/display/cyberdeck_ui_post.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"

namespace cyberdeck_ui_post {

bool post(callback fn, void *payload) {
    if (fn == nullptr)
        return false;
    // Ordem preservada do worker do cat/completion: agenda primeiro e so acorda a
    // task do display depois de o call estar na fila do LVGL.  Uma recusa nao
    // acorda nada e mantem a posse do payload com o chamador.
    if (lv_async_call(fn, payload) != LV_RESULT_OK)
        return false;
    (void)lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr);
    return true;
}

} // namespace cyberdeck_ui_post
