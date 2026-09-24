/*
 * RED host contract for the injectable Wi-Fi audit persistence layer.
 *
 * The production header/source intentionally do not exist yet.  This file is
 * deliberately linked against the public production header, not against a
 * test-only implementation: once the coder adds the layer, the same binary
 * exercises its actual ABI and transaction/ownership behavior.
 *
 * Planned production paths:
 *   components/cyberdeck/include/features/wifi/cyberdeck_wifi_audit_persistence.h
 *   components/cyberdeck/src/features/wifi/cyberdeck_wifi_audit_persistence.cpp
 *
 * The interface is documented in contracts/cyberdeck_wifi_audit_persistence.h.
 * There is no hardware, ESP-IDF, FreeRTOS, real SD card, or asynchronous test
 * timing here.  The fake file system and completion sink are deterministic and
 * inject write/fsync/close/rename failures, stale artifacts, ACK failure, and
 * a rename barrier for the concurrency case.
 */
#include "features/wifi/cyberdeck_wifi_audit_persistence.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace cyberdeck_wifi_audit_persistence;

int failures = 0;
int checks = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (false)

constexpr std::string_view destination =
    "/sdcard/wifi-audit/wifi-audit-20260924-000405.txt";
constexpr std::string_view temporary_suffix = ".tmp";
constexpr std::string_view backup_suffix = ".bak";

struct expected_transaction_paths {
    std::string temporary{};
    std::string backup{};
};

// Keep the fixture's sidecars tied to the requested destination instead of
// reusing the retired root-level names.  This mirrors the production
// make_transaction_paths derivation: a dot-prefixed basename in the same
// parent directory, with a bounded .tmp/.bak suffix.
expected_transaction_paths make_transaction_paths(std::string_view target)
{
    expected_transaction_paths paths;
    const std::size_t slash = target.rfind('/');
    if (slash == std::string_view::npos) return paths;

    const std::string_view parent = target.substr(0, slash + 1);
    const std::string_view name = target.substr(slash + 1);
    paths.temporary.assign(parent.data(), parent.size());
    paths.temporary.push_back('.');
    paths.temporary.append(name.data(), name.size());
    paths.temporary.append(temporary_suffix.data(), temporary_suffix.size());
    paths.backup.assign(parent.data(), parent.size());
    paths.backup.push_back('.');
    paths.backup.append(name.data(), name.size());
    paths.backup.append(backup_suffix.data(), backup_suffix.size());
    return paths;
}

const expected_transaction_paths paths = make_transaction_paths(destination);
const std::string &temporary = paths.temporary;
const std::string &backup = paths.backup;

std::string copy_string(std::string_view value)
{
    return std::string(value.data(), value.size());
}

