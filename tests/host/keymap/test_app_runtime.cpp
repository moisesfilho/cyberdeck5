#include "apps/runtime/cyberdeck_app_runtime.h"

#include <chrono>
#include <cstdio>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", message);
    }
}

class fake_app final : public cyberdeck_apps::application {
public:
    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override
    {
        if (running_) return false;
        running_ = true;
        return true;
    }
    bool stop() override
    {
        if (!running_) return false;
        running_ = false;
        return true;
    }
    bool running() const override { return running_; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override
    {
        if (!running_) return {cyberdeck_apps::result_status::rejected, "not running\n"};
        return {cyberdeck_apps::result_status::handled, "ran\n"};
    }

private:
    const cyberdeck_apps::manifest manifest_{
        "test.app", "Test app", "1.0.0", "host test app", "testapp",
        {}, 0, {}, 0, 1000, "1", cyberdeck_apps::app_type::foreground,
        {"display"}, 1, 4096, 4, {"testapp"}, 1};
    bool running_ = false;
};

class non_foreground_command_app final : public cyberdeck_apps::application {
public:
    non_foreground_command_app(std::string_view id, std::string_view command,
                               cyberdeck_apps::app_type type)
    {
        manifest_.id = id;
        manifest_.name = id;
        manifest_.command = command;
        manifest_.type = type;
    }

    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override { ++start_calls; running_ = true; return true; }
    bool stop() override { running_ = false; return true; }
    bool running() const override { return running_; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override
    {
        return running_ ? cyberdeck_apps::result{cyberdeck_apps::result_status::handled, "ran\n"}
                        : cyberdeck_apps::result{cyberdeck_apps::result_status::rejected,
                                                 "not running\n"};
    }

    int start_calls = 0;

private:
    cyberdeck_apps::manifest manifest_{};
    bool running_ = false;
};

class dependent_app final : public cyberdeck_apps::application {
public:
    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override { running_ = true; return true; }
    bool stop() override { running_ = false; return true; }
    bool running() const override { return running_; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override { return {}; }

private:
    const cyberdeck_apps::manifest manifest_{
        "dependent.app", "Dependent app", "1.0.0", "dependent host app", {},
         {"test.app"}, 1, {"storage"}, 1, 1000};
    bool running_ = false;
};

class hooked_app final : public cyberdeck_apps::application {
public:
    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool init() override { ++init_calls; return true; }
    bool start() override { running_ = true; return true; }
    bool stop() override { running_ = false; return true; }
    bool teardown() override { ++teardown_calls; running_ = false; return true; }
    bool running() const override { return running_; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override { return {}; }

    int init_calls = 0;
    int teardown_calls = 0;

private:
    const cyberdeck_apps::manifest manifest_{
        "hooked.app", "Hooked app", "1.0.0", "explicit lifecycle hooks", {}};
    bool running_ = false;
};

/* Regression: a start hook that exceeds the declared budget but still returns
 * success must stay running and must not block its dependents. On device this
 * was wifi_mgr_start(), which legitimately takes seconds to bring up the C6
 * radio and SD storage. */
class slow_app final : public cyberdeck_apps::application {
public:
    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
        while (std::chrono::steady_clock::now() < deadline) {
        }
        running_ = true;
        return true;
    }
    bool stop() override
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
        while (std::chrono::steady_clock::now() < deadline) {
        }
        running_ = false;
        return true;
    }
    bool running() const override { return running_; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override { return {}; }

private:
    const cyberdeck_apps::manifest manifest_{
        "slow.app", "Slow app", "1.0.0", "slow start host app", {},
        {}, 0, {}, 0, 1};
    bool running_ = false;
};

class slow_dependent_app final : public cyberdeck_apps::application {
public:
    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override { running_ = true; return true; }
    bool stop() override { running_ = false; return true; }
    bool running() const override { return running_; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override { return {}; }

private:
    const cyberdeck_apps::manifest manifest_{
        "slow.dependent", "Slow dependent", "1.0.0", "depends on slow start", {},
        {"slow.app"}, 1, {}, 0, 1000};
    bool running_ = false;
};

class failing_app final : public cyberdeck_apps::application {
public:
    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override { return false; }
    bool stop() override { return true; }
    bool running() const override { return false; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override { return {}; }

private:
    const cyberdeck_apps::manifest manifest_{
        "failing.app", "Failing app", "1.0.0", "start always fails", {}};
};

class failing_dependent_app final : public cyberdeck_apps::application {
public:
    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override { running_ = true; return true; }
    bool stop() override { running_ = false; return true; }
    bool running() const override { return running_; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override { return {}; }

private:
    const cyberdeck_apps::manifest manifest_{
        "failing.dependent", "Failing dependent", "1.0.0", "blocked by failure", {},
        {"failing.app"}, 1, {}, 0, 1000};
    bool running_ = false;
};

} // namespace

int main()
{
    cyberdeck_apps::runtime runtime;
    fake_app app;
    dependent_app dependent;

    check(runtime.register_application(app), "first application registers");
    check(runtime.register_application(dependent), "dependent application registers");
    check(!runtime.register_application(app), "duplicate application is rejected");

    auto list = runtime.execute_line("app list");
    check(list.status == cyberdeck_apps::result_status::handled, "app list is handled");
    check(list.output == "test.app 1.0.0 registered\ndependent.app 1.0.0 registered\n",
          "app list is deterministic");

    auto info = runtime.execute_line("app info test.app");
    check(info.status == cyberdeck_apps::result_status::handled, "app info is handled");
    check(info.output.find("command: testapp\n") != std::string::npos, "app info exposes command");
    check(info.output.find("api_version: 1\n") != std::string::npos,
          "app info exposes API version");
    check(info.output.find("type: foreground\n") != std::string::npos,
          "app info exposes application type");
    check(info.output.find("stack_bytes: 4096\n") != std::string::npos,
          "app info exposes requested stack");
    check(info.output.find("queue_depth: 4\n") != std::string::npos,
          "app info exposes requested queue");
    check(info.output.find("capabilities: display\n") != std::string::npos,
          "app info exposes capabilities");
    check(info.output.find("commands: testapp\n") != std::string::npos,
          "app info exposes commands");

    auto before = runtime.execute_line("testapp");
    check(before.status == cyberdeck_apps::result_status::handled,
          "stopped foreground app starts lazily for command");
    check(before.output == "ran\n", "lazy-started foreground command returns output");
    check(app.running(), "lazy-started foreground app becomes running");
    check(runtime.state("test.app") == cyberdeck_apps::app_state::running,
          "lazy-started foreground app exposes running state");
    check(runtime.execute_line("testapp again").status == cyberdeck_apps::result_status::handled,
          "running foreground command remains dispatchable");

    auto start = runtime.execute_line("app start test.app");
    check(start.status == cyberdeck_apps::result_status::handled, "app start is handled");
    check(app.running(), "app becomes running");
    check(runtime.state("test.app") == cyberdeck_apps::app_state::running,
          "started app exposes running state");

    auto command = runtime.execute_line("testapp argument");
    check(command.status == cyberdeck_apps::result_status::handled, "registered command is dispatched");
    check(command.output == "ran\n", "registered command returns output");

    auto stop = runtime.execute_line("app stop test.app");
    check(stop.status == cyberdeck_apps::result_status::handled, "app stop is handled");
    check(!app.running(), "app becomes stopped");
    check(runtime.state("test.app") == cyberdeck_apps::app_state::registered,
          "stopped app returns to registered state");

    check(runtime.start_application("dependent.app"), "dependency start is declarative");
    check(app.running(), "dependency starts before dependent");
    std::array<std::string_view, cyberdeck_apps::k_max_resources> resources{};
    std::size_t resource_count = 0;
    check(runtime.resources("dependent.app", resources, resource_count),
          "manifest resources are exposed");
    check(resource_count == 1 && resources[0] == "storage", "resource list is bounded");
    check(runtime.restart_application("dependent.app"), "running app can be restarted");

    auto unknown = runtime.execute_line("app info missing");
    check(unknown.status == cyberdeck_apps::result_status::rejected, "unknown app is rejected");
    check(runtime.execute_line("not-an-app").status == cyberdeck_apps::result_status::not_handled,
          "unregistered command is left for passthrough");
    check(runtime.execute_line("wifi").status == cyberdeck_apps::result_status::not_handled,
          "service command names remain available to the shell parser");
    check(runtime.failure_reason("missing") == "application not found",
           "missing app has a diagnostic");

    cyberdeck_apps::runtime type_policy_runtime;
    non_foreground_command_app service("test.service", "service-command",
                                       cyberdeck_apps::app_type::service);
    non_foreground_command_app background("test.background", "background-command",
                                          cyberdeck_apps::app_type::background);
    check(type_policy_runtime.register_application(service), "service app registers");
    check(type_policy_runtime.register_application(background), "background app registers");
    check(type_policy_runtime.execute_line("service-command").status ==
              cyberdeck_apps::result_status::rejected,
          "stopped service command is rejected without auto-start");
    check(service.start_calls == 0 && !service.running(),
          "service command never lazy-starts");
    check(type_policy_runtime.execute_line("background-command").status ==
              cyberdeck_apps::result_status::rejected,
          "stopped background command is rejected without auto-start");
    check(background.start_calls == 0 && !background.running(),
          "background command never lazy-starts");

    cyberdeck_apps::runtime hook_runtime;
    hooked_app hooked;
    check(hook_runtime.register_application(hooked), "hooked app registers");
    check(hook_runtime.start_application("hooked.app"), "explicit init hook runs");
    check(hooked.init_calls == 1, "init hook is called once per start");
    check(hook_runtime.stop_application("hooked.app"), "explicit teardown hook runs");
    check(hooked.teardown_calls == 1, "teardown hook is called once per stop");

    /* Slow-but-successful start must not be reported as failed and must not
     * block dependents (device regression on wifi_mgr_start). */
    cyberdeck_apps::runtime slow_runtime;
    slow_app slow;
    slow_dependent_app slow_dependent;
    check(slow_runtime.register_application(slow), "slow app registers");
    check(slow_runtime.register_application(slow_dependent), "slow dependent registers");
    check(slow_runtime.start_application("slow.dependent"),
          "dependent starts even when dependency start exceeds its budget");
    check(slow_runtime.state("slow.app") == cyberdeck_apps::app_state::running,
          "slow successful start is running, not failed");
    check(slow_runtime.state("slow.dependent") == cyberdeck_apps::app_state::running,
          "dependent of a slow app still runs");
    check(slow_runtime.failure_reason("slow.app") == "start hook timeout",
          "budget overrun is kept as diagnostic");
    check(slow_runtime.stop_application("slow.app"), "slow app stops");
    check(slow_runtime.state("slow.dependent") == cyberdeck_apps::app_state::registered,
          "stopping a dependency stops its dependents first");
    check(slow_runtime.failure_reason("slow.app") == "stop hook timeout",
          "successful stop overrun is kept as diagnostic");

    /* A hook that genuinely fails must fail closed and block dependents. */
    cyberdeck_apps::runtime failing_runtime;
    failing_app failing;
    failing_dependent_app failing_dependent;
    check(failing_runtime.register_application(failing), "failing app registers");
    check(failing_runtime.register_application(failing_dependent), "failing dependent registers");
    check(!failing_runtime.start_application("failing.dependent"),
          "dependent start fails when dependency fails");
    check(failing_runtime.state("failing.app") == cyberdeck_apps::app_state::failed,
          "failed hook is reported as failed");
    check(failing_runtime.failure_reason("failing.app") == "start hook failed",
          "failed hook exposes reason");
    check(failing_runtime.state("failing.dependent") == cyberdeck_apps::app_state::failed,
          "dependent of a failed app is failed");
    check(failing_dependent.running() == false, "dependent hook is not invoked");
    check(failing_runtime.failure_reason("failing.dependent").find("failing.app") !=
              std::string_view::npos,
          "dependent diagnostic names the missing dependency");
    const auto failure_info = failing_runtime.execute_line("app info failing.app");
    check(failure_info.output.find("failure: start hook failed\n") != std::string::npos,
          "app info exposes the last failure reason");

    if (failures == 0) {
        std::printf("PASS: app runtime (%d checks)\n", checks);
        return 0;
    }
    std::printf("FAIL: %d of %d app runtime checks failed\n", failures, checks);
    return 1;
}
