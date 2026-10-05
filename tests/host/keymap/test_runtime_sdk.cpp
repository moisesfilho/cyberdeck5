#include "apps/runtime/cyberdeck_app_logger.h"
#include "apps/runtime/cyberdeck_app_storage.h"
#include "apps/runtime/cyberdeck_command_catalog.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

namespace cyberdeck_apps {
const char *cyberdeck_app_logger_translation_unit();
}

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

class recording_logger final : public cyberdeck_apps::logger {
  public:
    std::size_t latest(std::size_t, line_callback, void *) override {
        return 0;
    }
    std::size_t writes = 0;
    event last{};

  protected:
    void write_event(const event &value) override {
        ++writes;
        last = value;
    }
};

class sdk_app final : public cyberdeck_apps::application {
  public:
    sdk_app(const char *id, const char *command, const char *extra) {
        manifest_.id = id;
        manifest_.name = id;
        manifest_.command = command;
        manifest_.resources[0] = "storage";
        manifest_.resource_count = 1;
        manifest_.commands[0] = extra;
        manifest_.command_count = 1;
    }
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
    cyberdeck_apps::manifest manifest_{};
    bool running_ = false;
};

void test_logger() {
    recording_logger sink;
    check(sink.write(cyberdeck_apps::logger::level::warning, "ui", "ready"), "logger accepts bounded fields");
    check(sink.writes == 1 && sink.last.severity == cyberdeck_apps::logger::level::warning,
          "logger forwards typed event");
    check(sink.last.tag_bytes == 2 && std::strcmp(sink.last.tag, "ui") == 0, "logger preserves bounded tag");
    check(!sink.write('I', "password", "redacted"), "logger rejects sensitive tag");
    check(!sink.write('I', nullptr, "payload"), "logger rejects null compatibility input");
    const std::string oversized(193, 'x');
    check(!sink.write(cyberdeck_apps::logger::level::info, "ui", oversized), "logger rejects oversized payload");
    check(std::strcmp(cyberdeck_apps::cyberdeck_app_logger_translation_unit(), "bounded-write") == 0,
          "logger TU is linked");
}

void test_storage() {
    cyberdeck_apps::runtime runtime;
    sdk_app app("storage.app", "storage", "read");
    check(runtime.register_application(app), "storage app registers");
    check(runtime.start_application("storage.app"), "storage app starts");
    cyberdeck_apps::storage_facade storage(runtime.app_grant("storage.app"));
    char buffer[8] = {};
    check(storage.available(), "storage grant is available");
    check(storage.bounded_read("", buffer, sizeof(buffer)).error == EINVAL, "storage rejects empty path");
    check(storage.bounded_read("/dev/null", nullptr, 0).status == cyberdeck_apps::read_status::eof,
          "zero-capacity read is EOF");
    check(storage.bounded_read("/dev/null", buffer, cyberdeck_apps::k_max_read_bytes + 1).error == EINVAL,
          "storage rejects oversized capacity");
    check(storage.bounded_read("/dev/null", nullptr, 1).error == EINVAL, "storage rejects null nonzero buffer");
}

void test_catalog() {
    cyberdeck_apps::runtime runtime;
    sdk_app first("first.app", "first", "inspect");
    sdk_app second("second.app", "second", "status");
    check(runtime.register_application(first), "first app registers");
    check(runtime.register_application(second), "second app registers");
    cyberdeck_apps::command_catalog catalog;
    check(catalog.build(runtime), "catalog builds from manifests");
    check(catalog.size() == 4, "catalog includes manifest and declared commands");
    check(catalog.dispatch("first", false)->application == "first.app", "catalog resolves manifest command");
    check(catalog.dispatch("status", false)->application == "second.app", "catalog resolves declared command");
    check(catalog.dispatch("first", true) == nullptr, "legacy command remains shell-owned");
    check(catalog.dispatch("missing", false) == nullptr, "unknown command fails closed");
    check(catalog.at(4) == nullptr, "catalog bounds-checks enumeration");
}
} // namespace

int main() {
    test_logger();
    test_storage();
    test_catalog();
    std::printf("%s: runtime SDK (%d checks)\n", failures == 0 ? "PASS" : "FAIL", checks);
    return failures == 0 ? 0 : 1;
}