std::string rename_event(std::string_view from, std::string_view to)
{
    return "rename:" + copy_string(from) + ">" + copy_string(to);
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

class memory_file_ops final : public file_ops {
public:
    explicit memory_file_ops(event_trace &trace) : trace_(trace) {}

    void put(std::string_view path, std::string_view data)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        files_[copy_string(path)] = copy_string(data);
        special_.erase(copy_string(path));
    }

    void inject_destination_race(std::string_view content)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        race_content_ = copy_string(content);
        race_armed_ = true;
        race_destination_inspects_ = 0;
    }

    bool get(std::string_view path, std::string &out) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = files_.find(copy_string(path));
        if (found == files_.end()) return false;
        out = found->second;
        return true;
    }

    bool exists(std::string_view path) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string name = copy_string(path);
        return files_.find(name) != files_.end() || special_.find(name) != special_.end();
    }

    void make_special(std::string_view path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string name = copy_string(path);
        files_.erase(name);
        special_.insert(name);
    }

    void fail_open() { set_flag(&fail_open_); }
    void fail_write() { set_flag(&fail_write_); }
    void fail_fsync() { set_flag(&fail_fsync_); }
    void fail_close() { set_flag(&fail_close_); }

    void set_partial_write(std::size_t limit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        partial_write_limit_ = limit;
    }

    void fail_rename_once(std::string_view from, std::string_view to)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++rename_failures_[copy_string(from) + "\n" + copy_string(to)];
    }

    void block_candidate_rename()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        block_rename_ = true;
        rename_entered_ = false;
        release_rename_ = false;
    }

    bool wait_for_candidate_rename()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return rename_cv_.wait_for(lock, std::chrono::seconds(2), [this] {
            return rename_entered_;
        });
    }

    void release_candidate_rename()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            release_rename_ = true;
        }
        rename_cv_.notify_all();
    }

    int write_calls() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return write_calls_;
    }

    int fsync_calls() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return fsync_calls_;
    }

    int close_calls() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return close_calls_;
    }

    int rename_calls() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return rename_calls_;
    }

    artifact_state inspect(std::string_view path) override
    {
        const std::string name = copy_string(path);
        std::lock_guard<std::mutex> lock(mutex_);
        trace_.record("inspect:" + name);
        if (race_armed_ && name == destination && ++race_destination_inspects_ == 2) {
            files_[name] = race_content_;
            special_.erase(name);
        }
        if (special_.find(name) != special_.end()) return artifact_state::other;
        return files_.find(name) == files_.end() ? artifact_state::missing
                                                  : artifact_state::regular;
    }

    int open_exclusive(std::string_view path) override
    {
        const std::string name = copy_string(path);
        std::lock_guard<std::mutex> lock(mutex_);
        trace_.record("open:" + name);
        if (fail_open_ || special_.find(name) != special_.end() ||
            files_.find(name) != files_.end()) {
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
        if (partial_write_limit_ >= 0) {
            amount = std::min(amount, static_cast<std::size_t>(partial_write_limit_));
        }
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
        ++close_calls_;
        const auto descriptor = descriptors_.find(fd);
        if (descriptor == descriptors_.end()) return -1;
        descriptors_.erase(descriptor);
        return fail_close_ ? -1 : 0;
    }

    int rename(std::string_view from, std::string_view to) override
    {
        const std::string source = copy_string(from);
        const std::string target = copy_string(to);
        std::unique_lock<std::mutex> lock(mutex_);
        trace_.record(rename_event(source, target));
        ++rename_calls_;

        if (block_rename_ && std::string_view(source) == std::string_view(temporary) &&
            std::string_view(target) == destination) {
            block_rename_ = false;
            rename_entered_ = true;
            rename_cv_.notify_all();
            rename_cv_.wait(lock, [this] { return release_rename_; });
        }

        auto failure = rename_failures_.find(source + "\n" + target);
        if (failure != rename_failures_.end() && failure->second > 0) {
            --failure->second;
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
        const std::string name = copy_string(path);
        std::lock_guard<std::mutex> lock(mutex_);
        trace_.record("unlink:" + name);
        const auto found = files_.find(name);
        if (found == files_.end()) return -1;
        files_.erase(found);
        return 0;
    }

private:
    void set_flag(bool *flag)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        *flag = true;
    }

    event_trace &trace_;
    mutable std::mutex mutex_;
    std::map<std::string, std::string> files_;
    std::set<std::string> special_;
    std::map<int, std::string> descriptors_;
    std::map<std::string, int> rename_failures_;
    int next_fd_ = 1;
    int partial_write_limit_ = -1;
    int write_calls_ = 0;
    int fsync_calls_ = 0;
    int close_calls_ = 0;
    int rename_calls_ = 0;
    bool fail_open_ = false;
    bool fail_write_ = false;
    bool fail_fsync_ = false;
    bool fail_close_ = false;
    bool block_rename_ = false;
    bool rename_entered_ = false;
    bool release_rename_ = false;
    std::string race_content_;
    bool race_armed_ = false;
    int race_destination_inspects_ = 0;
    std::condition_variable rename_cv_;
};

class scripted_sink final : public completion_sink {
public:
    explicit scripted_sink(event_trace &trace) : trace_(trace) {}

    void reject_next_publish() { reject_next_ = true; }

    std::size_t attempted_count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return attempted_.size();
    }

    std::size_t delivered_count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return delivered_.size();
    }

    completion delivered_at(std::size_t index) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return index < delivered_.size() ? delivered_[index] : completion{};
    }

    completion attempted_at(std::size_t index) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return index < attempted_.size() ? attempted_[index] : completion{};
    }

    bool publish(const completion &value) override
    {
        trace_.record("publish");
        std::lock_guard<std::mutex> lock(mutex_);
        attempted_.push_back(value);
        if (reject_next_) {
            reject_next_ = false;
            return false;
        }
        delivered_.push_back(value);
        return true;
    }

