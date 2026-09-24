/*
 * RED host contract for the simplified Wi-Fi audit flow.
 *
 * The production implementation is intentionally not supplied here.  The
 * test links the real audit and persistence seams and keeps the filesystem
 * deterministic: no ESP-IDF, FreeRTOS, SD card, or device clock is used.
 * A production path is considered a test input, never a filesystem escape.
 */
#include "features/wifi/cyberdeck_wifi_audit.h"
#include "features/wifi/cyberdeck_wifi_audit_persistence.h"
#include "platform/display/cyberdeck_clock.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace audit = cyberdeck_wifi_audit;
namespace persistence = cyberdeck_wifi_audit_persistence;

int checks = 0;
int failures = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (false)

constexpr std::string_view kAuditDirectory = "/sdcard/wifi-audit/";
constexpr std::string_view kAuditFilename = "wifi-audit-20260924-000405.txt";
constexpr std::string_view kAuditDestination =
    "/sdcard/wifi-audit/wifi-audit-20260924-000405.txt";
constexpr std::string_view kPayload =
    "version=1\n"
    "token=900\n"
    "status=ready\n"
    "ssid=Lab-Room\n"
    "bssid=aa:bb:cc:dd:ee:01\n"
    "ip=198.51.100.7\n";

bool starts_with(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool in_audit_directory(std::string_view value)
{
    return value == "/sdcard/wifi-audit" || starts_with(value, kAuditDirectory);
}

bool ends_with(std::string_view value, std::string_view suffix)
{
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

bool is_sidecar(std::string_view value)
{
    return ends_with(value, ".tmp") || ends_with(value, ".bak");
}

std::string normalized_directory(std::string_view value)
{
    std::string result(value);
    while (result.size() > 1 && result.back() == '/') result.pop_back();
    return result;
}

std::string parent_directory(std::string_view value)
{
    const std::size_t slash = value.rfind('/');
    return slash == std::string_view::npos ? std::string()
                                           : normalized_directory(value.substr(0, slash));
}

class event_trace {
public:
    void record(std::string event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(std::move(event));
    }

    std::size_t index(std::string_view needle) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < events_.size(); ++i) {
            if (events_[i].find(needle) != std::string::npos) return i;
        }
        return std::string::npos;
    }

    bool contains(std::string_view needle) const { return index(needle) != std::string::npos; }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> events_;
};

class memory_file_ops final : public persistence::file_ops {
public:
    explicit memory_file_ops(event_trace &trace, std::string destination = std::string(kAuditDestination))
        : trace_(trace), destination_(std::move(destination))
    {
    }

    // This extra method is deliberately not marked override: it lets this RED
    // test compile against the current seam while still requiring the future
    // production adapter to expose directory creation through the seam.
    void ensure_directory(std::string_view path)
    {
        const std::string name = normalized_directory(path);
        std::lock_guard<std::mutex> lock(mutex_);
        directories_.insert(name);
        record_path("mkdir", name);
    }

