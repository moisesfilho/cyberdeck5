/* Host coverage for the compiled-in demo application (TEST-COV-012..026).
 *
 * Links the real production demo application against the real app runtime and
 * a LOCAL runtime instance, so nothing here touches ESP-IDF, LVGL, FreeRTOS,
 * NVS, the serial bridge or hardware.  Each scenario owns its own application
 * instance so the order of execution cannot hide a state transition, and every
 * call whose result is asserted is sequenced before the assertion.
 */
#include "apps/demo/cyberdeck_demo_app.h"
#include "apps/runtime/cyberdeck_app_runtime.h"

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

/* Same reporting as check(), plus the input that produced the failure, so a
 * rejected command/args variant names itself in the log. */
void check_named(bool condition, const char *variant, const char *message)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s [%s]\n", message, variant);
    }
}

constexpr char k_id[] = "cyberdeck.demo";
constexpr char k_running_output[] = "demo: compiled-in application is running\n";
constexpr char k_not_running_output[] = "demo: application is not running\n";

struct command_variant {
    const char *command;
    const char *args;
};

/* TEST-COV-012: the manifest is a fixed, fully declared contract. */
void test_manifest()
{
    cyberdeck_apps::demo_application app;
    const cyberdeck_apps::manifest &item = app.get_manifest();

    check(item.id == k_id, "manifest exposes the demo identifier");
    check(item.name == "Demo application", "manifest exposes the human name");
    check(item.version == "0.1.0", "manifest exposes the version");
    check(item.description == "Proof application for the compiled-in runtime",
          "manifest exposes the description");
    check(item.command == "demo", "manifest exposes the shell command");
    check(item.dependency_count == 0, "manifest declares no dependency");
    check(item.resource_count == 0, "manifest declares no resource");
    check(item.lifecycle_timeout_ms == 1000, "manifest exposes the lifecycle budget");
    check(item.api_version == "1", "manifest exposes the API version");
    check(item.type == cyberdeck_apps::app_type::demo, "manifest declares the demo type");
    check(std::string_view{cyberdeck_apps::app_type_name(item.type)} == "demo",
          "the demo type resolves to the demo name");
    check(item.capability_count == 1 && item.capabilities[0] == "demo",
          "manifest declares exactly the demo capability");
    check(item.stack_bytes == 2048, "manifest exposes the requested stack");
    check(item.queue_depth == 2, "manifest exposes the requested queue depth");
    check(item.command_count == 1 && item.commands[0] == "demo",
          "manifest declares exactly the demo command");
    check(!item.owns_console, "the demo application does not own the user console");
    /* Two calls must answer the same definition, not two independent copies. */
    check(&app.get_manifest() == &item, "manifest is a stable shared definition");
}

/* TEST-COV-013: a first start succeeds and flips the running state. */
void test_start_succeeds_once()
{
    cyberdeck_apps::demo_application app;
    check(!app.running(), "a fresh demo application is not running");
    check(app.init(), "the inherited init hook succeeds");
    const bool started = app.start();
    check(started, "the first raw start succeeds");
    check(app.running(), "a started demo application reports running");
}

/* TEST-COV-014: the raw start hook is idempotent and refuses a second call. */
void test_start_is_idempotent()
{
    cyberdeck_apps::demo_application app;
    check(app.start(), "the demo application starts");
    const bool again = app.start();
    check(!again, "a second raw start is refused");
    check(app.running(), "the refused start keeps the application running");
}

/* TEST-COV-015: stop before start is refused and changes nothing. */
void test_stop_without_start()
{
    cyberdeck_apps::demo_application app;
    const bool stopped = app.stop();
    check(!stopped, "stop before start is refused");
    check(!app.running(), "the refused stop keeps the application stopped");
    const auto out = app.execute("demo", "");
    check(out.status == cyberdeck_apps::result_status::rejected,
          "execute after a refused stop is still rejected");
    check(out.output == k_not_running_output,
          "execute after a refused stop reports that the app is not running");
}