private:
    event_trace &trace_;
    mutable std::mutex mutex_;
    bool reject_next_ = false;
    std::vector<completion> attempted_;
    std::vector<completion> delivered_;
};

std::string real_audit_payload()
{
    return "version=1\n"
           "token=731\n"
           "status=ready\n"
           "ssid=Lab-Room\n"
           "bssid=aa:bb:cc:dd:ee:01\n"
           "ip=198.51.100.7\n";
}

completion first_result(audit_persistence &worker, scripted_sink &sink)
{
    if (sink.delivered_count() != 0) return sink.delivered_at(0);
    completion retained{};
    (void)worker.drain(retained);
    return retained;
}

void check_success(const completion &value,
                   std::uint64_t token,
                   std::string_view expected_data)
{
    CHECK(value.ok);
    CHECK(value.error == persistence_error::none);
    CHECK(value.token == token);
    CHECK(value.path == destination);
    CHECK(value.data == expected_data);
    CHECK(value.bytes == expected_data.size());
}

void check_failure(const completion &value, persistence_error expected_error)
{
    CHECK(!value.ok);
    CHECK(value.error == expected_error);
    CHECK(value.data.empty());
    CHECK(value.bytes == 0);
}

void test_success_publishes_exact_content_before_ack()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string payload = real_audit_payload();
    files.set_partial_write(7); // write_all must handle a short write.

    CHECK(worker.initialize());
    CHECK(worker.enqueue(731, destination, payload) == submit_status::accepted);
    CHECK(worker.in_flight());
    CHECK(worker.pending() == 1);
    CHECK(worker.pump_one());

    CHECK(sink.delivered_count() == 1);
    const completion result = sink.delivered_at(0);
    check_success(result, 731, payload);
    std::string on_disk;
    CHECK(files.get(destination, on_disk));
    CHECK(on_disk == payload);
    CHECK(!files.exists(temporary));
    CHECK(!files.exists(backup));
    CHECK(files.write_calls() > 1);
    CHECK(files.fsync_calls() >= 1);
    CHECK(files.close_calls() >= 1);
    CHECK(trace.contains(rename_event(temporary, destination)));
    CHECK(trace.index(rename_event(temporary, destination)) < trace.index("publish"));
    CHECK(!worker.in_flight());
    CHECK(worker.pending() == 0);
    completion no_more{};
    CHECK(!worker.drain(no_more));
}

void test_existing_destination_uses_backup_then_publishes()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string old_content = "old-audit\n";
    const std::string new_content = real_audit_payload();
    // A timestamped save is no-clobber.  Inject the destination after the
    // initial probe so the backup/publication rollback branch is exercised.
    files.inject_destination_race(old_content);

    CHECK(worker.initialize());
    CHECK(worker.enqueue(732, destination, new_content) == submit_status::accepted);
    CHECK(worker.pump_one());
    const completion result = sink.delivered_at(0);
    check_failure(result, persistence_error::invalid_request);
    CHECK(result.token == 732);
    CHECK(result.path == destination);

    std::string on_disk;
    CHECK(files.get(destination, on_disk));
    CHECK(on_disk == old_content);
    CHECK(!files.exists(backup));
    CHECK(!files.exists(temporary));
    const std::size_t move_old = trace.index(rename_event(destination, backup));
    const std::size_t publish_new = trace.index(rename_event(temporary, destination));
    const std::size_t undo_publish = trace.index(rename_event(destination, temporary));
    const std::size_t restore_old = trace.index(rename_event(backup, destination));
    CHECK(move_old != std::string::npos);
    CHECK(publish_new != std::string::npos);
    CHECK(undo_publish != std::string::npos);
    CHECK(restore_old != std::string::npos);
    CHECK(move_old < publish_new);
    CHECK(publish_new < undo_publish);
    CHECK(undo_publish < restore_old);
    CHECK(restore_old < trace.index("publish"));
}

enum class io_failure { write, fsync, close, rename };

void configure_io_failure(memory_file_ops &files, io_failure failure)
{
    switch (failure) {
    case io_failure::write:
        files.fail_write();
        break;
    case io_failure::fsync:
        files.fail_fsync();
        break;
    case io_failure::close:
        files.fail_close();
        break;
    case io_failure::rename:
        files.fail_rename_once(temporary, destination);
        break;
    }
}

