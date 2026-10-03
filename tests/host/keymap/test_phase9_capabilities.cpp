#include "apps/runtime/cyberdeck_app_facades.h"

#include <chrono>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <thread>

namespace {
int failures = 0;
int checks = 0;
void check(bool value, const char *message)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL: %s\n", message); }
}

cyberdeck_apps::manifest make_manifest(const char *id,
                                       std::initializer_list<const char *> resources)
{
    cyberdeck_apps::manifest item{};
    item.id = id;
    item.name = id;
    item.version = "1";
    for (const char *name : resources) item.resources[item.resource_count++] = name;
    return item;
}

class app final : public cyberdeck_apps::application {
public:
    explicit app(cyberdeck_apps::manifest item) : manifest_(item) {}
    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool init() override { ++init_calls; return init_ok; }
    bool start() override
    {
        if (delay_ms != 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        running_ = start_ok;
        return start_ok;
    }
    bool stop() override { running_ = false; return true; }
    bool teardown() override { running_ = false; ++teardown_calls; return teardown_ok; }
    bool running() const override { return running_; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override { return {}; }

    cyberdeck_apps::manifest manifest_;
    bool init_ok = true;
    bool start_ok = true;
    bool teardown_ok = true;
    unsigned delay_ms = 0;
    int init_calls = 0;
    int teardown_calls = 0;

private:
    bool running_ = false;
};
} // namespace

int main()
{
    using namespace cyberdeck_apps;
    using cyberdeck_window_manager::view_context;

    /* TEST-CAP-01: deterministic grants for the four foundational resources. */
    runtime base_runtime;
    app base(make_manifest("base", {"display", "input", "storage", "network"}));
    check(base_runtime.register_application(base), "base manifest registers");
    check(base_runtime.start_application("base"), "base app starts");
    const grant base_grant = base.app_grant();
    check(base_grant.valid(), "running app receives a valid grant");
    check(base_grant.allows(resource::display) && base_grant.allows("input"),
          "display and input are granted");
    check(base_grant.allows(resource::storage) && base_grant.allows(resource::network),
          "storage and network are granted");
    check(!base_grant.allows(resource::ble) && !base_grant.allows("unknown"),
          "undeclared resources deny by default");

    /* TEST-CAP-02: exclusive BLE/Serial-JTAG/Screenshot grants and invalid manifests. */
    runtime exclusive_runtime;
    app ble(make_manifest("ble", {"ble"}));
    app serial(make_manifest("serial", {"serial"}));
    app screenshot(make_manifest("screenshot", {"screenshot"}));
    check(exclusive_runtime.register_application(ble), "BLE app registers");
    check(exclusive_runtime.register_application(serial), "Serial app registers");
    check(exclusive_runtime.register_application(screenshot), "Screenshot app registers");
    check(exclusive_runtime.start_all(), "independent exclusive apps start");
    check(ble.app_grant().allows(resource::ble) && !ble.app_grant().allows(resource::serial),
          "BLE grant is exclusive");
    check(serial.app_grant().allows(resource::serial) && !serial.app_grant().allows(resource::ble),
          "Serial grant is exclusive");
    check(screenshot.app_grant().allows(resource::screenshot) &&
              !screenshot.app_grant().allows(resource::ble),
          "Screenshot grant is exclusive");
    app unknown(make_manifest("unknown", {"no-such-resource"}));
    app duplicate(make_manifest("duplicate", {"ble", "ble"}));
    app over_limit(make_manifest("over-limit", {"display", "input", "storage", "network",
                                             "ble", "serial", "screenshot", "event_log"}));
    over_limit.manifest_.resource_count = k_max_resources + 1;
    check(!exclusive_runtime.register_application(unknown), "unknown resource is rejected");
    check(!exclusive_runtime.register_application(duplicate), "duplicate resource is rejected");
    check(!exclusive_runtime.register_application(over_limit), "maximum plus one is rejected");

    /* TEST-CAP-03: all typed facades re-check grants; invalid context is fail-closed. */
    runtime facade_runtime;
    app resources(make_manifest("resources", {"display", "input", "storage", "network", "ble",
                                          "serial", "screenshot", "event_log"}));
    check(facade_runtime.register_application(resources), "facade app registers");
    cyberdeck_window_manager::manager windows;
    const display_facade no_display{};
    check(!no_display.available(), "default display facade denies");
    check(facade_runtime.start_application("resources"), "facade app starts");
    const grant live = resources.app_grant();
    display_facade display(live, windows);
    input_facade input(live, windows);
    view_context context;
    check(display.available() && display.create(7, context), "display creates a view");
    check(input.available() && input.focus(context) && input.validate(context),
          "input uses an authorized context");
    check(!display.activate(view_context{}), "invalid display context is rejected");
    check(!display.notify(context, std::string(96, 'x')), "maximum notification is rejected");
    check(storage_facade(live).available() && network_facade(live).available(),
          "storage/network facades are authorized");
    check(ble_facade(live).available() && serial_facade(live).available() &&
              screenshot_facade(live).available(), "BLE/Serial/Screenshot are authorized");
    check(event_log_facade(live).available() && clock_facade(live).available() == false &&
              battery_facade(live).available() == false,
          "event log is authorized while undeclared clock/battery deny");
    check(facade_runtime.stop_application("resources"), "teardown succeeds");
    check(!live.valid() && !display.available() && !input.available(),
          "teardown revokes copied grants and facades");
    check(!display.activate(context), "stale view cannot be used after teardown");

    /* TEST-CAP-04: failed lifecycle, timeout, isolation and restart generation. */
    runtime lifecycle_runtime;
    app init_fail(make_manifest("init-fail", {"clock"})); init_fail.init_ok = false;
    app start_fail(make_manifest("start-fail", {"battery"})); start_fail.start_ok = false;
    app teardown_fail(make_manifest("teardown-fail", {"storage"})); teardown_fail.teardown_ok = false;
    app slow(make_manifest("slow", {"network"})); slow.delay_ms = 2; slow.manifest_.lifecycle_timeout_ms = 0;
    app independent(make_manifest("independent", {"display"}));
    check(lifecycle_runtime.register_application(init_fail) && lifecycle_runtime.register_application(start_fail) &&
              lifecycle_runtime.register_application(teardown_fail) && lifecycle_runtime.register_application(slow) &&
              lifecycle_runtime.register_application(independent), "lifecycle apps register independently");
    check(!lifecycle_runtime.start_application("init-fail") && !init_fail.app_grant().valid(),
          "init failure has no grant");
    check(!lifecycle_runtime.start_application("start-fail") && !start_fail.app_grant().valid(),
          "start failure has no grant");
    check(lifecycle_runtime.start_application("teardown-fail"), "teardown-fail starts");
    const grant old_generation = teardown_fail.app_grant();
    check(!lifecycle_runtime.stop_application("teardown-fail") && !old_generation.valid(),
          "failed teardown revokes grant");
    check(lifecycle_runtime.start_application("slow") && lifecycle_runtime.failure_reason("slow") ==
              "start hook timeout", "timeout is diagnostic, not authorization bypass");
    check(lifecycle_runtime.start_application("independent"), "independent app starts");
    const grant independent_generation = independent.app_grant();
    check(lifecycle_runtime.stop_application("slow") && independent.app_grant().valid(),
          "stopping one app preserves another app grant");
    check(lifecycle_runtime.restart_application("independent"), "restart creates a new grant");
    check(!independent_generation.valid() && independent.app_grant().valid(),
          "restart invalidates stale grant and issues a fresh one");

    if (failures == 0) { std::printf("PASS: phase 9 capabilities (%d checks)\n", checks); return 0; }
    std::printf("FAIL: %d of %d phase 9 checks failed\n", failures, checks);
    return 1;
}
