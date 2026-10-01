#include "apps/runtime/cyberdeck_app_runtime.h"

#include <utility>
#include <chrono>

namespace cyberdeck_apps {
namespace {

constexpr std::size_t k_max_line_bytes = 256;
constexpr std::size_t k_max_tokens = 8;

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
    app.set_logger(logger_);
    applications_[count_] = &app;
    states_[count_] = app_state::registered;
    failures_[count_].clear();
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
        return false;
    }
    visiting[index] = true;
    const manifest &item = applications_[index]->get_manifest();
    if (item.dependency_count > item.dependencies.size()) {
        failures_[index] = "invalid dependency declaration";
        states_[index] = app_state::failed;
        visiting[index] = false;
        return false;
    }
    for (std::size_t dep = 0; dep < item.dependency_count; ++dep) {
        const std::size_t dependency = index_of(item.dependencies[dep]);
        if (dependency == k_not_found || !start_index(dependency, visiting)) {
            failures_[index] = "dependency unavailable: ";
            failures_[index] += item.dependencies[dep];
            states_[index] = app_state::failed;
            visiting[index] = false;
            return false;
        }
    }
    visiting[index] = false;
    states_[index] = app_state::starting;
    const auto started = std::chrono::steady_clock::now();
    const bool initialized = applications_[index]->init();
    const bool started_ok = initialized && applications_[index]->start();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (!initialized) {
        states_[index] = app_state::failed;
        failures_[index] = "init hook failed";
        return false;
    }
    if (!started_ok) {
        states_[index] = app_state::failed;
        failures_[index] = "start hook failed";
        return false;
    }
    states_[index] = app_state::running;
    if (elapsed > item.lifecycle_timeout_ms) {
        /* The hook returned success, so the observable state is running. The
         * budget overrun is a diagnostic only: hooks are synchronous and
         * cannot be preempted, so failing a started application here would
         * report a state the device does not have and would block dependents. */
        failures_[index] = "start hook timeout";
    } else {
        failures_[index].clear();
    }
    return true;
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
    const auto started = std::chrono::steady_clock::now();
    const bool stopped = applications_[index]->teardown();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (!stopped) {
        states_[index] = app_state::failed;
        failures_[index] = "stop hook failed";
        return false;
    }
    states_[index] = app_state::registered;
    if (elapsed > applications_[index]->get_manifest().lifecycle_timeout_ms) {
        failures_[index] = "stop hook timeout";
    } else {
        failures_[index].clear();
    }
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
                if (!failures_[index_of(words[2])].empty()) {
                    output += "failure: ";
                    output += failures_[index_of(words[2])];
                    output += "\n";
                }
                return handled(std::move(output));
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