persistence_error expected_error(io_failure failure)
{
    switch (failure) {
    case io_failure::write: return persistence_error::write_failed;
    case io_failure::fsync: return persistence_error::fsync_failed;
    case io_failure::close: return persistence_error::close_failed;
    case io_failure::rename: return persistence_error::rename_failed;
    }
    return persistence_error::none;
}

void run_io_failure_case(io_failure failure, bool existing_destination)
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string payload = real_audit_payload();
    const std::string old_content = "preserve-this-destination\n";
    if (existing_destination) files.inject_destination_race(old_content);
    configure_io_failure(files, failure);

    CHECK(worker.initialize());
    CHECK(worker.enqueue(740 + static_cast<std::uint64_t>(failure), destination, payload) ==
          submit_status::accepted);
    CHECK(worker.pump_one());
    const completion result = first_result(worker, sink);
    check_failure(result, expected_error(failure));
    CHECK(sink.attempted_count() == 1);

    std::string on_disk;
    if (existing_destination) {
        CHECK(files.get(destination, on_disk));
        CHECK(on_disk == old_content);
    } else {
        CHECK(!files.exists(destination));
        // With no old destination there is no proof that a partial candidate
        // is complete; it must remain available for recovery, never vanish.
        CHECK(files.exists(temporary));
    }
    CHECK(!result.ok);
    if (failure != io_failure::rename) {
        CHECK(!trace.contains(rename_event(temporary, destination)));
        CHECK(files.close_calls() == 1);
    }
    if (failure == io_failure::fsync || failure == io_failure::close) {
        CHECK(files.fsync_calls() == 1);
    }
    CHECK(!worker.in_flight());
    CHECK(worker.pending() == 0);
}

void test_all_io_failures_are_reported_without_success_ack()
{
    run_io_failure_case(io_failure::write, false);
    run_io_failure_case(io_failure::fsync, false);
    run_io_failure_case(io_failure::close, false);
    run_io_failure_case(io_failure::rename, false);
    run_io_failure_case(io_failure::write, true);
    run_io_failure_case(io_failure::fsync, true);
    run_io_failure_case(io_failure::close, true);
    run_io_failure_case(io_failure::rename, true);
}

void test_recovery_restores_backup_and_discards_only_proven_stale_tmp()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string recovered_old = "recoverable-old\n";
    const std::string new_content = real_audit_payload();
    files.put(backup, recovered_old);
    files.put(temporary, "untrusted-stale-candidate\n");

    // Recovery may restore the only proven copy, but the resulting save is
    // still no-clobber: it must not replace the recovered destination.
    CHECK(worker.initialize());
    CHECK(worker.enqueue(750, destination, new_content) == submit_status::accepted);
    CHECK(worker.pump_one());
    const completion result = first_result(worker, sink);
    check_failure(result, persistence_error::invalid_request);
    CHECK(result.token == 750);
    CHECK(result.path == destination);

    std::string on_disk;
    CHECK(files.get(destination, on_disk));
    CHECK(on_disk == recovered_old);
    CHECK(!files.exists(backup));
    CHECK(!files.exists(temporary));
    const std::size_t restore = trace.index(rename_event(backup, destination));
    const std::size_t new_open = trace.index("open:" + copy_string(temporary));
    CHECK(restore != std::string::npos);
    CHECK(new_open != std::string::npos);
    CHECK(restore < new_open);
}

void test_recovery_preserves_orphan_tmp_and_fails_closed()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    files.put(temporary, "orphan-without-proof\n");

    CHECK(worker.initialize());
    CHECK(worker.enqueue(751, destination, real_audit_payload()) == submit_status::accepted);
    CHECK(worker.pump_one());
    const completion result = first_result(worker, sink);
    check_failure(result, persistence_error::recovery_failed);
    CHECK(files.exists(temporary));
    CHECK(!files.exists(destination));
    CHECK(!files.exists(backup));
    CHECK(!trace.contains("open:" + copy_string(temporary)));
    CHECK(!worker.in_flight());
}