    void put(std::string_view path, std::string_view data)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        files_[std::string(path)] = std::string(data);
        special_.erase(std::string(path));
    }

    bool get(std::string_view path, std::string &out) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = files_.find(std::string(path));
        if (found == files_.end()) return false;
        out = found->second;
        return true;
    }

    bool exists(std::string_view path) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string name(path);
        return files_.find(name) != files_.end() || special_.find(name) != special_.end();
    }

    void make_special(std::string_view path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string name(path);
        files_.erase(name);
        special_.insert(name);
    }

    void fail_write() { lock_and_set(&fail_write_); }
    void fail_fsync() { lock_and_set(&fail_fsync_); }
    void fail_close() { lock_and_set(&fail_close_); }

    void fail_rename_to_destination_once()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++rename_to_destination_failures_;
    }

    void fail_backup_restore_once()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++backup_restore_failures_;
    }

    void inject_destination_race(std::string_view content)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        race_content_ = std::string(content);
        race_armed_ = true;
        race_destination_inspects_ = 0;
    }

    void set_partial_write(std::size_t amount)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        partial_write_limit_ = static_cast<int>(amount);
    }

    bool directory_created() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return directories_.find("/sdcard/wifi-audit") != directories_.end();
    }

    std::vector<std::string> paths() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return paths_;
    }

    std::size_t write_calls() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return write_calls_;
    }

    std::size_t fsync_calls() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return fsync_calls_;
    }

    persistence::artifact_state inspect(std::string_view path) override
    {
        const std::string name(path);
        std::lock_guard<std::mutex> lock(mutex_);
        record_path("inspect", name);
        if (race_armed_ && name == destination_ && ++race_destination_inspects_ == 2) {
            files_[name] = race_content_;
        }
        if (special_.find(name) != special_.end()) return persistence::artifact_state::other;
        if (files_.find(name) != files_.end()) return persistence::artifact_state::regular;
        return persistence::artifact_state::missing;
    }

    int open_exclusive(std::string_view path) override
    {
        const std::string name(path);
        std::lock_guard<std::mutex> lock(mutex_);
        record_path("open", name);
        if (fail_open_ || special_.find(name) != special_.end() ||
            files_.find(name) != files_.end()) {
            return -1;
        }
        const std::string parent = parent_directory(name);
        if (!parent.empty() && directories_.find(parent) == directories_.end()) {
            // Model a VFS that does not implicitly create parent directories.
            return -1;
        }
        const int fd = next_fd_++;
        files_[name] = {};
        descriptors_[fd] = name;
        return fd;
    }

    std::ptrdiff_t write(int fd, std::string_view data) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        trace_.record("write");
        ++write_calls_;
        if (fail_write_) return -1;
        const auto descriptor = descriptors_.find(fd);
        if (descriptor == descriptors_.end()) return -1;
        std::string &contents = files_[descriptor->second];
        std::size_t amount = data.size();
        if (partial_write_limit_ >= 0) amount = std::min(amount, static_cast<std::size_t>(partial_write_limit_));
        contents.append(data.data(), amount);
        return static_cast<std::ptrdiff_t>(amount);
    }

    int fsync(int fd) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        trace_.record("fsync");
        ++fsync_calls_;
        if (fail_fsync_ || descriptors_.find(fd) == descriptors_.end()) return -1;
        return 0;
    }

    int close(int fd) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        trace_.record("close");
        const auto descriptor = descriptors_.find(fd);
        if (descriptor == descriptors_.end()) return -1;
        descriptors_.erase(descriptor);
        return fail_close_ ? -1 : 0;
    }

    int rename(std::string_view from, std::string_view to) override
    {
        const std::string source(from);
        const std::string target(to);
        std::lock_guard<std::mutex> lock(mutex_);
        record_path("rename", source);
        record_path("rename", target);
        trace_.record("rename:" + source + ">" + target);

        if (backup_restore_failures_ > 0 && target == destination_ && is_sidecar(source)) {
            --backup_restore_failures_;
            return -1;
        }
        if (rename_to_destination_failures_ > 0 && target == destination_) {
            --rename_to_destination_failures_;
            return -1;
        }
        const auto source_entry = files_.find(source);
        if (source_entry == files_.end() || files_.find(target) != files_.end()) return -1;
        files_[target] = std::move(source_entry->second);
        files_.erase(source_entry);
        special_.erase(target);
        return 0;
    }

    int unlink(std::string_view path) override
    {
        const std::string name(path);
        std::lock_guard<std::mutex> lock(mutex_);
        record_path("unlink", name);
        const auto found = files_.find(name);
        if (found == files_.end()) return -1;
        files_.erase(found);
        return 0;
    }

private:
    void lock_and_set(bool *flag)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        *flag = true;
    }

    void record_path(const char *operation, const std::string &path)
    {
        paths_.push_back(std::string(operation) + ":" + path);
        trace_.record(std::string(operation) + ":" + path);
    }

    event_trace &trace_;
    std::string destination_;
    mutable std::mutex mutex_;
    std::map<std::string, std::string> files_;
    std::set<std::string> special_;
    std::set<std::string> directories_;
    std::map<int, std::string> descriptors_;
    std::vector<std::string> paths_;
    int next_fd_ = 1;
    int partial_write_limit_ = -1;
    int write_calls_ = 0;
    int fsync_calls_ = 0;
    int rename_to_destination_failures_ = 0;
    int backup_restore_failures_ = 0;
    std::string race_content_;
    bool race_armed_ = false;
    int race_destination_inspects_ = 0;
    bool fail_open_ = false;
    bool fail_write_ = false;
    bool fail_fsync_ = false;
    bool fail_close_ = false;
};

