#include "apps/runtime/cyberdeck_app_runtime.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", message);
    }
}

class fake_app final : public cyberdeck_apps::application {
  public:
    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool start() override {
        if (running_)
            return false;
        running_ = true;
        return true;
    }
    bool stop() override {
        if (!running_)
            return false;
        running_ = false;
        return true;
    }
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override {
        if (!running_)
            return {cyberdeck_apps::result_status::rejected, "not running\n"};
        return {cyberdeck_apps::result_status::handled, "ran\n"};
    }

  private:
    const cyberdeck_apps::manifest manifest_{"test.app",  "Test app", "1.0.0", "host test app",
                                             "testapp",   {},         0,       {},
                                             0,           1000,       "1",     cyberdeck_apps::app_type::foreground,
                                             {"display"}, 1,          4096,    4,
                                             {"testapp"}, 1};
    bool running_ = false;
};

class non_foreground_command_app final : public cyberdeck_apps::application {
  public:
    non_foreground_command_app(std::string_view id, std::string_view command, cyberdeck_apps::app_type type) {
        manifest_.id = id;
        manifest_.name = id;
        manifest_.command = command;
        manifest_.type = type;
    }

    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool start() override {
        ++start_calls;
        running_ = true;
        return true;
    }
    bool stop() override {
        running_ = false;
        return true;
    }
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override {
        return running_ ? cyberdeck_apps::result{cyberdeck_apps::result_status::handled, "ran\n"}
                        : cyberdeck_apps::result{cyberdeck_apps::result_status::rejected, "not running\n"};
    }

    int start_calls = 0;

  private:
    cyberdeck_apps::manifest manifest_{};
    bool running_ = false;
};

class dependent_app final : public cyberdeck_apps::application {
  public:
    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool start() override {
        running_ = true;
        return true;
    }
    bool stop() override {
        running_ = false;
        return true;
    }
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override {
        return {};
    }

  private:
    const cyberdeck_apps::manifest manifest_{
        "dependent.app", "Dependent app", "1.0.0", "dependent host app", {}, {"test.app"}, 1, {"storage"}, 1, 1000};
    bool running_ = false;
};

class hooked_app final : public cyberdeck_apps::application {
  public:
    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool init() override {
        ++init_calls;
        return true;
    }
    bool start() override {
        running_ = true;
        return true;
    }
    bool stop() override {
        running_ = false;
        return true;
    }
    bool teardown() override {
        ++teardown_calls;
        running_ = false;
        return true;
    }
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override {
        return {};
    }

    int init_calls = 0;
    int teardown_calls = 0;

  private:
    const cyberdeck_apps::manifest manifest_{"hooked.app", "Hooked app", "1.0.0", "explicit lifecycle hooks", {}};
    bool running_ = false;
};

/* Regression: a start hook that exceeds the declared budget but still returns
 * success must stay running and must not block its dependents. On device this
 * was wifi_mgr_start(), which legitimately takes seconds to bring up the C6
 * radio and SD storage. */
class slow_app final : public cyberdeck_apps::application {
  public:
    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool start() override {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
        while (std::chrono::steady_clock::now() < deadline) {
        }
        running_ = true;
        return true;
    }
    bool stop() override {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
        while (std::chrono::steady_clock::now() < deadline) {
        }
        running_ = false;
        return true;
    }
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override {
        return {};
    }

  private:
    const cyberdeck_apps::manifest manifest_{"slow.app", "Slow app", "1.0.0", "slow start host app", {}, {}, 0,
                                             {},         0,          1};
    bool running_ = false;
};

class slow_dependent_app final : public cyberdeck_apps::application {
  public:
    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool start() override {
        running_ = true;
        return true;
    }
    bool stop() override {
        running_ = false;
        return true;
    }
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override {
        return {};
    }

  private:
    const cyberdeck_apps::manifest manifest_{
        "slow.dependent", "Slow dependent", "1.0.0", "depends on slow start", {}, {"slow.app"}, 1, {}, 0, 1000};
    bool running_ = false;
};

class failing_app final : public cyberdeck_apps::application {
  public:
    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool start() override {
        return false;
    }
    bool stop() override {
        return true;
    }
    bool running() const override {
        return false;
    }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override {
        return {};
    }