/* TEST-COV-016: stop clears the running state and the cycle closes. */
void test_stop_cycle()
{
    cyberdeck_apps::demo_application app;
    check(app.start(), "the demo application starts");
    const bool stopped = app.stop();
    check(stopped, "stop after start succeeds");
    check(!app.running(), "a stopped demo application reports not running");
    const bool again = app.stop();
    check(!again, "a second raw stop is refused");
    check(!app.running(), "the refused stop keeps the application stopped");
}

/* TEST-COV-017: execute before start is rejected with the exact diagnostic. */
void test_execute_before_start()
{
    cyberdeck_apps::demo_application app;
    const command_variant variants[] = {
        {"demo", ""},
        {"demo", "--verbose"},
        {"not-a-command", "args are irrelevant"},
    };
    for (const auto &variant : variants) {
        const auto out = app.execute(variant.command, variant.args);
        check_named(out.status == cyberdeck_apps::result_status::rejected, variant.command,
                    "execute before start is rejected for every command/args variant");
        check_named(out.output == k_not_running_output, variant.command,
                    "execute before start reports the exact not-running diagnostic");
    }
}

/* TEST-COV-018: once running, execute is handled with the exact diagnostic
 * regardless of the command or the arguments. */
void test_execute_after_start()
{
    cyberdeck_apps::demo_application app;
    check(app.start(), "the demo application starts");
    const command_variant variants[] = {
        {"demo", ""},
        {"demo", "--fast"},
        {"demo", "several   words   here"},
        {"", ""},
        {"not-a-command", "still handled"},
        {"demo", "multi\nline\n"},
    };
    for (const auto &variant : variants) {
        const auto out = app.execute(variant.command, variant.args);
        check_named(out.status == cyberdeck_apps::result_status::handled, variant.command,
                    "execute after start is handled for every command/args variant");
        check_named(out.output == k_running_output, variant.command,
                    "execute after start reports the exact running diagnostic");
    }
}

/* TEST-COV-019: the full start/execute/stop cycle, sequenced call by call. */
void test_full_cycle()
{
    cyberdeck_apps::demo_application app;
    const auto before = app.execute("demo", "");
    check(before.status == cyberdeck_apps::result_status::rejected, "cycle starts rejected");

    const bool started = app.start();
    check(started, "cycle start succeeds");
    check(!app.start(), "cycle second start is refused");

    const auto running = app.execute("demo", "payload");
    check(running.status == cyberdeck_apps::result_status::handled, "cycle execute is handled");
    check(running.output == k_running_output, "cycle execute reports the running diagnostic");

    const bool stopped = app.stop();
    check(stopped, "cycle stop succeeds");
    check(!app.running(), "cycle leaves the application stopped");

    const auto after = app.execute("demo", "");
    check(after.status == cyberdeck_apps::result_status::rejected, "cycle ends rejected");
    check(after.output == k_not_running_output, "cycle end reports the not-running diagnostic");
}

/* TEST-COV-020: two instances keep independent state; a shared (static)
 * running flag would make the first start visible in the second instance. */
void test_instances_are_independent()
{
    cyberdeck_apps::demo_application first;
    cyberdeck_apps::demo_application second;

    check(first.start(), "the first instance starts");
    check(!second.running(), "the second instance is unaffected by the first start");
    const auto second_before_start = second.execute("demo", "");
    check(second_before_start.status == cyberdeck_apps::result_status::rejected,
          "the second instance still rejects execute");
    check(second_before_start.output == k_not_running_output,
          "the second instance reports the not-running diagnostic");

    check(second.start(), "the second instance starts on its own");
    check(first.stop(), "the first instance stops");
    check(!first.running(), "the first instance is stopped");
    check(second.running(), "stopping the first instance leaves the second running");
    const auto second_running = second.execute("demo", "");
    check(second_running.status == cyberdeck_apps::result_status::handled,
          "the second instance still handles execute");
    check(second_running.output == k_running_output,
          "the second instance reports the running diagnostic");
}