void test_recovery_rename_failure_keeps_backup_and_stops_before_write()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string old_content = "backup-that-must-survive\n";
    files.put(backup, old_content);
    files.fail_rename_once(backup, destination);

    CHECK(worker.initialize());
    CHECK(worker.enqueue(753, destination, real_audit_payload()) == submit_status::accepted);
    CHECK(worker.pump_one());
    const completion result = first_result(worker, sink);
    check_failure(result, persistence_error::recovery_failed);
    std::string saved_backup;
    CHECK(files.get(backup, saved_backup));
    CHECK(saved_backup == old_content);
    CHECK(!files.exists(destination));
    CHECK(!files.exists(temporary));
    CHECK(!trace.contains("open:" + copy_string(temporary)));
}

void test_recovery_with_live_destination_cleans_stale_sidecars_before_publish()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string new_content = real_audit_payload();
    const std::string old_content = "live-old\n";
    files.inject_destination_race(old_content);
    files.put(backup, "stale-backup\n");
    files.put(temporary, "stale-candidate\n");

    // Stale artifacts are cleaned only after the raced-in live destination is
    // proven; the subsequent save remains no-clobber and rolls back.
    CHECK(worker.initialize());
    CHECK(worker.enqueue(752, destination, new_content) == submit_status::accepted);
    CHECK(worker.pump_one());
    const completion result = first_result(worker, sink);
    check_failure(result, persistence_error::invalid_request);
    CHECK(result.token == 752);
    CHECK(result.path == destination);
    CHECK(!files.exists(backup));
    CHECK(!files.exists(temporary));
    std::string on_disk;
    CHECK(files.get(destination, on_disk));
    CHECK(on_disk == old_content);
}

void test_rollback_restores_old_destination_when_candidate_rename_fails()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string old_content = "rollback-old\n";
    const std::string new_content = real_audit_payload();
    files.inject_destination_race(old_content);
    files.fail_rename_once(temporary, destination);

    CHECK(worker.initialize());
    CHECK(worker.enqueue(760, destination, new_content) == submit_status::accepted);
    CHECK(worker.pump_one());
    const completion result = first_result(worker, sink);
    check_failure(result, persistence_error::rename_failed);
    std::string on_disk;
    CHECK(files.get(destination, on_disk));
    CHECK(on_disk == old_content);
    CHECK(!files.exists(backup));
    CHECK(!files.exists(temporary));
    CHECK(trace.contains(rename_event(destination, backup)));
    CHECK(trace.contains(rename_event(backup, destination)));
    CHECK(trace.index(rename_event(destination, backup)) <
          trace.index(rename_event(backup, destination)));
}

void test_rollback_failure_preserves_both_recoverable_sidecars()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string old_content = "rollback-failure-old\n";
    const std::string new_content = real_audit_payload();
    files.inject_destination_race(old_content);
    files.fail_rename_once(temporary, destination);
    files.fail_rename_once(backup, destination);

    CHECK(worker.initialize());
    CHECK(worker.enqueue(761, destination, new_content) == submit_status::accepted);
    CHECK(worker.pump_one());
    const completion result = first_result(worker, sink);
    check_failure(result, persistence_error::rollback_failed);

    std::string on_disk;
    std::string candidate;
    std::string saved_old;
    CHECK(!files.get(destination, on_disk));
    CHECK(files.get(temporary, candidate));
    CHECK(candidate == new_content);
    CHECK(files.get(backup, saved_old));
    CHECK(saved_old == old_content);
    CHECK(!result.ok);
    CHECK(sink.attempted_count() == 1);
    CHECK(!worker.in_flight());
}

void test_single_shot_queue_full_and_failure_ack_release_guard()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string first_payload = "first-real-payload\n";
    const std::string second_payload = "second-real-payload\n";

    CHECK(worker.initialize());
    CHECK(worker.enqueue(770, destination, first_payload) == submit_status::accepted);
    CHECK(worker.enqueue(771, destination, second_payload) == submit_status::rejected_queue_full);
    CHECK(worker.pending() == 1);
    CHECK(worker.in_flight());
    CHECK(worker.pump_one());
    check_success(sink.delivered_at(0), 770, first_payload);
    CHECK(!worker.in_flight());
    CHECK(worker.pending() == 0);

    // A failed result is still a terminal completion and releases the guard.
    event_trace failure_trace;
    memory_file_ops failure_files(failure_trace);
    scripted_sink failure_sink(failure_trace);
    audit_persistence failed_worker(failure_files, failure_sink);
    failure_files.fail_write();
    CHECK(failed_worker.initialize());
    CHECK(failed_worker.enqueue(772, destination, "failure\n") == submit_status::accepted);
    CHECK(failed_worker.pump_one());
    check_failure(failure_sink.delivered_at(0), persistence_error::write_failed);
    CHECK(!failed_worker.in_flight());
    CHECK(failed_worker.enqueue(773, destination, "after-failure\n") ==
          submit_status::accepted);
}

