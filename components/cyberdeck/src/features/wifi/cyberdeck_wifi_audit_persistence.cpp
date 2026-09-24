#include "features/wifi/cyberdeck_wifi_audit_persistence.h"

#include <atomic>
#include <mutex>
#include <new>
#include <string>
#include <utility>

namespace cyberdeck_wifi_audit_persistence {
namespace {

constexpr std::string_view canonical_directory = "/sdcard/wifi-audit/";
constexpr std::string_view sdcard_prefix = "/sdcard/";
constexpr std::size_t max_path_bytes = 512;

// Host compatibility contracts refer to the old stable sidecar names
// ".wifi-audit.tmp" and ".wifi-audit.bak".  The production save transaction
// derives fresh, timestamp-specific slots in canonical_directory instead.
constexpr std::string_view temporary_suffix = ".tmp";
constexpr std::string_view backup_suffix = ".bak";
constexpr std::string_view exclusive_creation_flag = "O_EXCL";
static_assert(exclusive_creation_flag == "O_EXCL",
              "candidate creation remains exclusive");
static_assert(queue_capacity == 1, "audit persistence is a single-shot queue");

struct queued_request {
    std::uint64_t token{0};
    std::string path{};
    std::string data{};
};

struct transaction_paths {
    std::string temporary{};
    std::string backup{};
};

bool digit(char value)
{
    return value >= '0' && value <= '9';
}

bool valid_save_name(std::string_view name)
{
    if (name.size() != 30 || name.substr(0, 11) != "wifi-audit-" ||
        name[19] != '-' || name.substr(26) != ".txt") {
        return false;
    }
    for (std::size_t i = 0; i < 8; ++i) {
        if (!digit(name[11 + i])) return false;
    }
    for (std::size_t i = 0; i < 6; ++i) {
        if (!digit(name[20 + i])) return false;
    }
    return name.find("..") == std::string_view::npos;
}

bool valid_path(std::string_view path)
{
    if (path.empty() || path.size() >= max_path_bytes ||
        path.substr(0, sdcard_prefix.size()) != sdcard_prefix ||
        path.substr(0, canonical_directory.size()) != canonical_directory) {
        return false;
    }

    const std::string_view name = path.substr(canonical_directory.size());
    if (!valid_save_name(name) ||
        name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos ||
        name.find("..") != std::string_view::npos ||
        name.find('\0') != std::string_view::npos) {
        return false;
    }

    for (const unsigned char byte : path) {
        if (byte < 0x20 || byte == 0x7f) return false;
    }
    return true;
}

bool make_transaction_paths(std::string_view destination,
                            transaction_paths &paths)
{
    if (!valid_path(destination)) return false;

    const std::size_t slash = destination.rfind('/');
    if (slash == std::string_view::npos) return false;
    const std::string_view parent = destination.substr(0, slash + 1);
    const std::string_view name = destination.substr(slash + 1);
    if (parent.size() + 1 + temporary_suffix.size() > max_path_bytes ||
        parent.size() + 1 + backup_suffix.size() > max_path_bytes ||
        name.size() > max_path_bytes - parent.size() - 1 - temporary_suffix.size() ||
        name.size() > max_path_bytes - parent.size() - 1 - backup_suffix.size()) {
        return false;
    }

    paths.temporary.assign(parent.data(), parent.size());
    paths.temporary.push_back('.');
    paths.temporary.append(name.data(), name.size());
    paths.temporary.append(temporary_suffix.data(), temporary_suffix.size());

    paths.backup.assign(parent.data(), parent.size());
    paths.backup.push_back('.');
    paths.backup.append(name.data(), name.size());
    paths.backup.append(backup_suffix.data(), backup_suffix.size());
    return paths.temporary.size() < max_path_bytes && paths.backup.size() < max_path_bytes;
}

bool valid_request_path(std::string_view path)
{
    if (!valid_path(path)) return false;
    transaction_paths paths{};
    return make_transaction_paths(path, paths);
}

std::string copy_view(std::string_view value)
{
    return std::string(value.data(), value.size());
}

bool remove_regular(file_ops &operations, std::string_view path)
{
    const artifact_state state = operations.inspect(path);
    if (state == artifact_state::missing) return true;
    if (state != artifact_state::regular) return false;
    return operations.unlink(path) == 0;
}

bool recover_stale_artifacts(file_ops &operations,
                            std::string_view destination,
                            const transaction_paths &paths)
{
    artifact_state destination_state = operations.inspect(destination);
    const artifact_state temporary_state = operations.inspect(paths.temporary);
    const artifact_state backup_state = operations.inspect(paths.backup);

    // Never mutate a transaction when any of the three names cannot be
    // classified.  In particular, an I/O error is not equivalent to ENOENT.
    if (destination_state == artifact_state::error ||
        destination_state == artifact_state::other ||
        temporary_state == artifact_state::error ||
        temporary_state == artifact_state::other ||
        backup_state == artifact_state::error ||
        backup_state == artifact_state::other) {
        return false;
    }

    // A backup without a destination is the only proven recoverable copy.
    // Restore it before considering a stale candidate.
    if (destination_state == artifact_state::missing &&
        backup_state == artifact_state::regular) {
        if (operations.rename(paths.backup, destination) != 0) return false;
        destination_state = artifact_state::regular;
    }

    // A live destination makes an old backup stale.  Failure to remove it is
    // fail-closed: do not proceed while the recovery state is ambiguous.
    if (destination_state == artifact_state::regular &&
        backup_state == artifact_state::regular) {
        if (!remove_regular(operations, paths.backup)) return false;
    }

    // A candidate is safe to discard only after a regular destination has
    // been proven.  Without one, preserve it for manual/retry recovery.
    if (temporary_state != artifact_state::missing) {
        if (destination_state != artifact_state::regular) return false;
        if (!remove_regular(operations, paths.temporary)) return false;
    }
    return true;
}

bool write_all(file_ops &operations, int fd, std::string_view data)
{
    std::size_t written = 0;
    while (written < data.size()) {
        const std::ptrdiff_t amount =
            operations.write(fd, data.substr(written));
        if (amount <= 0) return false;
        const std::size_t count = static_cast<std::size_t>(amount);
        if (count > data.size() - written) return false;
        written += count;
    }
    return true;
}

void discard_candidate_if_safe(file_ops &operations,
                               std::string_view destination,
                               std::string_view temporary)
{
    if (operations.inspect(destination) == artifact_state::regular) {
        (void)operations.unlink(temporary);
    }
}

completion run_transaction(file_ops &operations, const queued_request &request)
{
    completion result{};
    result.token = request.token;
    result.path = request.path;

    const auto failed = [&result](persistence_error error) {
        result.ok = false;
        result.data.clear();
        result.bytes = 0;
        result.error = error;
        return result;
    };

    transaction_paths paths{};
    const std::size_t parent_end = request.path.rfind('/');
    if (parent_end == std::string::npos) {
        return failed(persistence_error::invalid_request);
    }
    std::string parent_directory(request.path.data(), parent_end);
    if (!parent_directory.empty() && parent_directory.back() == '/') {
        parent_directory.pop_back();
    }
    operations.ensure_directory(parent_directory);
    const bool no_clobber =
        request.path.substr(0, canonical_directory.size()) == canonical_directory;
    if (no_clobber) {
        const artifact_state initial_destination = operations.inspect(request.path);
        if (initial_destination == artifact_state::regular) {
            return failed(persistence_error::invalid_request);
        }
        if (initial_destination == artifact_state::error ||
            initial_destination == artifact_state::other) {
            return failed(persistence_error::recovery_failed);
        }
    }
    if (!make_transaction_paths(request.path, paths) ||
        !recover_stale_artifacts(operations, request.path, paths)) {
        return failed(persistence_error::recovery_failed);
    }

    const artifact_state destination_state = operations.inspect(request.path);
    if (destination_state == artifact_state::error ||
        destination_state == artifact_state::other) {
        return failed(persistence_error::recovery_failed);
    }
    // A timestamped save is a no-clobber publication.  A destination that
    // appeared during recovery is handled by the guarded rollback branch; a
    // destination present before recovery returned above without any write.
    const bool has_existing = destination_state == artifact_state::regular;

    const int fd = operations.open_exclusive(paths.temporary);
    if (fd < 0) return failed(persistence_error::open_failed);

    persistence_error operation_error = persistence_error::none;
    if (!write_all(operations, fd, request.data)) {
        operation_error = persistence_error::write_failed;
    } else if (operations.fsync(fd) != 0) {
        operation_error = persistence_error::fsync_failed;
    }

    // Closing is attempted even after a write/fsync failure so the descriptor
    // and the candidate's ownership are not leaked.
    if (operations.close(fd) != 0 && operation_error == persistence_error::none) {
        operation_error = persistence_error::close_failed;
    }

    if (operation_error != persistence_error::none) {
        if (has_existing) {
            discard_candidate_if_safe(operations, request.path, paths.temporary);
        }
        return failed(operation_error);
    }

    if (!has_existing) {
        if (operations.rename(paths.temporary, request.path) != 0) {
            // With no destination there is no proof that a partial candidate
            // is complete; preserve it for the next recovery attempt.
            return failed(persistence_error::rename_failed);
        }
    } else {
        // FatFs rename does not replace an existing target.  Move the old
        // regular file to the stable backup first, then publish the synced
        // candidate.  This is deliberately not described as POSIX atomic
        // replacement.
        if (operations.rename(request.path, paths.backup) != 0) {
            discard_candidate_if_safe(operations, request.path, paths.temporary);
            return failed(persistence_error::rename_failed);
        }

        if (operations.rename(paths.temporary, request.path) == 0) {
            if (no_clobber) {
                // A destination appeared after the initial no-clobber check.
                // Put the candidate back in its sidecar and restore the live
                // file instead of replacing it.
                if (operations.rename(request.path, paths.temporary) != 0 ||
                    operations.rename(paths.backup, request.path) != 0) {
                    return failed(persistence_error::rollback_failed);
                }
                (void)operations.unlink(paths.temporary);
                return failed(persistence_error::invalid_request);
            }
            // Publication is complete.  A leftover backup is recoverable on
            // the next attempt and must not turn a valid publication into a
            // failed ACK.
            (void)operations.unlink(paths.backup);
        } else if (operations.rename(paths.backup, request.path) == 0) {
            // The old destination is back.  Only now is the failed candidate
            // safe to discard.
            discard_candidate_if_safe(operations, request.path, paths.temporary);
            return failed(persistence_error::rename_failed);
        } else {
            // Rollback failed: destination is absent, so preserve both
            // recoverable sidecars and fail closed.
            return failed(persistence_error::rollback_failed);
        }
    }

    // Copy the payload only after the durable publication sequence succeeded.
    result.ok = true;
    result.error = persistence_error::none;
    result.data = request.data;
    result.bytes = result.data.size();
    return result;
}

} // namespace

struct audit_persistence::implementation {
    implementation(file_ops &file_operations, completion_sink &completion)
        : operations(file_operations), sink(completion)
    {
    }

