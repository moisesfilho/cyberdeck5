#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "apps/runtime/cyberdeck_app_logger.h"

namespace cyberdeck_apps {

constexpr std::size_t k_max_applications = 16;
constexpr std::size_t k_max_dependencies = 4;
constexpr std::size_t k_max_resources = 8;
constexpr std::size_t k_max_capabilities = 8;
constexpr std::size_t k_max_commands = 4;

enum class app_type {
    service,
    foreground,
    background,
    demo,
};

const char *app_type_name(app_type type);

enum class app_state {
    registered,
    starting,
    running,
    stopping,
    failed,
};

const char *app_state_name(app_state state);

struct manifest {
    std::string_view id;
    std::string_view name;
    std::string_view version;
    std::string_view description;
    std::string_view command;
    std::array<std::string_view, k_max_dependencies> dependencies{};
    std::size_t dependency_count = 0;
    std::array<std::string_view, k_max_resources> resources{};
    std::size_t resource_count = 0;
    /* Diagnostic budget measured after a synchronous hook returns. It does
     * not preempt the hook; effective execution limits belong to the
     * cooperative joins owned by the service. */
    std::uint32_t lifecycle_timeout_ms = 1000;
    std::string_view api_version = "1";
    app_type type = app_type::service;
    std::array<std::string_view, k_max_capabilities> capabilities{};
    std::size_t capability_count = 0;
    std::uint32_t stack_bytes = 0;
    std::uint32_t queue_depth = 0;
    std::array<std::string_view, k_max_commands> commands{};
    std::size_t command_count = 0;
    /* Declares that the application owns the only user console.  The commands
     * that reach the supervisor travel on that console, so the supervisor must
     * refuse to stop the console owner from inside itself: it would remove the
     * only path that could bring it back. */
    bool owns_console = false;
};

enum class result_status {
    not_handled,
    handled,
    rejected,
};

struct result {
    result_status status = result_status::not_handled;
    std::string output;
};

class application {
public:
    virtual ~application() = default;
    virtual const manifest &get_manifest() const = 0;
    virtual bool init() { return true; }
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool teardown() { return stop(); }
    virtual bool running() const = 0;
    virtual result execute(std::string_view command, std::string_view args) = 0;
    void set_logger(logger *value) { logger_ = value; }
    logger *app_logger() const { return logger_; }

private:
    logger *logger_ = nullptr;
};

class runtime {
public:
    bool register_application(application &app);
    result execute_line(std::string_view line);
    bool start_application(std::string_view id);
    bool stop_application(std::string_view id);
    bool restart_application(std::string_view id);
    bool start_all();
    bool stop_all();
    void set_logger(logger *value);
    logger *app_logger() const { return logger_; }
    application *find(std::string_view id);
    const application *find(std::string_view id) const;
    /* Bounded enumeration in registration order.  Returns nullptr when the
     * index is outside the registered range. */
    const application *at(std::size_t index) const;
    /* True when stopping `id` would also stop the application that owns the
     * console, either directly or through its dependent chain.  Stopping a
     * dependency cascades to dependents first, so the console owner can be
     * reached without naming it. */
    bool stops_console_owner(std::string_view id) const;
    app_state state(std::string_view id) const;
    std::string_view failure_reason(std::string_view id) const;
    bool resources(std::string_view id, std::array<std::string_view, k_max_resources> &out,
                   std::size_t &count) const;
    std::size_t size() const { return count_; }

private:
    std::array<application *, k_max_applications> applications_{};
    std::array<app_state, k_max_applications> states_{};
    std::array<std::string, k_max_applications> failures_{};
    std::size_t count_ = 0;
    logger *logger_ = nullptr;

    bool start_index(std::size_t index, std::array<bool, k_max_applications> &visiting);
    bool stop_index(std::size_t index);
    std::size_t index_of(std::string_view id) const;
    /* Bounded transitive search over the dependent tree of `index`. */
    bool cascade_stops_console(std::size_t index,
                               std::array<bool, k_max_applications> &visited) const;
};

} // namespace cyberdeck_apps

namespace cyberdeck_apps {

/* Singleton used by app_main and the shell UI composition. */
runtime &global_runtime();

} // namespace cyberdeck_apps
