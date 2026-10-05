#include "apps/system/cyberdeck_system_apps.h"

#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/runtime/cyberdeck_resource_catalog.h"
#include "apps/runtime/cyberdeck_sd_package.h"
#include "apps/demo/cyberdeck_demo_app.h"
#include "apps/bluetooth/ble_mgr.h"
#include "apps/screenshot/screenshot_server.h"
#include "apps/serial/cyberdeck_serial_bridge.h"
#include "apps/system/cyberdeck_service_ports.h"
#include "apps/shell/cyberdeck_shell_app.h"
#include "apps/ssh/ssh_client.h"
#include "apps/wifi/wifi_mgr.h"
#include "platform/logging/event_log.h"
#include "apps/system/cyberdeck_recovery.h"

#include "esp_log.h"

#include <initializer_list>
#include <cstdint>

namespace {

constexpr const char *TAG = "system_apps";
constexpr uint32_t k_ssh_lifecycle_timeout_ms = 1000;
constexpr uint32_t k_wifi_lifecycle_timeout_ms = 8000;
constexpr uint32_t k_ssh_task_stack_bytes = 24576;
constexpr uint32_t k_ssh_event_queue_depth = 8;
constexpr uint32_t k_ble_host_task_stack_bytes = 8192;
constexpr uint32_t k_ble_command_queue_depth = 8;
constexpr uint32_t k_serial_task_stack_bytes = 8192;
constexpr uint32_t k_screenshot_task_stack_bytes = 6144;
constexpr uint32_t k_screenshot_control_queue_depth = 8;

bool start_event_log() { return event_log_init() == ESP_OK; }
bool stop_event_log() { return false; }

bool start_ssh() { return true; }
bool stop_ssh()
{
    return ssh_client_disconnect_and_wait(k_ssh_lifecycle_timeout_ms);
}

bool start_screenshot()
{
    if (screenshot_server_start() != ESP_OK) return false;
    if (wifi_mgr_add_state_callback(screenshot_server_wifi_state, nullptr) != ESP_OK) {
        (void)screenshot_server_stop(1000);
        return false;
    }
    return true;
}

bool stop_screenshot()
{
    const esp_err_t removed = wifi_mgr_remove_state_callback(screenshot_server_wifi_state, nullptr);
    if (removed != ESP_OK && removed != ESP_ERR_NOT_FOUND) return false;
    return screenshot_server_stop(1000) == ESP_OK;
}

bool start_wifi() { return wifi_mgr_start() == ESP_OK; }
bool stop_wifi() { return wifi_mgr_stop(k_wifi_lifecycle_timeout_ms) == ESP_OK; }

bool start_serial() { return cyberdeck_apps::service_ports::serial_start(); }
bool stop_serial() { return cyberdeck_apps::service_ports::serial_stop(2000); }

bool start_bluetooth() { return ble_mgr_start() == ESP_OK; }
bool stop_bluetooth() { return ble_mgr_stop() == ESP_OK; }

class service_application final : public cyberdeck_apps::application {
public:
    using lifecycle_fn = bool (*)();

    service_application(cyberdeck_apps::manifest manifest, lifecycle_fn start,
                        lifecycle_fn stop, bool idempotent = false)
        : manifest_(manifest), start_(start), stop_(stop), idempotent_(idempotent)
    {
    }

    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }

    bool start() override
    {
        if (running_) return idempotent_;
        if (start_ == nullptr || !start_()) return false;
        running_ = true;
        return true;
    }

