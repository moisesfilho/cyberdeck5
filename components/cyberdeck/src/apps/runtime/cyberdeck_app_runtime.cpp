#include "apps/runtime/cyberdeck_app_runtime.h"

#include <utility>
#include <chrono>
#include <cstdio>

namespace cyberdeck_apps {
namespace {

constexpr std::size_t k_max_line_bytes = 256;
constexpr std::size_t k_max_tokens = 8;

constexpr std::uint16_t resource_bit(resource value)
{
    return static_cast<std::uint16_t>(1u << static_cast<unsigned>(value));
}

bool manifest_resources_valid(const manifest &item, std::uint16_t &mask)
{
    if (item.resource_count > item.resources.size()) return false;
    mask = 0;
    for (std::size_t i = 0; i < item.resource_count; ++i) {
        resource value{};
        if (!resource_from_name(item.resources[i], value)) return false;
        const std::uint16_t bit = resource_bit(value);
        if ((mask & bit) != 0) return false;
        mask = static_cast<std::uint16_t>(mask | bit);
    }
    return true;
}

bool split_words(std::string_view line, std::array<std::string_view, k_max_tokens> &words,
                 std::size_t &count)
{
    if (line.size() > k_max_line_bytes) return false;
    count = 0;
    std::size_t offset = 0;
    while (offset < line.size()) {
        while (offset < line.size() && (line[offset] == ' ' || line[offset] == '\t')) ++offset;
        if (offset == line.size()) break;
        const std::size_t begin = offset;
        while (offset < line.size() && line[offset] != ' ' && line[offset] != '\t') ++offset;
        if (count == words.size()) return false;
        words[count++] = line.substr(begin, offset - begin);
    }
    return true;
}

result handled(std::string output)
{
    return {result_status::handled, std::move(output)};
}

result rejected(const char *message)
{
    return {result_status::rejected, std::string(message) + "\n"};
}

constexpr std::size_t k_not_found = static_cast<std::size_t>(-1);

} // namespace

const char *resource_name(resource value)
{
    switch (value) {
    case resource::display: return "display";
    case resource::input: return "input";
    case resource::storage: return "storage";
    case resource::network: return "network";
    case resource::ble: return "ble";
    case resource::serial: return "serial";
    case resource::screenshot: return "screenshot";
    case resource::event_log: return "event_log";
    case resource::clock: return "clock";
    case resource::battery: return "battery";
    }
    return "";
}

bool resource_from_name(std::string_view name, resource &out)
{
    for (unsigned value = 0; value <= static_cast<unsigned>(resource::battery); ++value) {
        const auto candidate = static_cast<resource>(value);
        if (name == resource_name(candidate)) { out = candidate; return true; }
    }
    return false;
}

bool grant::valid() const
{
    return runtime_ != nullptr && runtime_->grant_is_valid(index_, generation_, resource_mask_);
}

bool grant::allows(resource value) const
{
    return valid() && (resource_mask_ & resource_bit(value)) != 0;
}

bool grant::allows(std::string_view name) const
{
    resource value{};
    return resource_from_name(name, value) && allows(value);
}

const char *app_state_name(app_state state)
{
    switch (state) {
    case app_state::registered: return "registered";
    case app_state::starting: return "starting";
    case app_state::running: return "running";
    case app_state::stopping: return "stopping";
    case app_state::failed: return "failed";
    }
    return "failed";
}

const char *app_type_name(app_type type)
{
    switch (type) {
    case app_type::service: return "service";
    case app_type::foreground: return "foreground";
    case app_type::background: return "background";
    case app_type::demo: return "demo";
    }
    return "service";
}

bool runtime::register_application(application &app)
{
    if (count_ == applications_.size() || find(app.get_manifest().id) != nullptr) return false;
    std::uint16_t mask = 0;
    if (!manifest_resources_valid(app.get_manifest(), mask) ||
        app.get_manifest().capability_count > app.get_manifest().capabilities.size()) return false;
    app.set_logger(logger_);
    applications_[count_] = &app;
    states_[count_] = app_state::registered;
    failures_[count_].clear();
    grant_generations_[count_] = 0;
    grant_masks_[count_] = mask;
    ++count_;
    return true;
}

void runtime::set_logger(logger *value)
{
    logger_ = value;
    for (std::size_t i = 0; i < count_; ++i) applications_[i]->set_logger(value);
}

application *runtime::find(std::string_view id)
{
    for (std::size_t i = 0; i < count_; ++i) {
        if (applications_[i]->get_manifest().id == id) return applications_[i];
    }
    return nullptr;
}

const application *runtime::find(std::string_view id) const
{
    for (std::size_t i = 0; i < count_; ++i) {
        if (applications_[i]->get_manifest().id == id) return applications_[i];
    }
    return nullptr;
}

const application *runtime::at(std::size_t index) const
{
    return index < count_ ? applications_[index] : nullptr;
}

bool runtime::start_application(std::string_view id)
{
    const std::size_t index = index_of(id);
    if (index == k_not_found) return false;
    std::array<bool, k_max_applications> visiting{};
    return start_index(index, visiting);
}

bool runtime::stop_application(std::string_view id)
{
    const std::size_t index = index_of(id);
    return index != k_not_found && stop_index(index);
}

bool runtime::restart_application(std::string_view id)
{
    const std::size_t index = index_of(id);
    if (index == k_not_found) return false;
    if (states_[index] == app_state::running && !stop_index(index)) return false;
    std::array<bool, k_max_applications> visiting{};
    return start_index(index, visiting);
}

bool runtime::start_all()
{
    bool ok = true;
    for (std::size_t i = 0; i < count_; ++i) {
        std::array<bool, k_max_applications> visiting{};
        ok = start_index(i, visiting) && ok;
    }
    return ok;
}

bool runtime::stop_all()
{
    bool ok = true;
    for (std::size_t i = count_; i-- > 0;) ok = stop_index(i) && ok;
    return ok;
}

std::size_t runtime::index_of(std::string_view id) const
{
    for (std::size_t i = 0; i < count_; ++i) {
        if (applications_[i]->get_manifest().id == id) return i;
    }
    return k_not_found;
}

bool runtime::start_index(std::size_t index, std::array<bool, k_max_applications> &visiting)
{
    if (states_[index] == app_state::running) return true;
    if (visiting[index]) {
        states_[index] = app_state::failed;
        failures_[index] = "dependency cycle";
        log_lifecycle(index, "failure", "dependency_cycle");
        return false;
    }
    visiting[index] = true;
    const manifest &item = applications_[index]->get_manifest();
    if (item.dependency_count > item.dependencies.size()) {
        failures_[index] = "invalid dependency declaration";
        states_[index] = app_state::failed;
        log_lifecycle(index, "failure", "invalid_dependency");
        visiting[index] = false;
        return false;
    }
    for (std::size_t dep = 0; dep < item.dependency_count; ++dep) {
        const std::size_t dependency = index_of(item.dependencies[dep]);
        if (dependency == k_not_found || !start_index(dependency, visiting)) {
            failures_[index] = "dependency unavailable: ";
            failures_[index] += item.dependencies[dep];
            states_[index] = app_state::failed;
            log_lifecycle(index, "failure", "dependency_unavailable");
            visiting[index] = false;
            return false;
        }
    }
    visiting[index] = false;
    states_[index] = app_state::starting;
    ++grant_generations_[index];
    if (grant_generations_[index] == 0) ++grant_generations_[index];
    grant application_grant{};
    application_grant.runtime_ = this;
    application_grant.index_ = index;
    application_grant.generation_ = grant_generations_[index];
    application_grant.resource_mask_ = grant_masks_[index];
    application_grant.owner_ = item.id;
    applications_[index]->grant_ = application_grant;
    log_lifecycle(index, "start", "begin");
    const auto started = std::chrono::steady_clock::now();
    const bool initialized = applications_[index]->init();
    const bool started_ok = initialized && applications_[index]->start();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (!initialized) {
        applications_[index]->grant_ = {};
        ++grant_generations_[index];
        states_[index] = app_state::failed;
        failures_[index] = "init hook failed";
        log_lifecycle(index, "failure", "init_hook_failed");
        return false;
    }
    if (!started_ok) {
        applications_[index]->grant_ = {};
        ++grant_generations_[index];
        states_[index] = app_state::failed;
        failures_[index] = "start hook failed";
        log_lifecycle(index, "failure", "start_hook_failed");
        return false;
    }
    states_[index] = app_state::running;
    if (elapsed > item.lifecycle_timeout_ms) {
        /* The hook returned success, so the observable state is running. The
         * budget is checked only after return: synchronous hooks cannot be
         * preempted, so this diagnostic must not block dependents. */
        failures_[index] = "start hook timeout";
        log_lifecycle(index, "timeout", "start_hook");
    }
    log_lifecycle(index, "start", "running");
    return true;
}

bool runtime::cascade_stops_console(std::size_t index,
                                   std::array<bool, k_max_applications> &visited) const
{
    if (visited[index]) return false;
    visited[index] = true;
    const std::string_view id = applications_[index]->get_manifest().id;
    if (applications_[index]->get_manifest().owns_console) return true;
    /* stop_index stops dependents first, so anything this application depends
     * on is also part of the cascade when the dependency is stopped. */
    for (std::size_t i = 0; i < count_; ++i) {
        const manifest &candidate = applications_[i]->get_manifest();
        for (std::size_t dep = 0; dep < candidate.dependency_count; ++dep) {
            if (candidate.dependencies[dep] == id &&
                cascade_stops_console(i, visited)) {
                return true;
            }
        }
    }
    return false;
}

bool runtime::stops_console_owner(std::string_view id) const
{
    const std::size_t index = index_of(id);
    if (index == k_not_found) return false;
    std::array<bool, k_max_applications> visited{};
    return cascade_stops_console(index, visited);
}

bool runtime::stop_index(std::size_t index)
{
    if (states_[index] == app_state::registered) return true;
    if (states_[index] == app_state::stopping) return false;
    if (states_[index] != app_state::running && states_[index] != app_state::failed) return false;
    for (std::size_t i = 0; i < count_; ++i) {
        const manifest &dependent = applications_[i]->get_manifest();
        for (std::size_t dep = 0; dep < dependent.dependency_count; ++dep) {
            if (dependent.dependencies[dep] == applications_[index]->get_manifest().id &&
                states_[i] == app_state::running && !stop_index(i)) return false;
        }
    }
    states_[index] = app_state::stopping;
    /* Revoke before teardown so callbacks that outlive the hook cannot use a
     * resource.  The generation check also invalidates copied facades. */
    applications_[index]->grant_ = {};
    ++grant_generations_[index];
    log_lifecycle(index, "stop", "begin");
    const auto started = std::chrono::steady_clock::now();
    const bool stopped = applications_[index]->teardown();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (!stopped) {
        states_[index] = app_state::failed;
        failures_[index] = "stop hook failed";
        log_lifecycle(index, "failure", "stop_hook_failed");
        return false;
    }
    /* A successful stop, including one that exceeded the budget, leaves the
     * application stopped. The timeout is post-return diagnostics only; any
     * effective bound must be enforced by the service's cooperative join. */
    states_[index] = app_state::registered;
    if (elapsed > applications_[index]->get_manifest().lifecycle_timeout_ms) {
        failures_[index] = "stop hook timeout";
        log_lifecycle(index, "timeout", "stop_hook");
    }
    log_lifecycle(index, "stop", "registered");
    return true;
}

app_state runtime::state(std::string_view id) const
{
    const std::size_t index = index_of(id);
    return index == k_not_found ? app_state::failed : states_[index];
}

std::string_view runtime::failure_reason(std::string_view id) const
{
    const std::size_t index = index_of(id);
    return index == k_not_found ? std::string_view{"application not found"} :
                                  std::string_view{failures_[index]};
}

bool runtime::restore_failure_reason(std::string_view id, std::string_view reason)
{
    const std::size_t index = index_of(id);
    if (index == k_not_found || reason.empty() || reason.size() > 127) return false;
    failures_[index] = reason;
    return true;
}

bool runtime::resources(std::string_view id, std::array<std::string_view, k_max_resources> &out,
                        std::size_t &count) const
{
    const std::size_t index = index_of(id);
    if (index == k_not_found) return false;
    const manifest &item = applications_[index]->get_manifest();
    if (item.resource_count > out.size()) return false;
    count = item.resource_count;
    for (std::size_t i = 0; i < count; ++i) out[i] = item.resources[i];
    return true;
}

grant runtime::app_grant(std::string_view id) const
{
    const std::size_t index = index_of(id);
    if (index == k_not_found ||
        (states_[index] != app_state::starting && states_[index] != app_state::running)) return {};
    grant out{};
    out.runtime_ = this;
    out.index_ = index;
    out.generation_ = grant_generations_[index];
    out.resource_mask_ = grant_masks_[index];
    out.owner_ = applications_[index]->get_manifest().id;
    return out;
}

bool runtime::grant_is_valid(std::size_t index, std::uint64_t generation,
                             std::uint16_t mask) const
{
    return index < count_ && generation != 0 && generation == grant_generations_[index] &&
           (states_[index] == app_state::starting || states_[index] == app_state::running) &&
           mask == grant_masks_[index];
}

void runtime::log_lifecycle(std::size_t index, const char *event, const char *outcome) const
{
    if (logger_ == nullptr || index >= count_ || event == nullptr || outcome == nullptr) return;
    char message[192];
    const std::string_view id = applications_[index]->get_manifest().id;
    std::snprintf(message, sizeof(message), "app=%.*s event=%s outcome=%s",
                  static_cast<int>(id.size()), id.data(), event, outcome);
    logger_->write('I', "app.lifecycle", message);
}

result runtime::execute_line(std::string_view line)
{
    std::array<std::string_view, k_max_tokens> words{};
    std::size_t count = 0;
    if (!split_words(line, words, count) || count == 0) return {};

    if (words[0] == "app") {
        if (count == 2 && words[1] == "list") {
            std::string output;
            for (std::size_t i = 0; i < count_; ++i) {
                const manifest &item = applications_[i]->get_manifest();
                output.append(item.id.data(), item.id.size());
                output += " ";
                output.append(item.version.data(), item.version.size());
                 output += " ";
                 output += app_state_name(states_[i]);
                output += "\n";
            }
            if (output.empty()) output = "no applications registered\n";
            return handled(std::move(output));
        }
        if (count == 3 && (words[1] == "info" || words[1] == "start" || words[1] == "stop")) {
            application *target = find(words[2]);
            if (target == nullptr) return rejected("app: application not found");
            const manifest &item = target->get_manifest();
            if (words[1] == "info") {
                /* app info includes the bounded, persisted last error. */
                std::string output;
                output += "id: "; output.append(item.id.data(), item.id.size()); output += "\n";
                output += "name: "; output.append(item.name.data(), item.name.size()); output += "\n";
                output += "version: "; output.append(item.version.data(), item.version.size()); output += "\n";
                output += "api_version: "; output.append(item.api_version.data(), item.api_version.size()); output += "\n";
                output += "type: "; output += app_type_name(item.type); output += "\n";
                output += "description: "; output.append(item.description.data(), item.description.size()); output += "\n";
                output += "command: "; output.append(item.command.data(), item.command.size()); output += "\n";
                output += "stack_bytes: "; output += std::to_string(item.stack_bytes); output += "\n";
                output += "queue_depth: "; output += std::to_string(item.queue_depth); output += "\n";
                output += "dependencies:";
                for (std::size_t dependency = 0; dependency < item.dependency_count; ++dependency) {
                    output += " "; output.append(item.dependencies[dependency].data(),
                                                   item.dependencies[dependency].size());
                }
                output += "\ncapabilities:";
                for (std::size_t capability = 0; capability < item.capability_count; ++capability) {
                    output += " "; output.append(item.capabilities[capability].data(),
                                                   item.capabilities[capability].size());
                }
                output += "\ncommands:";
                if (!item.command.empty()) {
                    output += " "; output.append(item.command.data(), item.command.size());
                }
                for (std::size_t command = 0; command < item.command_count; ++command) {
                    if (item.commands[command] == item.command) continue;
                    output += " "; output.append(item.commands[command].data(),
                                                   item.commands[command].size());
                }
                output += "\n";
                output += "state: ";
                output += app_state_name(states_[index_of(words[2])]);
                output += "\n";
                output += "last_error: ";
                output += failures_[index_of(words[2])].empty() ? "none" : failures_[index_of(words[2])];
                output += "\n";
                if (!failures_[index_of(words[2])].empty()) {
                    output += "failure: ";
                    output += failures_[index_of(words[2])];
                    output += "\n";
                }
                return handled(std::move(output));
            }
            /* Stopping cascades to dependents first, so the application that
             * owns the console can be torn down without ever naming it (for
             * example through `app stop cyberdeck.event_log`).  Every command
             * here arrives on that console, so any such cascade would remove the
             * only path that could bring it back.  The lifecycle API of the
             * supervisor stays available to whoever controls the boot. */
            if (words[1] == "stop" && stops_console_owner(words[2])) {
                return rejected("app: stop would remove the console; stop it from the supervisor");
            }
            const bool changed = words[1] == "start" ? start_application(words[2])
                                                       : stop_application(words[2]);
            if (!changed) return rejected("app: lifecycle operation failed");
            std::string output = "app ";
            output.append(words[1].data(), words[1].size());
            output += " ";
            output.append(item.id.data(), item.id.size());
            output += "\n";
            return handled(std::move(output));
        }
        return rejected("usage: app [list|info|start|stop] [id]");
    }

    for (std::size_t i = 0; i < count_; ++i) {
        application &app = *applications_[i];
        const manifest &item = app.get_manifest();
        bool command_match = item.command == words[0];
        for (std::size_t command = 0; command < item.command_count; ++command) {
            command_match = command_match || item.commands[command] == words[0];
        }
        if (command_match) {
            const std::size_t args_begin = line.find(words[0]) + words[0].size();
            const std::string_view args = args_begin < line.size() ? line.substr(args_begin) : std::string_view{};
            return app.execute(item.command, args);
        }
    }
    return {};
}

runtime &global_runtime()
{
    static runtime instance;
    return instance;
}

} // namespace cyberdeck_apps