  private:
    const cyberdeck_apps::manifest manifest_{"failing.app", "Failing app", "1.0.0", "start always fails", {}};
};

class failing_dependent_app final : public cyberdeck_apps::application {
  public:
    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool start() override {
        running_ = true;
        return true;
    }
    bool stop() override {
        running_ = false;
        return true;
    }
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override {
        return {};
    }

  private:
    const cyberdeck_apps::manifest manifest_{
        "failing.dependent", "Failing dependent", "1.0.0", "blocked by failure", {}, {"failing.app"}, 1, {}, 0, 1000};
    bool running_ = false;
};

class grant_test_app final : public cyberdeck_apps::application {
  public:
    explicit grant_test_app(std::string_view id = "grant.app") {
        manifest_.id = id;
    }
    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool init() override {
        if (capture_during_init)
            captured = app_grant();
        return init_ok;
    }
    bool start() override {
        if (capture_during_start)
            captured = app_grant();
        running_ = start_ok;
        return start_ok;
    }
    bool stop() override {
        running_ = false;
        return true;
    }
    bool teardown() override {
        if (on_teardown)
            on_teardown(app_grant());
        running_ = false;
        return true;
    }
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override {
        return {};
    }

    bool init_ok = true;
    bool start_ok = true;
    bool capture_during_init = false;
    bool capture_during_start = false;
    cyberdeck_apps::grant captured{};
    std::function<void(cyberdeck_apps::grant)> on_teardown;

  private:
    cyberdeck_apps::manifest manifest_{"grant.app", "Grant app", "1", "grant test", {}, {}, 0, {"display", "input"}, 2};
    bool running_ = false;
};

/* Configurable manifest application: every registration/start invariant below
 * is validated by the real runtime, so an invalid manifest is rejected by
 * production validation rather than by test-only plumbing. */
class configurable_app final : public cyberdeck_apps::application {
  public:
    explicit configurable_app(std::string_view id) {
        manifest_.id = id;
        manifest_.name = id;
    }
    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool init() override {
        return init_ok;
    }
    bool start() override {
        running_ = start_ok;
        return start_ok;
    }
    bool stop() override {
        running_ = false;
        return teardown_ok;
    }
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override {
        return {cyberdeck_apps::result_status::handled, "ran\n"};
    }

    cyberdeck_apps::manifest manifest_{};
    bool init_ok = true;
    bool start_ok = true;
    bool teardown_ok = true;

  private:
    bool running_ = false;
};

/* Logger sink used to observe lifecycle events emitted by the runtime. */
class recording_logger final : public cyberdeck_apps::logger {
  public:
    std::size_t latest(std::size_t, line_callback, void *) override {
        return 0;
    }
    std::vector<std::string> payloads;

  protected:
    void write_event(const event &value) override {
        payloads.emplace_back(value.payload);
    }
};

/* Stable ids for the registry-capacity fixture (manifest views must outlive
 * the runtime under test). */
constexpr const char *k_registry_full_ids[] = {
    "full.app00", "full.app01", "full.app02", "full.app03", "full.app04", "full.app05", "full.app06", "full.app07",
    "full.app08", "full.app09", "full.app10", "full.app11", "full.app12", "full.app13", "full.app14", "full.app15",
};

bool wait_for(std::condition_variable &cv, std::unique_lock<std::mutex> &lock, const std::function<bool()> &predicate) {
    return cv.wait_for(lock, std::chrono::seconds(3), predicate);
}

} // namespace

