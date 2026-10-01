#include "apps/system/cyberdeck_system_apps.h"

#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/demo/cyberdeck_demo_app.h"
#include "apps/bluetooth/ble_mgr.h"
#include "apps/screenshot/screenshot_server.h"
#include "apps/serial/cyberdeck_serial_bridge.h"
#include "apps/ssh/ssh_client.h"
#include "apps/wifi/wifi_mgr.h"
#include "platform/logging/event_log.h"

#include "esp_log.h"

#include <initializer_list>

namespace {

constexpr const char *TAG = "system_apps";

bool start_shell() { return true; }
bool stop_shell() { return false; }

bool start_event_log() { return event_log_init() == ESP_OK; }
bool stop_event_log() { return false; }

bool start_ssh() { return true; }
bool stop_ssh()
{
    ssh_client_disconnect();
    return true;
}

bool start_screenshot()
{
    return screenshot_server_init() == ESP_OK &&
           wifi_mgr_add_state_callback(screenshot_server_wifi_state, nullptr) == ESP_OK;
}

bool start_wifi() { return wifi_mgr_start() == ESP_OK; }

bool start_serial() { return cyberdeck_serial::bridge_start(); }
bool stop_serial() { return false; }

bool start_bluetooth() { return ble_mgr_start() == ESP_OK; }
bool stop_bluetooth() { return ble_mgr_stop() == ESP_OK; }

class service_application final : public cyberdeck_apps::application {
public:
    using lifecycle_fn = bool (*)();

    service_application(cyberdeck_apps::manifest manifest, lifecycle_fn start,
                        lifecycle_fn stop)
        : manifest_(manifest), start_(start), stop_(stop)
    {
    }

    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }

    bool start() override
    {
        if (running_) return false;
        if (start_ == nullptr || !start_()) return false;
        running_ = true;
        return true;
    }

    bool stop() override
    {
        if (!running_ || stop_ == nullptr || !stop_()) return false;
        running_ = false;
        return true;
    }

    bool running() const override { return running_; }

    cyberdeck_apps::result execute(std::string_view, std::string_view) override
    {
        return {cyberdeck_apps::result_status::rejected,
                "app: service application has no shell command\n"};
    }

private:
    cyberdeck_apps::manifest manifest_;
    lifecycle_fn start_;
    lifecycle_fn stop_;
    bool running_ = false;
};

class event_log_logger final : public cyberdeck_apps::logger {
public:
    void write(char level, const char *tag, const char *message) override
    {
        event_log_write(level, tag, message);
    }

    std::size_t latest(std::size_t max_events, line_callback callback, void *context) override
    {
        return event_log_latest(max_events, callback, context);
    }
};

cyberdeck_apps::manifest make_manifest(std::string_view id, std::string_view name,
                                       std::string_view description,
                                       std::initializer_list<std::string_view> dependencies,
                                       std::initializer_list<std::string_view> resources,
                                       std::uint32_t lifecycle_timeout_ms = 1000,
                                       cyberdeck_apps::app_type type = cyberdeck_apps::app_type::service,
                                       std::initializer_list<std::string_view> capabilities = {},
                                       std::uint32_t stack_bytes = 0,
                                       std::uint32_t queue_depth = 0,
                                       std::initializer_list<std::string_view> commands = {})
{
    cyberdeck_apps::manifest item{id, name, "1.0.0", description, {}};
    for (const std::string_view dependency : dependencies) {
        if (item.dependency_count == item.dependencies.size()) break;
        item.dependencies[item.dependency_count++] = dependency;
    }
    for (const std::string_view resource : resources) {
        if (item.resource_count == item.resources.size()) break;
        item.resources[item.resource_count++] = resource;
    }
    item.lifecycle_timeout_ms = lifecycle_timeout_ms;
    item.type = type;
    for (const std::string_view capability : capabilities) {
        if (item.capability_count == item.capabilities.size()) break;
        item.capabilities[item.capability_count++] = capability;
    }
    item.stack_bytes = stack_bytes;
    item.queue_depth = queue_depth;
    for (const std::string_view command : commands) {
        if (item.command_count == item.commands.size()) break;
        item.commands[item.command_count++] = command;
    }
    return item;
}

cyberdeck_apps::demo_application s_demo;
event_log_logger s_event_logger;
service_application s_event_log{make_manifest("cyberdeck.event_log", "Event log service",
                                               "Persistent bounded event log for applications", {},
                                               {"storage", "logging"}, 1000,
                                               cyberdeck_apps::app_type::service,
                                               {"logging"}, 6144, 16),
                                start_event_log, stop_event_log};