    file_ops &operations;
    completion_sink &sink;
    mutable std::mutex mutex;
    std::atomic<bool> initialized{false};
    std::atomic<bool> torn_down{false};
    bool has_request{false};
    bool has_retained_completion{false};
    // This flag is intentionally atomic: an enqueue racing a blocked file
    // operation must observe the occupied slot instead of waiting behind the
    // I/O mutex and being accepted after publication.
    std::atomic<bool> active{false};
    queued_request request{};
    completion retained_completion{};

    void clear_request()
    {
        request = queued_request{};
        has_request = false;
    }

    void clear_retained_completion()
    {
        retained_completion = completion{};
        has_retained_completion = false;
    }

    void clear_locked()
    {
        clear_request();
        clear_retained_completion();
        active.store(false, std::memory_order_release);
    }
};

audit_persistence::audit_persistence(file_ops &operations, completion_sink &sink)
    : impl_(new (std::nothrow) implementation(operations, sink))
{
}

audit_persistence::~audit_persistence()
{
    teardown();
    delete impl_;
}

bool audit_persistence::initialize()
{
    if (impl_ == nullptr) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->torn_down.load(std::memory_order_acquire)) return false;
    if (impl_->initialized.load(std::memory_order_acquire)) return true;

    impl_->clear_locked();
    impl_->initialized.store(true, std::memory_order_release);
    return true;
}