/* TEST-COV-021: registration in a local runtime, including the duplicate. */
void test_runtime_registration()
{
    cyberdeck_apps::runtime local;
    cyberdeck_apps::demo_application app;

    const bool registered = local.register_application(app);
    check(registered, "the demo application registers in a local runtime");
    const bool duplicate = local.register_application(app);
    check(!duplicate, "a duplicate registration is refused");
    check(local.size() == 1, "the refused duplicate does not grow the registry");
    check(local.find(k_id) == &app, "the registered application is reachable by id");
    check(local.find("missing.app") == nullptr, "an unknown id resolves to nothing");
}

/* TEST-COV-022: dispatch before start is rejected and an unknown command is
 * left for the shell parser. */
void test_runtime_dispatch_before_start()
{
    cyberdeck_apps::runtime local;
    cyberdeck_apps::demo_application app;
    check(local.register_application(app), "the demo application registers");

    const auto dispatched = local.execute_line("demo");
    check(dispatched.status == cyberdeck_apps::result_status::rejected,
          "the runtime dispatch before start is rejected");
    check(dispatched.output == k_not_running_output,
          "the runtime dispatch reports the exact not-running diagnostic");
    check(local.state(k_id) == cyberdeck_apps::app_state::registered,
          "a rejected dispatch leaves the application registered");

    const auto unknown = local.execute_line("not-a-demo-command");
    check(unknown.status == cyberdeck_apps::result_status::not_handled,
          "an unregistered command is left for the shell parser");
}

/* TEST-COV-023: runtime start is a lifecycle transition; a second start of an
 * already running application reports success WITHOUT a second hook call. */
void test_runtime_start()
{
    cyberdeck_apps::runtime local;
    cyberdeck_apps::demo_application app;
    check(local.register_application(app), "the demo application registers");

    const bool started = local.start_application(k_id);
    check(started, "the runtime starts the demo application");
    check(app.running(), "the started application reports running");
    check(local.state(k_id) == cyberdeck_apps::app_state::running,
          "the runtime exposes the running state");

    const bool started_again = local.start_application(k_id);
    check(started_again, "starting an already running application still reports success");
    /* The raw hook refuses a second start, so a false here proves the runtime
     * did not invoke the hook again. */
    check(!app.start(), "the runtime start hook ran exactly once");
    check(local.state(k_id) == cyberdeck_apps::app_state::running,
          "the repeated start keeps the running state");
    check(local.failure_reason(k_id).empty(), "a successful start keeps no diagnostic");
}

/* TEST-COV-024: dispatch after start is handled with the exact diagnostic for
 * a bare command and for a command with arguments. */
void test_runtime_dispatch_after_start()
{
    cyberdeck_apps::runtime local;
    cyberdeck_apps::demo_application app;
    check(local.register_application(app), "the demo application registers");
    check(local.start_application(k_id), "the runtime starts the demo application");

    const auto bare = local.execute_line("demo");
    check(bare.status == cyberdeck_apps::result_status::handled,
          "the runtime dispatch of the registered command is handled");
    check(bare.output == k_running_output,
          "the runtime dispatch reports the exact running diagnostic");

    const auto with_args = local.execute_line("demo now please");
    check(with_args.status == cyberdeck_apps::result_status::handled,
          "the runtime dispatch with arguments is handled");
    check(with_args.output == k_running_output,
          "the runtime dispatch with arguments reports the exact running diagnostic");

    const auto tabbed = local.execute_line("\tdemo\targ");
    check(tabbed.status == cyberdeck_apps::result_status::handled,
          "the runtime dispatch tolerates tab separators");
    check(tabbed.output == k_running_output,
          "the runtime dispatch with tab separators reports the exact running diagnostic");
}