    bool stop() override
    {
        if (!running_) return idempotent_;
        if (stop_ == nullptr || !stop_()) return false;
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
    bool idempotent_;
    bool running_ = false;
};

class event_log_logger final : public cyberdeck_apps::logger {
public:
    void write_event(const event &value) override
    {
        const char level = value.severity == logger::level::error ? 'E' :
                           value.severity == logger::level::warning ? 'W' :
                           value.severity == logger::level::debug ? 'D' : 'I';
        event_log_write(level, value.tag, value.payload);
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
        if (item.resource_count == item.resources.size()) {
            /* Preserve overflow for the runtime validator instead of silently
             * turning a max+1 declaration into a valid manifest. */
            ++item.resource_count;
            break;
        }
        item.resources[item.resource_count++] = resource;
    }
    item.lifecycle_timeout_ms = lifecycle_timeout_ms;
    item.type = type;
    for (const std::string_view capability : capabilities) {
        if (item.capability_count == item.capabilities.size()) {
            ++item.capability_count;
            break;
        }
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
                                               {"storage", "event_log"}, 1000,
                                 cyberdeck_apps::app_type::service,
                                                {"event_log"}, 6144, 16),
                                 start_event_log, stop_event_log, true};
/* cyberdeck.shell is registered as the real foreground application from
 * cyberdeck_shell_app: the supervisor owns the console lifecycle, while the UI
 * composition only lends its session host (see attach_console). */
/* Wi-Fi brings up the C6 radio, SD storage and the STA netif, so its lifecycle
 * budget is larger than the trivial services. */
service_application s_wifi{make_manifest("cyberdeck.wifi", "Wi-Fi service",
                                          "Wi-Fi connectivity and network management", {"cyberdeck.event_log"},
                                         {"network", "storage"}, 8000,
                                          cyberdeck_apps::app_type::service,
                                          {"network", "storage"}, 4096, 8),
                            start_wifi, stop_wifi, true};
service_application s_serial{make_manifest("cyberdeck.serial", "Serial bridge",
                                            "USB Serial-JTAG NDJSON control bridge",
                                             {"cyberdeck.event_log", "cyberdeck.shell"}, {"serial", "input"}, 2000,
                                            cyberdeck_apps::app_type::service,
                                             {"serial", "input"}, k_serial_task_stack_bytes, 0),
                               start_serial, stop_serial, true};
service_application s_ssh{make_manifest("cyberdeck.ssh", "SSH service",
                                         "Asynchronous SSH client service",
                                          {"cyberdeck.event_log", "cyberdeck.wifi"}, {"network", "storage"}, 1000,
                                          cyberdeck_apps::app_type::background,
                                          {"network", "storage"}, k_ssh_task_stack_bytes,
                                          k_ssh_event_queue_depth),
                            start_ssh, stop_ssh, true};
service_application s_screenshot{make_manifest("cyberdeck.screenshot", "Screenshot service",
                                               "Local-network screenshot HTTP endpoint",
                                                {"cyberdeck.event_log", "cyberdeck.wifi"}, {"display", "network"}, 1000,
                                                cyberdeck_apps::app_type::service,
                                                {"display", "network"}, k_screenshot_task_stack_bytes,
                                                k_screenshot_control_queue_depth),
                                   start_screenshot, stop_screenshot, true};
service_application s_bluetooth{make_manifest("cyberdeck.bluetooth", "Bluetooth service",
                                                 "ESP-Hosted BLE manager", {"cyberdeck.event_log"}, {"ble"}, 1000,
                                               cyberdeck_apps::app_type::background,
                                                {"ble"}, k_ble_host_task_stack_bytes,
                                               k_ble_command_queue_depth),
                                  start_bluetooth, stop_bluetooth, true};

cyberdeck_apps::application *const k_apps[] = {
    &s_event_log,
    &cyberdeck_shell_app::global_application(),
    &s_wifi, &s_serial, &s_ssh, &s_screenshot, &s_bluetooth, &s_demo,
};

bool s_registered = false;

} // namespace

extern "C" esp_err_t cyberdeck_system_apps_register(void)
{
    if (s_registered) return ESP_OK;
    const auto &catalog = cyberdeck_apps::compiled_resources();
    std::size_t invalid_assets = 0;
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        const auto *asset = catalog.at(index);
        if (asset == nullptr || !cyberdeck_apps::verify_asset(*asset)) ++invalid_assets;
    }
    // SD input is optional metadata/data only: absence is diagnostic, never a
    // boot failure, and this boundary does not install or execute contents.
    const cyberdeck_apps::sd_package_view absent_package{};
    const bool valid_optional_package = cyberdeck_apps::global_runtime().inspect_sd_package(absent_package);
    ESP_LOGI(TAG, "distribution assets=%u invalid=%u sd_package=%s",
             static_cast<unsigned>(catalog.size()), static_cast<unsigned>(invalid_assets),
             valid_optional_package ? "valid" : "absent");
    cyberdeck_apps::global_runtime().set_logger(&s_event_logger);
    for (cyberdeck_apps::application *app : k_apps) {
        if (!cyberdeck_apps::global_runtime().register_application(*app)) {
            ESP_LOGE(TAG, "failed to register system app");
            return ESP_ERR_NO_MEM;
        }
        const auto &saved = cyberdeck_recovery::current();
        const std::string_view reason = cyberdeck_recovery::error_for(saved, app->get_manifest().id);
        if (!reason.empty()) {
            (void)cyberdeck_apps::global_runtime().restore_failure_reason(app->get_manifest().id, reason);
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
    const bool started = runtime.start_all();
    if (!started) {
        for (const char *id : {"cyberdeck.event_log", "cyberdeck.shell", "cyberdeck.wifi", "cyberdeck.serial",
                               "cyberdeck.ssh", "cyberdeck.screenshot", "cyberdeck.bluetooth"}) {
            if (runtime.state(id) == cyberdeck_apps::app_state::failed) {
                cyberdeck_recovery::record_app_error(id, runtime.failure_reason(id).data());
                ESP_LOGW(TAG, "system app failed: %s (%.*s)", id,
                         static_cast<int>(runtime.failure_reason(id).size()),
                         runtime.failure_reason(id).data());
            }
        }
    }
    return started ? ESP_OK : ESP_FAIL;
}

extern "C" esp_err_t cyberdeck_system_apps_start_safe_mode(void)
{
    cyberdeck_apps::runtime &runtime = cyberdeck_apps::global_runtime();
    esp_err_t result = ESP_OK;
    for (const char *id : {"cyberdeck.event_log", "cyberdeck.shell", "cyberdeck.serial"}) {
        if (!runtime.start_application(id)) {
            cyberdeck_recovery::record_app_error(id, runtime.failure_reason(id).data());
            ESP_LOGE(TAG, "safe-mode app failed: %s (%.*s)", id,
                     static_cast<int>(runtime.failure_reason(id).size()),
                     runtime.failure_reason(id).data());
            result = ESP_FAIL;
        }
    }
    return result;
}

extern "C" esp_err_t cyberdeck_system_apps_commit_ready(void)
{
    return cyberdeck_recovery::commit_ready();
}