class collecting_sink final : public persistence::completion_sink {
public:
    explicit collecting_sink(event_trace &trace) : trace_(trace) {}

    bool publish(const persistence::completion &value) override
    {
        trace_.record("publish");
        std::lock_guard<std::mutex> lock(mutex_);
        completions_.push_back(value);
        return true;
    }

    std::size_t count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return completions_.size();
    }

    persistence::completion at(std::size_t index) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return index < completions_.size() ? completions_[index] : persistence::completion{};
    }

private:
    event_trace &trace_;
    mutable std::mutex mutex_;
    std::vector<persistence::completion> completions_;
};

void check_failed(const persistence::completion &value, persistence::persistence_error expected)
{
    CHECK(!value.ok);
    CHECK(value.error == expected);
    CHECK(value.bytes == 0);
    CHECK(value.data.empty());
}

void test_parser_and_direct_snapshot_rendering()
{
    const auto standard = audit::parse_command("wifi audit");
    CHECK(standard.kind == audit::command::audit);
    CHECK(!standard.confirmed);

    const auto save = audit::parse_command("wifi audit save");
    CHECK(save.kind != audit::command::invalid);
    CHECK(save.kind != audit::command::audit);
    CHECK(!save.confirmed);

    CHECK(audit::parse_command("wifi audit export").kind == audit::command::invalid);
    CHECK(audit::parse_command("wifi audit export confirm").kind == audit::command::invalid);
    CHECK(audit::parse_command("wifi audit save confirm").kind == audit::command::invalid);
    CHECK(audit::parse_command("wifi audit save /sdcard/wifi-audit.txt").kind == audit::command::invalid);
    CHECK(audit::parse_command("wifi audit savex").kind == audit::command::invalid);
    CHECK(kAuditFilename == "wifi-audit-20260924-000405.txt");

    audit::snapshot ready{};
    ready.version = audit::snapshot_version;
    ready.token = 900;
    ready.status = audit::state::ready;
    std::snprintf(ready.ssid, sizeof(ready.ssid), "%s", "Lab-Room");
    std::snprintf(ready.bssid, sizeof(ready.bssid), "%s", "aa:bb:cc:dd:ee:01");
    std::snprintf(ready.ip, sizeof(ready.ip), "%s", "198.51.100.7");

    const std::string rendered = audit::render_ui(ready);
    CHECK(rendered.find("ready") != std::string::npos);
    CHECK(rendered.find("Lab-Room") != std::string::npos);
    CHECK(rendered.find("aa:bb:cc:dd:ee:01") != std::string::npos);
    CHECK(rendered.find("198.51.100.7") != std::string::npos);

    audit::snapshot missing = ready;
    std::memset(missing.ssid, 0, sizeof(missing.ssid));
    std::memset(missing.bssid, 0, sizeof(missing.bssid));
    std::memset(missing.ip, 0, sizeof(missing.ip));
    const std::string missing_render = audit::render_ui(missing);
    std::size_t marker_count = 0;
    for (std::size_t at = missing_render.find("<missing>");
         at != std::string::npos;
         at = missing_render.find("<missing>", at + 1)) {
        ++marker_count;
    }
    CHECK(marker_count >= 3);
    CHECK(missing_render.find("Lab-Room") == std::string::npos);

    // The ordinary audit request only updates the in-memory snapshot.  It
    // must not arm the explicit save/export slot.
    audit::audit_controller controller;
    CHECK(controller.initialize());
    const auto token = controller.begin({true, "Lab-Room", "aa:bb:cc:dd:ee:01", "198.51.100.7"});
    CHECK(token != 0);
    CHECK(controller.complete(token, {true, "Lab-Room", "aa:bb:cc:dd:ee:01", "198.51.100.7"}) ==
          audit::result::accepted);
    CHECK(controller.pending() == 0);
    CHECK(!controller.drain_export().ok);
}

