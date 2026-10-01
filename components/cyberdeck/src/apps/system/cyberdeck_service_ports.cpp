#include "apps/system/cyberdeck_service_ports.h"

#include "apps/serial/cyberdeck_serial_bridge.h"

namespace cyberdeck_apps::service_ports {

void wifi_process_state_callbacks() { wifi_mgr_process_state_callbacks(); }
esp_err_t wifi_connect(const char *ssid, const char *password) { return wifi_mgr_connect(ssid, password); }
esp_err_t wifi_cancel_connection() { return wifi_mgr_cancel_connection(); }
void wifi_forget(const char *ssid) { wifi_mgr_forget(ssid); }
bool wifi_enabled() { return wifi_mgr_is_enabled(); }
std::uint64_t wifi_current_token() { return wifi_mgr_connection_token(); }
bool wifi_status(wifi_status_t *out) { return wifi_mgr_get_status(out) == ESP_OK; }
esp_err_t wifi_scan(wifi_scan_cb_t callback, void *context) { return wifi_mgr_scan(callback, context); }
bool wifi_cancel_scan(wifi_scan_cb_t callback, void *context)
{
    return wifi_mgr_cancel_scan(callback, context);
}
void wifi_set_state_callback(wifi_state_cb_t callback, void *context)
{
    wifi_mgr_set_state_callback(callback, context);
}

ssh_client_state_t ssh_state() { return ssh_client_get_state(); }
ssh_client_generation_t ssh_generation() { return ssh_client_generation(); }
esp_err_t ssh_connect(const char *user, const char *host, int port,
                      ssh_rx_cb_t data_callback, ssh_state_cb_t state_callback)
{
    return ssh_client_connect(user, host, port, data_callback, state_callback);
}
esp_err_t ssh_send_data(const char *data, std::size_t length)
{
    return ssh_client_send_data(data, length);
}
esp_err_t ssh_send_password(const char *password) { return ssh_client_send_password(password); }
void ssh_accept_host_key() { ssh_client_accept_host_key(); }
bool ssh_disconnect_and_wait(std::uint32_t timeout_ms)
{
    return ssh_client_disconnect_and_wait(timeout_ms);
}

esp_err_t ble_enqueue(const ble_mgr_cmd_t *command, TickType_t timeout_ticks)
{
    return ble_mgr_enqueue_cmd(command, timeout_ticks);
}
ble_mgr_observer_handle_t ble_register_observer(ble_mgr_observer_cb_t callback, void *context)
{
    return ble_mgr_register_observer(callback, context);
}
void ble_unregister_observer(ble_mgr_observer_handle_t handle) { ble_mgr_unregister_observer(handle); }

bool serial_start() { return cyberdeck_serial::bridge_start(); }

} // namespace cyberdeck_apps::service_ports