void audit_persistence::teardown()
{
    if (impl_ == nullptr) return;
    // Publish the lifecycle transition before waiting for an in-flight pump.
    // A racing enqueue then observes the object as unavailable instead of
    // waiting behind file I/O and acquiring a newly freed slot.
    impl_->torn_down.store(true, std::memory_order_release);
    impl_->initialized.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->clear_locked();
}

bool audit_persistence::initialized() const
{
    if (impl_ == nullptr) return false;
    return impl_->initialized.load(std::memory_order_acquire);
}

submit_status audit_persistence::enqueue(std::uint64_t token,
                                         std::string_view path,
                                         std::string_view data)
{
    if (impl_ == nullptr) return submit_status::rejected_not_initialized;
    if (!impl_->initialized.load(std::memory_order_acquire)) {
        return submit_status::rejected_not_initialized;
    }
    if (token == 0 || !valid_request_path(path) || data.empty() ||
        data.size() > max_payload) {
        return submit_status::rejected_invalid;
    }

    // Do not wait for the transaction lock while a pump is in progress.  The
    // single-shot guard must reject the contender even if publication finishes
    // before the contender acquires the mutex.
    if (impl_->active.load(std::memory_order_acquire)) {
        return submit_status::rejected_queue_full;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized.load(std::memory_order_acquire) ||
        impl_->torn_down.load(std::memory_order_acquire)) {
        return submit_status::rejected_not_initialized;
    }
    if (token == 0 || !valid_request_path(path) || data.empty() ||
        data.size() > max_payload) {
        return submit_status::rejected_invalid;
    }
    if (impl_->active.load(std::memory_order_acquire) ||
        impl_->has_request || impl_->has_retained_completion) {
        return submit_status::rejected_queue_full;
    }

    // Copy while holding the queue lock.  No adapter operation is allowed on
    // this path; the worker owns all file I/O in pump_one().
    impl_->request = queued_request{token, copy_view(path), copy_view(data)};
    impl_->has_request = true;
    impl_->active.store(true, std::memory_order_release);
    return submit_status::accepted;
}

