#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

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
    std::uint32_t lifecycle_timeout_ms = 1000;
    std::string_view api_version = "1";
    app_type type = app_type::service;
    std::array<std::string_view, k_max_capabilities> capabilities{};
    std::size_t capability_count = 0;
    std::uint32_t stack_bytes = 0;
    std::uint32_t queue_depth = 0;
    std::array<std::string_view, k_max_commands> commands{};
    std::size_t command_count = 0;
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
    application *find(std::string_view id);
    const application *find(std::string_view id) const;
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

    bool start_index(std::size_t index, std::array<bool, k_max_applications> &visiting);
    bool stop_index(std::size_t index);
    std::size_t index_of(std::string_view id) const;
};

} // namespace cyberdeck_apps

namespace cyberdeck_apps {

/* Singleton used by app_main and the shell UI composition. */
runtime &global_runtime();

} // namespace cyberdeck_apps