void test_ack_delivery_failure_retains_result_until_single_drain()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string payload = real_audit_payload();
    sink.reject_next_publish();

    CHECK(worker.initialize());
    CHECK(worker.enqueue(780, destination, payload) == submit_status::accepted);
    CHECK(worker.pump_one());
    CHECK(sink.attempted_count() == 1);
    CHECK(sink.delivered_count() == 0);
    CHECK(worker.in_flight());
    CHECK(worker.pending() == 1);

    completion retained{};
    CHECK(worker.drain(retained));
    check_success(retained, 780, payload);
    CHECK(!worker.in_flight());
    CHECK(worker.pending() == 0);
    CHECK(files.rename_calls() == 1);
    CHECK(worker.enqueue(781, destination, "next\n") == submit_status::accepted);
}

void test_failed_result_ack_delivery_also_releases_guard_only_on_drain()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    files.fail_write();
    sink.reject_next_publish();

    CHECK(worker.initialize());
    CHECK(worker.enqueue(782, destination, "will-fail\n") == submit_status::accepted);
    CHECK(worker.pump_one());
    CHECK(sink.attempted_count() == 1);
    CHECK(sink.delivered_count() == 0);
    CHECK(worker.in_flight());
    completion retained{};
    CHECK(worker.drain(retained));
    check_failure(retained, persistence_error::write_failed);
    CHECK(!worker.in_flight());
    CHECK(worker.enqueue(783, destination, "after-ack-failure\n") ==
          submit_status::accepted);
}

void test_concurrent_enqueue_has_exactly_one_winner()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    constexpr int thread_count = 8;
    std::array<submit_status, thread_count> statuses{};
    std::array<std::string, thread_count> payloads{};
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::mutex winner_mutex;
    int accepted = 0;
    std::string winner_payload;

    CHECK(worker.initialize());
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int i = 0; i < thread_count; ++i) {
        payloads[i] = "concurrent-payload-" + std::to_string(i) + "\n";
        threads.emplace_back([&, i] {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            const submit_status status = worker.enqueue(
                static_cast<std::uint64_t>(800 + i), destination, payloads[i]);
            statuses[i] = status;
            if (status == submit_status::accepted) {
                std::lock_guard<std::mutex> lock(winner_mutex);
                ++accepted;
                winner_payload = payloads[i];
            }
        });
    }
    while (ready.load(std::memory_order_acquire) != thread_count) std::this_thread::yield();
    go.store(true, std::memory_order_release);
    for (auto &thread : threads) thread.join();

    CHECK(accepted == 1);
    for (int i = 0; i < thread_count; ++i) {
        if (statuses[i] == submit_status::accepted) continue;
        CHECK(statuses[i] == submit_status::rejected_queue_full);
    }
    CHECK(worker.pending() == 1);
    CHECK(worker.pump_one());
    const completion result = sink.delivered_at(0);
    CHECK(result.ok);
    std::string on_disk;
    CHECK(files.get(destination, on_disk));
    CHECK(on_disk == winner_payload);
    CHECK(files.rename_calls() == 1);
    CHECK(files.write_calls() == 1);
}

