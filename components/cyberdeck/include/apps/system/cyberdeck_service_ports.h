#pragma once

#include "apps/bluetooth/ble_mgr.h"
#include "apps/ssh/ssh_client.h"
#include "apps/wifi/wifi_mgr.h"
#include "esp_err.h"

#include <cstddef>
#include <cstdint>

namespace cyberdeck_apps::service_ports {

void wifi_process_state_callbacks();
esp_err_t wifi_connect(const char *ssid, const char *password);
esp_err_t wifi_cancel_connection();
void wifi_forget(const char *ssid);
bool wifi_enabled();
std::uint64_t wifi_current_token();
bool wifi_status(wifi_status_t *out);
esp_err_t wifi_scan(wifi_scan_cb_t callback, void *context);
bool wifi_cancel_scan(wifi_scan_cb_t callback, void *context);
void wifi_set_state_callback(wifi_state_cb_t callback, void *context);

ssh_client_state_t ssh_state();
ssh_client_generation_t ssh_generation();
esp_err_t ssh_connect(const char *user, const char *host, int port,
                      ssh_rx_cb_t data_callback, ssh_state_cb_t state_callback);
esp_err_t ssh_send_data(const char *data, std::size_t length);
esp_err_t ssh_send_password(const char *password);
void ssh_accept_host_key();
bool ssh_disconnect_and_wait(std::uint32_t timeout_ms);

esp_err_t ble_enqueue(const ble_mgr_cmd_t *command, TickType_t timeout_ticks);
ble_mgr_observer_handle_t ble_register_observer(ble_mgr_observer_cb_t callback, void *context);
void ble_unregister_observer(ble_mgr_observer_handle_t handle);

bool serial_start();

} // namespace cyberdeck_apps::service_ports