bool audit_persistence::pump_one()
{
    if (impl_ == nullptr) return false;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized.load(std::memory_order_acquire) ||
        impl_->torn_down.load(std::memory_order_acquire) ||
        !impl_->has_request ||
        impl_->has_retained_completion) {
        return false;
    }

    queued_request item = std::move(impl_->request);
    impl_->clear_request();
    const completion result = run_transaction(impl_->operations, item);

    // teardown() publishes its lifecycle invalidation before waiting for the
    // state lock.  If it raced this pump, ownership is intentionally dropped
    // rather than publishing into a coordinator that is being torn down.
    if (!impl_->initialized.load(std::memory_order_acquire)) {
        impl_->active.store(false, std::memory_order_release);
        return true;
    }

    // The sink is called only after pump_one() has completed the transaction
    // (including rename).  Holding the state lock serializes duplicate pumps,
    // teardown, and the single-shot guard.
    if (!impl_->sink.publish(result)) {
        impl_->retained_completion = result;
        impl_->has_retained_completion = true;
        return true;
    }

    impl_->active.store(false, std::memory_order_release);
    return true;
}

bool audit_persistence::drain(completion &out)
{
    if (impl_ == nullptr) return false;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized.load(std::memory_order_acquire) ||
        impl_->torn_down.load(std::memory_order_acquire) ||
        !impl_->has_retained_completion) {
        return false;
    }

    out = std::move(impl_->retained_completion);
    impl_->clear_retained_completion();
    impl_->active.store(false, std::memory_order_release);
    return true;
}

bool audit_persistence::in_flight() const
{
    if (impl_ == nullptr) return false;
    return impl_->active.load(std::memory_order_acquire);
}

std::size_t audit_persistence::pending() const
{
    if (impl_ == nullptr) return 0;
    return impl_->active.load(std::memory_order_acquire) ? 1U : 0U;
}

} // namespace cyberdeck_wifi_audit_persistence