/* TEST-COV-025: the manifest is observable through the runtime commands. */
void test_runtime_app_info()
{
    cyberdeck_apps::runtime local;
    cyberdeck_apps::demo_application app;
    check(local.register_application(app), "the demo application registers");

    const auto registered_info = local.execute_line("app info cyberdeck.demo");
    check(registered_info.status == cyberdeck_apps::result_status::handled,
          "app info is handled while registered");
    const auto running = local.start_application(k_id);
    check(running, "the runtime starts the demo application");

    const auto info = local.execute_line("app info cyberdeck.demo");
    check(info.status == cyberdeck_apps::result_status::handled, "app info is handled while running");
    check(info.output.find("id: cyberdeck.demo\n") != std::string::npos, "app info exposes the id");
    check(info.output.find("name: Demo application\n") != std::string::npos,
          "app info exposes the name");
    check(info.output.find("version: 0.1.0\n") != std::string::npos,
          "app info exposes the version");
    check(info.output.find("api_version: 1\n") != std::string::npos,
          "app info exposes the API version");
    check(info.output.find("type: demo\n") != std::string::npos, "app info exposes the demo type");
    check(info.output.find("description: Proof application for the compiled-in runtime\n") !=
              std::string::npos,
          "app info exposes the description");
    check(info.output.find("command: demo\n") != std::string::npos, "app info exposes the command");
    check(info.output.find("stack_bytes: 2048\n") != std::string::npos,
          "app info exposes the requested stack");
    check(info.output.find("queue_depth: 2\n") != std::string::npos,
          "app info exposes the requested queue depth");
    check(info.output.find("dependencies:\ncapabilities: demo\n") != std::string::npos,
          "app info exposes an empty dependency list and the demo capability");
    check(info.output.find("commands: demo\n") != std::string::npos, "app info exposes the commands");
    check(info.output.find("state: running\n") != std::string::npos,
          "app info exposes the running state");

    const auto list = local.execute_line("app list");
    check(list.status == cyberdeck_apps::result_status::handled, "app list is handled");
    check(list.output == "cyberdeck.demo 0.1.0 running\n", "app list is deterministic");
}

/* TEST-COV-026: stop/restart cycle through the runtime and through the console
 * command, including the dispatch state after each transition. */
void test_runtime_stop_and_restart()
{
    cyberdeck_apps::runtime local;
    cyberdeck_apps::demo_application app;
    check(local.register_application(app), "the demo application registers");
    check(local.start_application(k_id), "the runtime starts the demo application");

    const bool stopped = local.stop_application(k_id);
    check(stopped, "the runtime stops the demo application");
    check(!app.running(), "the stopped application reports not running");
    check(local.state(k_id) == cyberdeck_apps::app_state::registered,
          "a stopped application returns to the registered state");
    const auto after_stop = local.execute_line("demo");
    check(after_stop.status == cyberdeck_apps::result_status::rejected,
          "dispatch after stop is rejected");
    check(after_stop.output == k_not_running_output,
          "dispatch after stop reports the exact not-running diagnostic");

    const bool restarted = local.restart_application(k_id);
    check(restarted, "the runtime restarts the demo application");
    check(app.running(), "the restarted application reports running");
    check(local.state(k_id) == cyberdeck_apps::app_state::running,
          "the restarted application exposes the running state");
    const auto after_restart = local.execute_line("demo");
    check(after_restart.status == cyberdeck_apps::result_status::handled,
          "dispatch after restart is handled");
    check(after_restart.output == k_running_output,
          "dispatch after restart reports the exact running diagnostic");

    /* The demo does not own the console, so the console command can stop it. */
    const auto console_stop = local.execute_line("app stop cyberdeck.demo");
    check(console_stop.status == cyberdeck_apps::result_status::handled,
          "app stop is handled for a non console owner");
    check(console_stop.output == "app stop cyberdeck.demo\n",
          "app stop names the stopped application");
    check(!app.running(), "app stop stops the demo application");
}

} // namespace

int main()
{
    test_manifest();
    test_start_succeeds_once();
    test_start_is_idempotent();
    test_stop_without_start();
    test_stop_cycle();
    test_execute_before_start();
    test_execute_after_start();
    test_full_cycle();
    test_instances_are_independent();
    test_runtime_registration();
    test_runtime_dispatch_before_start();
    test_runtime_start();
    test_runtime_dispatch_after_start();
    test_runtime_app_info();
    test_runtime_stop_and_restart();

    if (failures == 0) {
        std::printf("PASS: demo application (%d checks)\n", checks);
        return 0;
    }
    std::printf("FAIL: %d of %d demo application checks failed\n", failures, checks);
    return 1;
}