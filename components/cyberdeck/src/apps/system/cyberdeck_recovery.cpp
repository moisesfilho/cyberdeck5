#include "apps/system/cyberdeck_recovery.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <cstring>

namespace cyberdeck_recovery {
namespace {
constexpr char k_namespace[] = "cyberdeck_recovery";
constexpr char k_key[] = "state";
state s_state;
bool s_available = false;

esp_err_t save()
{
    if (!s_available) return ESP_ERR_NVS_NOT_INITIALIZED;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(k_namespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, k_key, &s_state, sizeof(s_state));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    return err;
}

template <typename Transition>
esp_err_t save_after_transition(Transition transition)
{
    if (!s_available || !transition(s_state)) return ESP_FAIL;
    return save();
}
}

esp_err_t init()
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(k_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err; /* Never erase a damaged/unavailable NVS. */
    std::size_t size = sizeof(s_state);
    err = nvs_get_blob(handle, k_key, &s_state, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_state = {};
        err = ESP_OK;
    } else if (err != ESP_OK || size != sizeof(s_state) || s_state.version != k_state_version) {
        nvs_close(handle);
        return ESP_ERR_INVALID_SIZE;
    }
    nvs_close(handle);
    s_available = true;
    (void)begin_boot(s_state);
    return save();
}

bool safe_mode() { return s_state.safe_mode_latched; }
bool persistent() { return s_available; }
const state &current() { return s_state; }
esp_err_t commit_ready() { return save_after_transition([](state &value) { return cyberdeck_recovery::commit_ready(value); }); }
esp_err_t clear_safe_mode() { return save_after_transition([](state &value) { return cyberdeck_recovery::clear_safe_mode(value); }); }
void record_app_error(const char *app, const char *message)
{
    if (app != nullptr && message != nullptr && record_error(s_state, app, message)) (void)save();
}
} // namespace cyberdeck_recovery