int main() {
    cyberdeck_apps::runtime runtime;
    fake_app app;
    dependent_app dependent;

    check(runtime.register_application(app), "first application registers");
    check(runtime.register_application(dependent), "dependent application registers");
    check(!runtime.register_application(app), "duplicate application is rejected");

    auto list = runtime.execute_line("app list");
    check(list.status == cyberdeck_apps::result_status::handled, "app list is handled");
    check(list.output == "test.app 1.0.0 registered\ndependent.app 1.0.0 registered\n", "app list is deterministic");

    auto info = runtime.execute_line("app info test.app");
    check(info.status == cyberdeck_apps::result_status::handled, "app info is handled");
    check(info.output.find("command: testapp\n") != std::string::npos, "app info exposes command");
    check(info.output.find("api_version: 1\n") != std::string::npos, "app info exposes API version");
    check(info.output.find("type: foreground\n") != std::string::npos, "app info exposes application type");
    check(info.output.find("stack_bytes: 4096\n") != std::string::npos, "app info exposes requested stack");
    check(info.output.find("queue_depth: 4\n") != std::string::npos, "app info exposes requested queue");
    check(info.output.find("capabilities: display\n") != std::string::npos, "app info exposes capabilities");
    check(info.output.find("commands: testapp\n") != std::string::npos, "app info exposes commands");

    auto before = runtime.execute_line("testapp");
    check(before.status == cyberdeck_apps::result_status::handled, "stopped foreground app starts lazily for command");
    check(before.output == "ran\n", "lazy-started foreground command returns output");
    check(app.running(), "lazy-started foreground app becomes running");
    check(runtime.state("test.app") == cyberdeck_apps::app_state::running,
          "lazy-started foreground app exposes running state");
    check(runtime.execute_line("testapp again").status == cyberdeck_apps::result_status::handled,
          "running foreground command remains dispatchable");

    auto start = runtime.execute_line("app start test.app");
    check(start.status == cyberdeck_apps::result_status::handled, "app start is handled");
    check(app.running(), "app becomes running");
    check(runtime.state("test.app") == cyberdeck_apps::app_state::running, "started app exposes running state");

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
    check(runtime.resources("dependent.app", resources, resource_count), "manifest resources are exposed");
    check(resource_count == 1 && resources[0] == "storage", "resource list is bounded");
    check(runtime.restart_application("dependent.app"), "running app can be restarted");

    auto unknown = runtime.execute_line("app info missing");
    check(unknown.status == cyberdeck_apps::result_status::rejected, "unknown app is rejected");
    check(runtime.execute_line("not-an-app").status == cyberdeck_apps::result_status::not_handled,
          "unregistered command is left for passthrough");
    check(runtime.execute_line("wifi").status == cyberdeck_apps::result_status::not_handled,
          "service command names remain available to the shell parser");
    check(runtime.failure_reason("missing") == "application not found", "missing app has a diagnostic");

    cyberdeck_apps::runtime type_policy_runtime;
    non_foreground_command_app service("test.service", "service-command", cyberdeck_apps::app_type::service);
    non_foreground_command_app background("test.background", "background-command",
                                          cyberdeck_apps::app_type::background);
    check(type_policy_runtime.register_application(service), "service app registers");
    check(type_policy_runtime.register_application(background), "background app registers");
    check(type_policy_runtime.execute_line("service-command").status == cyberdeck_apps::result_status::rejected,
          "stopped service command is rejected without auto-start");
    check(service.start_calls == 0 && !service.running(), "service command never lazy-starts");
    check(type_policy_runtime.execute_line("background-command").status == cyberdeck_apps::result_status::rejected,
          "stopped background command is rejected without auto-start");
    check(background.start_calls == 0 && !background.running(), "background command never lazy-starts");

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
    check(slow_runtime.failure_reason("slow.app") == "start hook timeout", "budget overrun is kept as diagnostic");
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
    check(!failing_runtime.start_application("failing.dependent"), "dependent start fails when dependency fails");
    check(failing_runtime.state("failing.app") == cyberdeck_apps::app_state::failed,
          "failed hook is reported as failed");
    check(failing_runtime.failure_reason("failing.app") == "start hook failed", "failed hook exposes reason");
    check(failing_runtime.state("failing.dependent") == cyberdeck_apps::app_state::failed,
          "dependent of a failed app is failed");
    check(failing_dependent.running() == false, "dependent hook is not invoked");
    check(failing_runtime.failure_reason("failing.dependent").find("failing.app") != std::string_view::npos,
          "dependent diagnostic names the missing dependency");
    const auto failure_info = failing_runtime.execute_line("app info failing.app");
    check(failure_info.output.find("failure: start hook failed\n") != std::string::npos,
          "app info exposes the last failure reason");

    // TEST-GRANT-08: default-invalid behavior and the exact declared resource mask.
    cyberdeck_apps::grant invalid_grant;
    check(!invalid_grant.valid() && !invalid_grant.allows(cyberdeck_apps::resource::display),
          "default grant is invalid and denies resources");
    cyberdeck_apps::runtime grant_runtime;
    grant_test_app grant_app;
    check(grant_runtime.register_application(grant_app), "grant app registers");
    check(!grant_runtime.app_grant("missing").valid() && !grant_app.app_grant().valid(),
          "unstarted and unknown applications have no grant");
    check(grant_runtime.start_application("grant.app"), "grant app starts");
    const auto first_grant = grant_runtime.app_grant("grant.app");
    const auto app_snapshot = grant_app.app_grant();
    constexpr cyberdeck_apps::resource all_resources[] = {
        cyberdeck_apps::resource::display,    cyberdeck_apps::resource::input,     cyberdeck_apps::resource::storage,
        cyberdeck_apps::resource::network,    cyberdeck_apps::resource::ble,       cyberdeck_apps::resource::serial,
        cyberdeck_apps::resource::screenshot, cyberdeck_apps::resource::event_log, cyberdeck_apps::resource::clock,
        cyberdeck_apps::resource::battery,
    };
    bool exact_mask = first_grant.valid();
    for (const auto resource : all_resources) {
        const bool expected =
            resource == cyberdeck_apps::resource::display || resource == cyberdeck_apps::resource::input;
        exact_mask = exact_mask && first_grant.allows(resource) == expected;
    }
    check(exact_mask && first_grant.allows("input") && !first_grant.allows("not-a-resource"),
          "running grant preserves exactly the declared resource mask");
    check(app_snapshot.valid() && app_snapshot.allows(cyberdeck_apps::resource::input),
          "application returns a usable by-value grant snapshot");

    // TEST-GRANT-02: teardown observes revocation before its bounded completion barrier.
    std::mutex barrier_mutex;
    std::condition_variable barrier_cv;
    bool teardown_observed = false;
    bool teardown_grant_valid = true;
    grant_app.on_teardown = [&](cyberdeck_apps::grant observed) {
        teardown_grant_valid = observed.valid();
        {
            std::lock_guard<std::mutex> lock(barrier_mutex);
            teardown_observed = true;
        }
        barrier_cv.notify_one();
    };
    bool stop_succeeded = false;
    std::thread stopper([&] { stop_succeeded = grant_runtime.stop_application("grant.app"); });
    {
        std::unique_lock<std::mutex> lock(barrier_mutex);
        check(wait_for(barrier_cv, lock, [&] { return teardown_observed; }), "teardown barrier completes within bound");
    }
    stopper.join();
    check(stop_succeeded, "grant app stops");
    // TEST-GRANT-01: stop revokes the live grant and all retained copies.
    check(!teardown_grant_valid && !first_grant.valid() && !app_snapshot.valid() &&
              !first_grant.allows(cyberdeck_apps::resource::display),
          "grant revocation linearizes before teardown and invalidates saved copies");

    // TEST-GRANT-03: grants captured by failed init/start hooks are invalidated.
    grant_test_app init_failure("grant.init-failure");
    init_failure.capture_during_init = true;
    init_failure.init_ok = false;
    cyberdeck_apps::runtime init_failure_runtime;
    check(init_failure_runtime.register_application(init_failure), "init-failure app registers");
    check(!init_failure_runtime.start_application("grant.init-failure") && !init_failure.captured.valid() &&
              !init_failure.app_grant().valid(),
          "init failure invalidates grant published during init");

    grant_test_app start_failure("grant.start-failure");
    start_failure.capture_during_start = true;
    start_failure.start_ok = false;
    cyberdeck_apps::runtime start_failure_runtime;
    check(start_failure_runtime.register_application(start_failure), "start-failure app registers");
    check(!start_failure_runtime.start_application("grant.start-failure") && !start_failure.captured.valid() &&
              !start_failure.app_grant().valid(),
          "start failure invalidates grant published during start");

    // TEST-GRANT-04: restart publishes a fresh generation without reviving stale copies.
    check(grant_runtime.start_application("grant.app"), "grant app starts after stop");
    const auto second_grant = grant_app.app_grant();
    check(second_grant.valid() && !first_grant.valid() && second_grant.allows(cyberdeck_apps::resource::display),
          "restart publishes a fresh grant without reviving stale generation");
    check(grant_runtime.stop_application("grant.app"), "restarted grant app stops");
    check(!second_grant.valid(), "second generation is invalid after stop");

    // TEST-GRANT-05: application access returns a usable by-value snapshot.
    // TEST-GRANT-06/07: concurrent reads cross repeated revocation with bounded barriers.
    cyberdeck_apps::runtime concurrent_runtime;
    grant_test_app concurrent_app("grant.concurrent");
    check(concurrent_runtime.register_application(concurrent_app), "concurrent grant app registers");
    std::atomic<bool> reader_done{false};
    std::atomic<bool> reader_failed{false};
    std::mutex reader_mutex;
    std::condition_variable reader_cv;
    bool reader_started = false;
    std::thread reader([&] {
        {
            std::lock_guard<std::mutex> lock(reader_mutex);
            reader_started = true;
        }
        reader_cv.notify_one();
        while (!reader_done.load(std::memory_order_acquire)) {
            const auto copy = concurrent_app.app_grant();
            (void)copy.valid();
            (void)copy.allows(cyberdeck_apps::resource::display);
            (void)concurrent_runtime.app_grant("grant.concurrent").allows("input");
        }
    });
    {
        std::unique_lock<std::mutex> lock(reader_mutex);
        check(wait_for(reader_cv, lock, [&] { return reader_started; }), "concurrent reader reaches start barrier");
    }
    for (int iteration = 0; iteration < 150; ++iteration) {
        if (!concurrent_runtime.start_application("grant.concurrent"))
            reader_failed = true;
        const auto stale = concurrent_app.app_grant();
        if (!concurrent_runtime.stop_application("grant.concurrent"))
            reader_failed = true;
        if (stale.valid() || stale.allows(cyberdeck_apps::resource::display))
            reader_failed = true;
    }
    reader_done.store(true, std::memory_order_release);
    reader.join();
    check(!reader_failed.load(), "concurrent grant reads remain safe and stale copies deny after stop");

    // TEST-RT-VALIDATION-01..06: every invalid manifest invariant is rejected
    // by the real registration validation and leaves the registry untouched.
    cyberdeck_apps::runtime validation_runtime;
    configurable_app resource_overflow("invalid.resource-overflow");
    resource_overflow.manifest_.resource_count = cyberdeck_apps::k_max_resources + 1;
    check(!validation_runtime.register_application(resource_overflow),
          "resource_count above the manifest capacity is rejected");
    check(validation_runtime.size() == 0, "resource overflow leaves the registry empty");

    configurable_app resource_unknown("invalid.resource-unknown");
    resource_unknown.manifest_.resources[0] = "not-a-resource";
    resource_unknown.manifest_.resource_count = 1;
    check(!validation_runtime.register_application(resource_unknown), "unknown resource name is rejected");
    check(validation_runtime.size() == 0, "unknown resource leaves the registry empty");

    configurable_app resource_duplicate("invalid.resource-duplicate");
    resource_duplicate.manifest_.resources[0] = "display";
    resource_duplicate.manifest_.resources[1] = "display";
    resource_duplicate.manifest_.resource_count = 2;
    check(!validation_runtime.register_application(resource_duplicate), "duplicated resource is rejected");
    check(validation_runtime.size() == 0, "duplicated resource leaves the registry empty");

    configurable_app command_overflow("invalid.command-overflow");
    command_overflow.manifest_.command_count = cyberdeck_apps::k_max_commands + 1;
    check(!validation_runtime.register_application(command_overflow),
          "command_count above the manifest capacity is rejected");

    configurable_app command_empty("invalid.command-empty");
    command_empty.manifest_.commands[0] = "";
    command_empty.manifest_.command_count = 1;
    check(!validation_runtime.register_application(command_empty), "empty declared command is rejected");

    configurable_app capability_overflow("invalid.capability-overflow");
    capability_overflow.manifest_.capability_count = cyberdeck_apps::k_max_capabilities + 1;
    check(!validation_runtime.register_application(capability_overflow),
          "capability_count above the manifest capacity is rejected");
    check(validation_runtime.size() == 0, "all invalid manifests leave the registry empty");

    configurable_app valid_manifest("validation.valid");
    check(validation_runtime.register_application(valid_manifest),
          "a valid manifest still registers after the rejected ones");

    // TEST-RT-REGISTRY-01: the registry is bounded at k_max_applications and
    // rejects anything beyond it without leaking an entry.
    cyberdeck_apps::runtime full_runtime;
    std::vector<std::unique_ptr<configurable_app>> full_apps;
    for (std::size_t index = 0; index < cyberdeck_apps::k_max_applications; ++index) {
        auto app = std::make_unique<configurable_app>(k_registry_full_ids[index]);
        check(full_runtime.register_application(*app), "registry accepts applications up to its capacity");
        full_apps.push_back(std::move(app));
    }
    check(full_runtime.size() == cyberdeck_apps::k_max_applications, "registry reports its exact capacity");
    configurable_app registry_overflow("full.overflow");
    check(!full_runtime.register_application(registry_overflow), "a full registry rejects registration");
    check(full_runtime.find("full.overflow") == nullptr, "rejected registration leaves no entry behind");
    check(full_runtime.at(0) != nullptr, "at() exposes the first registration");
    check(full_runtime.at(full_runtime.size()) == nullptr, "at() rejects the end index");
    check(full_runtime.state("not.registered") == cyberdeck_apps::app_state::failed,
          "unknown applications report the failed state");
    check(!full_runtime.restart_application("not.registered"), "restart of an unknown application fails");
    check(!full_runtime.stop_application("not.registered"), "stop of an unknown application fails");

    // TEST-RT-RECOVERY-01..03: restored diagnostics are bounded and only apply
    // to registered applications.
    check(!full_runtime.restore_failure_reason("not.registered", "reason"),
          "diagnostics cannot be restored for unknown ids");
    check(!full_runtime.restore_failure_reason(k_registry_full_ids[0], ""), "empty diagnostics are rejected");
    check(!full_runtime.restore_failure_reason(k_registry_full_ids[0], std::string(128, 'x')),
          "diagnostics longer than 127 bytes are rejected");
    check(full_runtime.restore_failure_reason(k_registry_full_ids[0], "restored diagnostic") &&
              full_runtime.failure_reason(k_registry_full_ids[0]) == "restored diagnostic",
          "bounded diagnostics round-trip through the runtime");

    // TEST-RT-START-01..03: dependency declarations are validated at start
    // time, including missing dependencies and cycles.
    cyberdeck_apps::runtime invalid_dependency_runtime;
    configurable_app invalid_dependency("invalid.dependency");
    invalid_dependency.manifest_.dependency_count = cyberdeck_apps::k_max_dependencies + 1;
    check(invalid_dependency_runtime.register_application(invalid_dependency),
          "dependency overflow is a start-time property, not a registration error");
    check(!invalid_dependency_runtime.start_application("invalid.dependency"),
          "dependency_count above capacity fails the start");
    check(invalid_dependency_runtime.failure_reason("invalid.dependency") == "invalid dependency declaration",
          "invalid dependency declaration is reported");

    cyberdeck_apps::runtime missing_dependency_runtime;
    configurable_app missing_dependency("missing.dependency");
    missing_dependency.manifest_.dependencies[0] = "not.registered.app";
    missing_dependency.manifest_.dependency_count = 1;
    check(missing_dependency_runtime.register_application(missing_dependency), "missing-dependency app registers");
    check(!missing_dependency_runtime.start_application("missing.dependency"),
          "starting an app whose dependency is unregistered fails");
    check(missing_dependency_runtime.failure_reason("missing.dependency") ==
              "dependency unavailable: not.registered.app",
          "the unavailable dependency is named in the diagnostic");

    cyberdeck_apps::runtime cycle_runtime;
    configurable_app cycle_a("cycle.a");
    cycle_a.manifest_.dependencies[0] = "cycle.b";
    cycle_a.manifest_.dependency_count = 1;
    configurable_app cycle_b("cycle.b");
    cycle_b.manifest_.dependencies[0] = "cycle.a";
    cycle_b.manifest_.dependency_count = 1;
    check(cycle_runtime.register_application(cycle_a) && cycle_runtime.register_application(cycle_b),
          "cyclic manifests register; cycles are a start-time property");
    check(!cycle_runtime.start_application("cycle.a"), "a dependency cycle fails the start");
    check(cycle_runtime.state("cycle.a") == cyberdeck_apps::app_state::failed &&
              cycle_runtime.state("cycle.b") == cyberdeck_apps::app_state::failed,
          "both sides of the cycle end up failed");

    // TEST-RT-CONSOLE-01..05: the shell must never stop the console owner,
    // directly or through its dependent chain, while the supervisor lifecycle
    // API keeps working.
    cyberdeck_apps::runtime console_runtime;
    configurable_app console_base("console.base");
    configurable_app console_cycle1("console.cycle1");
    console_cycle1.manifest_.dependencies[0] = "console.base";
    console_cycle1.manifest_.dependencies[1] = "console.cycle2";
    console_cycle1.manifest_.dependency_count = 2;
    configurable_app console_cycle2("console.cycle2");
    console_cycle2.manifest_.dependencies[0] = "console.cycle1";
    console_cycle2.manifest_.dependency_count = 1;
    configurable_app console_other("console.other");
    console_other.manifest_.dependencies[0] = "console.base";
    console_other.manifest_.dependency_count = 1;
    configurable_app console_owner("console.owner");
    console_owner.manifest_.owns_console = true;
    console_owner.manifest_.dependencies[0] = "console.base";
    console_owner.manifest_.dependency_count = 1;
    check(console_runtime.register_application(console_base) && console_runtime.register_application(console_cycle1) &&
              console_runtime.register_application(console_cycle2) && console_runtime.register_application(console_other) &&
              console_runtime.register_application(console_owner),
          "console dependency graph registers");
    check(console_runtime.start_application("console.other") && console_runtime.start_application("console.owner"),
          "console graph starts through the lifecycle API");
    check(console_runtime.stops_console_owner("console.base"),
          "stopping a transitive dependency of the console owner is detected");
    check(!console_runtime.stops_console_owner("console.cycle1"),
          "a cyclic dependency chain that cannot reach the owner is not flagged");
    check(!console_runtime.stops_console_owner("console.other"),
          "stopping a non-console dependent is not flagged");
    check(!console_runtime.stops_console_owner("not.registered"), "unknown ids never own the console");
    const auto refused_console_stop = console_runtime.execute_line("app stop console.base");
    check(refused_console_stop.status == cyberdeck_apps::result_status::rejected &&
              refused_console_stop.output.find("stop would remove the console") != std::string::npos,
          "the shell refuses a stop that would remove the console");
    check(console_runtime.state("console.owner") == cyberdeck_apps::app_state::running &&
              console_runtime.state("console.other") == cyberdeck_apps::app_state::running,
          "the refused stop leaves the whole console graph running");
    check(console_runtime.stop_application("console.base"),
          "the supervisor lifecycle API still cascades the stop");
    check(console_runtime.state("console.owner") == cyberdeck_apps::app_state::registered &&
              console_runtime.state("console.other") == cyberdeck_apps::app_state::registered &&
              console_runtime.state("console.base") == cyberdeck_apps::app_state::registered,
          "the supervisor cascade stops dependents before the target");

    // TEST-RT-COMMAND-01: a foreground command whose start hook fails is
    // rejected at its dispatch boundary, never half-started.
    cyberdeck_apps::runtime command_failure_runtime;
    configurable_app command_failure("command.failure");
    command_failure.manifest_.command = "cmdfail";
    command_failure.manifest_.type = cyberdeck_apps::app_type::foreground;
    command_failure.start_ok = false;
    check(command_failure_runtime.register_application(command_failure), "failing command app registers");
    const auto command_failure_result = command_failure_runtime.execute_line("cmdfail");
    check(command_failure_result.status == cyberdeck_apps::result_status::rejected &&
              command_failure_result.output.find("command application failed to start") != std::string::npos,
          "a foreground command that cannot start is rejected");
    check(command_failure_runtime.state("command.failure") == cyberdeck_apps::app_state::failed,
          "failed command start is reported as failed");

    // TEST-RT-STOP-01..02: a failing teardown is sticky and blocks restart.
    cyberdeck_apps::runtime stop_failure_runtime;
    configurable_app stop_failure("stop.failure");
    stop_failure.teardown_ok = false;
    check(stop_failure_runtime.register_application(stop_failure), "stop-failure app registers");
    check(stop_failure_runtime.start_application("stop.failure"), "stop-failure app starts");
    check(!stop_failure_runtime.restart_application("stop.failure"),
          "restart cannot proceed when the stop hook fails");
    check(stop_failure_runtime.state("stop.failure") == cyberdeck_apps::app_state::failed,
          "failed stop hook is reported as failed");
    check(stop_failure_runtime.failure_reason("stop.failure") == "stop hook failed",
          "failed stop hook exposes its diagnostic");

    // TEST-RT-TOKENIZER-01..07: bounded dispatch input and usage fallbacks.
    cyberdeck_apps::runtime tokenizer_runtime;
    check(tokenizer_runtime.execute_line(std::string(257, 'a')).status ==
              cyberdeck_apps::result_status::not_handled,
          "lines over 256 bytes are never dispatched");
    std::string nine_tokens;
    for (int index = 0; index < 9; ++index)
        nine_tokens += "word ";
    check(tokenizer_runtime.execute_line(nine_tokens).status == cyberdeck_apps::result_status::not_handled,
          "more than eight tokens are never dispatched");
    check(tokenizer_runtime.execute_line(" \t  ").status == cyberdeck_apps::result_status::not_handled,
          "whitespace-only lines are never dispatched");
    check(tokenizer_runtime.execute_line("app list").output == "no applications registered\n",
          "an empty registry still answers app list");
    check(tokenizer_runtime.execute_line("app").status == cyberdeck_apps::result_status::rejected,
          "bare app is rejected with usage");
    check(tokenizer_runtime.execute_line("app foo").status == cyberdeck_apps::result_status::rejected,
          "unknown app subcommands are rejected with usage");
    check(tokenizer_runtime.execute_line("app info").status == cyberdeck_apps::result_status::rejected,
          "app info without an id is rejected with usage");

    // TEST-RT-LOGGER-01..03 / TEST-RT-SUPPLIER-01..03: injected collaborators
    // propagate to registered applications, lifecycle events reach the logger,
    // and start_all/stop_all attempt every application.
    cyberdeck_apps::runtime supervisor_runtime;
    configurable_app supervisor_a("supervisor.a");
    configurable_app supervisor_b("supervisor.b");
    configurable_app supervisor_failing("supervisor.failing");
    supervisor_failing.start_ok = false;
    check(supervisor_runtime.register_application(supervisor_a) &&
              supervisor_runtime.register_application(supervisor_b) &&
              supervisor_runtime.register_application(supervisor_failing),
          "supervisor fixture registers");
    recording_logger lifecycle_logger;
    cyberdeck_sd_io::coordinator sd_coordinator;
    supervisor_runtime.set_logger(&lifecycle_logger);
    supervisor_runtime.set_sd_io(&sd_coordinator);
    check(supervisor_a.app_logger() == &lifecycle_logger && supervisor_b.app_logger() == &lifecycle_logger &&
              supervisor_failing.app_logger() == &lifecycle_logger,
          "the logger propagates to already registered applications");
    check(supervisor_a.app_sd_io() == &sd_coordinator && supervisor_b.app_sd_io() == &sd_coordinator,
          "the sd_io coordinator propagates to already registered applications");
    check(supervisor_runtime.app_logger() == &lifecycle_logger, "the runtime exposes the injected logger");
    check(!supervisor_runtime.start_all(), "start_all reports failure when one start hook fails");
    check(supervisor_a.running() && supervisor_b.running(),
          "start_all still attempts every application after a failure");
    bool saw_start_event = false;
    for (const auto &payload : lifecycle_logger.payloads)
        saw_start_event = saw_start_event || payload.find("app=supervisor.a event=start") != std::string::npos;
    check(saw_start_event, "start lifecycle events reach the injected logger with the app id");
    check(supervisor_runtime.stop_all(), "stop_all stops every application it can");
    check(!supervisor_a.running() && !supervisor_b.running(), "stop_all leaves no application running");

    if (failures == 0) {
        std::printf("PASS: app runtime (%d checks)\n", checks);
        return 0;
    }
    std::printf("FAIL: %d of %d app runtime checks failed\n", failures, checks);
    return 1;
}