service_application s_shell{make_manifest("cyberdeck.shell", "Terminal shell",
                                           "Primary foreground terminal application", {"cyberdeck.event_log"},
                                           {"display", "input", "storage"}, 1000,
                                           cyberdeck_apps::app_type::foreground,
                                           {"display", "input", "storage", "shell"}, 8192, 8),
                            start_shell, stop_shell};
/* Wi-Fi brings up the C6 radio, SD storage and the STA netif, so its lifecycle
 * budget is larger than the trivial services. */
service_application s_wifi{make_manifest("cyberdeck.wifi", "Wi-Fi service",
                                          "Wi-Fi connectivity and network management", {"cyberdeck.event_log"},
                                         {"network", "storage"}, 8000,
                                         cyberdeck_apps::app_type::service,
                                         {"network", "storage"}, 4096, 8),
                           start_wifi, nullptr};
service_application s_serial{make_manifest("cyberdeck.serial", "Serial bridge",
                                            "USB Serial-JTAG NDJSON control bridge",
                                             {"cyberdeck.event_log", "cyberdeck.shell"}, {"serial", "input"}, 2000,
                                            cyberdeck_apps::app_type::service,
                                            {"serial", "input"}, 4096, 8),
                              start_serial, stop_serial};
service_application s_ssh{make_manifest("cyberdeck.ssh", "SSH service",
                                         "Asynchronous SSH client service",
                                          {"cyberdeck.event_log", "cyberdeck.wifi"}, {"network", "storage"}, 1000,
                                         cyberdeck_apps::app_type::background,
                                         {"network", "storage"}, 6144, 8),
                           start_ssh, stop_ssh};
service_application s_screenshot{make_manifest("cyberdeck.screenshot", "Screenshot service",
                                               "Local-network screenshot HTTP endpoint",
                                                {"cyberdeck.event_log", "cyberdeck.wifi"}, {"display", "network"}, 1000,
                                               cyberdeck_apps::app_type::service,
                                               {"display", "network"}, 6144, 1),
                                  start_screenshot, nullptr};
service_application s_bluetooth{make_manifest("cyberdeck.bluetooth", "Bluetooth service",
                                               "ESP-Hosted BLE manager", {"cyberdeck.event_log"}, {"bluetooth"}, 1000,
                                              cyberdeck_apps::app_type::background,
                                              {"bluetooth"}, 8192, 8),
                                 start_bluetooth, stop_bluetooth};

cyberdeck_apps::application *const k_apps[] = {
    &s_event_log, &s_shell, &s_wifi, &s_serial, &s_ssh, &s_screenshot, &s_bluetooth, &s_demo,
};

bool s_registered = false;

} // namespace

extern "C" esp_err_t cyberdeck_system_apps_register(void)
{
    if (s_registered) return ESP_OK;
    cyberdeck_apps::global_runtime().set_logger(&s_event_logger);
    for (cyberdeck_apps::application *app : k_apps) {
        if (!cyberdeck_apps::global_runtime().register_application(*app)) {
            ESP_LOGE(TAG, "failed to register system app");
            return ESP_ERR_NO_MEM;
        }
    }
    s_registered = true;
    return ESP_OK;
}

extern "C" esp_err_t cyberdeck_system_apps_start_logging(void)
{
    cyberdeck_apps::runtime &runtime = cyberdeck_apps::global_runtime();
    return runtime.start_application("cyberdeck.event_log") ? ESP_OK : ESP_FAIL;
}

extern "C" esp_err_t cyberdeck_system_apps_start(void)
{
    cyberdeck_apps::runtime &runtime = cyberdeck_apps::global_runtime();
    if (!runtime.start_all()) {
        for (const char *id : {"cyberdeck.shell", "cyberdeck.wifi", "cyberdeck.serial",
                               "cyberdeck.ssh", "cyberdeck.screenshot", "cyberdeck.bluetooth"}) {
            if (runtime.state(id) == cyberdeck_apps::app_state::failed) {
                ESP_LOGW(TAG, "system app failed: %s (%.*s)", id,
                         static_cast<int>(runtime.failure_reason(id).size()),
                         runtime.failure_reason(id).data());
            }
        }
    }
    return ESP_OK;
}