void test_gmt3_timestamp_fixture()
{
    // The save filename uses the same fixed GMT-3 clock contract as the
    // header.  This fixture catches date rollover and zero-padding without
    // consulting the host timezone.
    cyberdeck_clock_time_t utc{2026, 9, 24, 3, 4};
    cyberdeck_clock_time_t local{};
    CHECK(cyberdeck_clock_from_utc(&utc, CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN, &local));
    CHECK(local.hour == 0);
    CHECK(local.minute == 4);

    char filename[64]{};
    std::snprintf(filename, sizeof(filename), "wifi-audit-%02u%02u%02u.txt",
                  static_cast<unsigned>(local.hour), static_cast<unsigned>(local.minute), 5U);
    CHECK(std::string_view(filename) == "wifi-audit-000405.txt");

    cyberdeck_clock_time_t midnight_utc{2026, 9, 24, 3, 0};
    cyberdeck_clock_time_t midnight_local{};
    CHECK(cyberdeck_clock_from_utc(&midnight_utc, CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN,
                                   &midnight_local));
    CHECK(midnight_local.day == 24);
    CHECK(midnight_local.hour == 0);
    CHECK(midnight_local.minute == 0);
}

void test_default_directory_is_created_and_paths_are_confined()
{
    event_trace trace;
    memory_file_ops files(trace);
    collecting_sink sink(trace);
    persistence::audit_persistence worker(files, sink);
    files.set_partial_write(5);

    CHECK(worker.initialize());
    CHECK(worker.enqueue(900, kAuditDestination, kPayload) == persistence::submit_status::accepted);
    CHECK(worker.pump_one());
    const persistence::completion result = sink.at(0);
    CHECK(result.ok);
    CHECK(result.path == kAuditDestination);
    CHECK(result.data == kPayload);
    CHECK(result.bytes == kPayload.size());
    CHECK(files.directory_created());

    std::string saved;
    CHECK(files.get(kAuditDestination, saved));
    CHECK(saved == kPayload);
    CHECK(files.write_calls() > 1);
    CHECK(files.fsync_calls() >= 1);

    bool sidecar_seen = false;
    for (const std::string &path_event : files.paths()) {
        const std::size_t colon = path_event.find(':');
        CHECK(colon != std::string::npos);
        if (colon == std::string::npos) continue;
        const std::string_view path(path_event.data() + colon + 1,
                                    path_event.size() - colon - 1);
        CHECK(in_audit_directory(path));
        if (is_sidecar(path)) sidecar_seen = true;
    }
    CHECK(sidecar_seen);
}

void test_path_validation_rejects_escapes_without_io()
{
    const char *invalid[] = {
        "/sdcard/wifi-audit.txt",
        "/sdcard/wifi-audit-other/wifi-audit-20260924-000405.txt",
        "/sdcard/wifi-audit/../escape.txt",
        "/sdcard/wifi-audit//wifi-audit-20260924-000405.txt",
        "/tmp/wifi-audit-20260924-000405.txt",
        "/sdcard/../wifi-audit-20260924-000405.txt",
    };

    for (const char *path : invalid) {
        event_trace trace;
        memory_file_ops files(trace);
        collecting_sink sink(trace);
        persistence::audit_persistence worker(files, sink);
        CHECK(worker.initialize());
        CHECK(worker.enqueue(901, path, kPayload) == persistence::submit_status::rejected_invalid);
        CHECK(files.paths().empty());
        CHECK(sink.count() == 0);
    }
}

void test_collision_never_overwrites_an_existing_file()
{
    event_trace trace;
    memory_file_ops files(trace);
    collecting_sink sink(trace);
    persistence::audit_persistence worker(files, sink);
    const std::string old_payload = "existing-audit\n";
    files.put(kAuditDestination, old_payload);

    CHECK(worker.initialize());
    const persistence::submit_status submitted =
        worker.enqueue(902, kAuditDestination, kPayload);
    CHECK(submitted == persistence::submit_status::accepted ||
          submitted == persistence::submit_status::rejected_invalid);
    if (submitted == persistence::submit_status::accepted) {
        CHECK(worker.pump_one());
        const persistence::completion result = sink.at(0);
        CHECK(!result.ok);
        CHECK(result.data.empty());
        CHECK(result.bytes == 0);
    }

    std::string still_old;
    CHECK(files.get(kAuditDestination, still_old));
    CHECK(still_old == old_payload);
    for (const persistence::completion &unused : {sink.at(0)}) {
        (void)unused;
    }
    CHECK(!trace.contains("rename:" + std::string(kAuditDestination) + ">" + std::string(kAuditDestination)));
}