void test_concurrent_enqueue_during_publish_is_rejected_without_duplicate_commit()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string first_payload = "first-concurrent-payload\n";
    const std::string second_payload = "must-not-commit\n";
    files.block_candidate_rename();

    CHECK(worker.initialize());
    CHECK(worker.enqueue(850, destination, first_payload) == submit_status::accepted);
    bool pump_result = false;
    std::thread pump([&] { pump_result = worker.pump_one(); });
    const bool reached_rename = files.wait_for_candidate_rename();
    CHECK(reached_rename);

    submit_status contender_status = submit_status::rejected_invalid;
    std::atomic<bool> contender_started{false};
    std::thread contender([&] {
        contender_started.store(true, std::memory_order_release);
        contender_status = worker.enqueue(851, destination, second_payload);
    });
    while (!contender_started.load(std::memory_order_acquire)) std::this_thread::yield();
    files.release_candidate_rename();
    pump.join();
    contender.join();

    CHECK(pump_result);
    CHECK(contender_status != submit_status::accepted);
    CHECK(sink.delivered_count() == 1);
    CHECK(files.write_calls() == 1);
    CHECK(files.rename_calls() == 1);
    std::string on_disk;
    CHECK(files.get(destination, on_disk));
    CHECK(on_disk == first_payload);
}

void test_concurrent_teardown_with_pump_enqueue_and_drain()
{
    event_trace trace;
    memory_file_ops files(trace);
    scripted_sink sink(trace);
    audit_persistence worker(files, sink);
    const std::string payload = real_audit_payload();
    files.block_candidate_rename();

    CHECK(worker.initialize());
    CHECK(worker.enqueue(900, destination, payload) == submit_status::accepted);

    bool pump_result = false;
    std::thread pump([&] { pump_result = worker.pump_one(); });
    const bool reached_rename = files.wait_for_candidate_rename();
    CHECK(reached_rename);

    std::atomic<bool> teardown_started{false};
    std::thread teardown([&] {
        teardown_started.store(true, std::memory_order_release);
        worker.teardown();
    });
    while (!teardown_started.load(std::memory_order_acquire)) std::this_thread::yield();

    // teardown must publish its terminal lifecycle state before waiting for
    // the pump's in-flight I/O.  This wait is bounded so a broken ordering
    // cannot leave the test process hanging.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (worker.initialized() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool invalidated_before_join = !worker.initialized();
    CHECK(invalidated_before_join);

    submit_status enqueue_status = submit_status::rejected_invalid;
    completion drained{};
    bool drain_result = true;
    std::thread enqueue_thread([&] {
        enqueue_status = worker.enqueue(901, destination, "after-teardown\n");
    });
    std::thread drain_thread([&] { drain_result = worker.drain(drained); });

    // The pump may finish the already-started transaction, but teardown has
    // invalidated ownership.  Release the barrier before joining every thread
    // so a regression cannot strand the suite.
    files.release_candidate_rename();
    pump.join();
    enqueue_thread.join();
    drain_thread.join();
    teardown.join();

    CHECK(pump_result);
    CHECK(enqueue_status == submit_status::rejected_not_initialized);
    CHECK(!drain_result);
    CHECK(sink.attempted_count() == 0);
    CHECK(!worker.initialized());
    CHECK(!worker.in_flight());
    CHECK(worker.pending() == 0);

    // Terminal teardown cannot be revived, and no stale completion can be
    // drained after the lifecycle transition.
    CHECK(worker.enqueue(902, destination, "post-teardown\n") ==
          submit_status::rejected_not_initialized);
    CHECK(!worker.pump_one());
    completion after_teardown{};
    CHECK(!worker.drain(after_teardown));
    worker.teardown();
    CHECK(!worker.initialized());
}

} // namespace

int main()
{
    test_success_publishes_exact_content_before_ack();
    test_existing_destination_uses_backup_then_publishes();
    test_all_io_failures_are_reported_without_success_ack();
    test_recovery_restores_backup_and_discards_only_proven_stale_tmp();
    test_recovery_preserves_orphan_tmp_and_fails_closed();
    test_recovery_rename_failure_keeps_backup_and_stops_before_write();
    test_recovery_with_live_destination_cleans_stale_sidecars_before_publish();
    test_rollback_restores_old_destination_when_candidate_rename_fails();
    test_rollback_failure_preserves_both_recoverable_sidecars();
    test_single_shot_queue_full_and_failure_ack_release_guard();
    test_ack_delivery_failure_retains_result_until_single_drain();
    test_failed_result_ack_delivery_also_releases_guard_only_on_drain();
    test_concurrent_enqueue_has_exactly_one_winner();
    test_concurrent_enqueue_during_publish_is_rejected_without_duplicate_commit();
    test_concurrent_teardown_with_pump_enqueue_and_drain();

    std::printf("wifi audit persistence: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