void test_write_failure_never_produces_success_ack()
{
    event_trace trace;
    memory_file_ops files(trace);
    collecting_sink sink(trace);
    persistence::audit_persistence worker(files, sink);
    files.fail_write();

    CHECK(worker.initialize());
    CHECK(worker.enqueue(903, kAuditDestination, kPayload) == persistence::submit_status::accepted);
    CHECK(worker.pump_one());
    const persistence::completion result = sink.at(0);
    check_failed(result, persistence::persistence_error::write_failed);
    CHECK(!files.exists(kAuditDestination));
    CHECK(files.write_calls() == 1);
}

void test_rename_failure_keeps_candidate_in_audit_directory()
{
    event_trace trace;
    memory_file_ops files(trace);
    collecting_sink sink(trace);
    persistence::audit_persistence worker(files, sink);
    files.fail_rename_to_destination_once();

    CHECK(worker.initialize());
    CHECK(worker.enqueue(904, kAuditDestination, kPayload) == persistence::submit_status::accepted);
    CHECK(worker.pump_one());
    const persistence::completion result = sink.at(0);
    check_failed(result, persistence::persistence_error::rename_failed);
    CHECK(!files.exists(kAuditDestination));
    CHECK(files.write_calls() == 1);

    bool candidate_seen = false;
    for (const std::string &path_event : files.paths()) {
        const std::size_t colon = path_event.find(':');
        if (colon == std::string::npos) continue;
        const std::string_view path(path_event.data() + colon + 1,
                                    path_event.size() - colon - 1);
        CHECK(in_audit_directory(path));
        if (is_sidecar(path)) candidate_seen = true;
    }
    CHECK(candidate_seen);
}

void test_rollback_failure_preserves_both_sidecars()
{
    event_trace trace;
    memory_file_ops files(trace);
    collecting_sink sink(trace);
    persistence::audit_persistence worker(files, sink);
    const std::string old_payload = "old-audit\n";
    files.inject_destination_race(old_payload);
    files.fail_rename_to_destination_once();
    files.fail_backup_restore_once();

    CHECK(worker.initialize());
    CHECK(worker.enqueue(905, kAuditDestination, kPayload) == persistence::submit_status::accepted);
    CHECK(worker.pump_one());
    const persistence::completion result = sink.at(0);
    check_failed(result, persistence::persistence_error::rollback_failed);
    CHECK(!files.exists(kAuditDestination));
    std::string destination_value;
    CHECK(!files.get(kAuditDestination, destination_value));

    bool temporary = false;
    bool backup = false;
    for (const std::string &path_event : files.paths()) {
        const std::size_t colon = path_event.find(':');
        if (colon == std::string::npos) continue;
        const std::string_view path(path_event.data() + colon + 1,
                                    path_event.size() - colon - 1);
        CHECK(in_audit_directory(path));
        if (ends_with(path, ".tmp")) temporary = true;
        if (ends_with(path, ".bak")) backup = true;
    }
    CHECK(temporary);
    CHECK(backup);
    CHECK(!result.ok);
}

void test_ack_is_after_durable_publish()
{
    event_trace trace;
    memory_file_ops files(trace);
    collecting_sink sink(trace);
    persistence::audit_persistence worker(files, sink);

    CHECK(worker.initialize());
    CHECK(worker.enqueue(906, kAuditDestination, kPayload) == persistence::submit_status::accepted);
    CHECK(worker.pump_one());
    const std::size_t write = trace.index("write");
    const std::size_t sync = trace.index("fsync");
    const std::size_t close = trace.index("close");
    const std::size_t rename = trace.index("rename:");
    const std::size_t publish = trace.index("publish");
    CHECK(write != std::string_view::npos);
    CHECK(sync != std::string::npos);
    CHECK(close != std::string::npos);
    CHECK(rename != std::string_view::npos);
    CHECK(publish != std::string_view::npos);
    CHECK(write < sync);
    CHECK(sync < close);
    CHECK(close < rename);
    CHECK(rename < publish);
    CHECK(sink.at(0).ok);
}

} // namespace

int main()
{
    test_parser_and_direct_snapshot_rendering();
    test_gmt3_timestamp_fixture();
    test_default_directory_is_created_and_paths_are_confined();
    test_path_validation_rejects_escapes_without_io();
    test_collision_never_overwrites_an_existing_file();
    test_write_failure_never_produces_success_ack();
    test_rename_failure_keeps_candidate_in_audit_directory();
    test_rollback_failure_preserves_both_sidecars();
    test_ack_is_after_durable_publish();

    std::printf("wifi audit save: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
