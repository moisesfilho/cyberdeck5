#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace cyberdeck_sd_io {

enum class access { read, write };
enum class gate_state { open, closing, closed };
enum class drain_result { drained, timed_out, not_owner };

class coordinator;

class lease final {
  public:
    lease() = default;
    ~lease();
    lease(const lease &) = delete;
    lease &operator=(const lease &) = delete;
    lease(lease &&other) noexcept;
    lease &operator=(lease &&other) noexcept;
    explicit operator bool() const noexcept {
        return owner_ != nullptr;
    }
    void release() noexcept;

  private:
    friend class coordinator;
    lease(coordinator *owner, std::uint64_t generation, std::uint64_t id) noexcept;
    coordinator *owner_ = nullptr;
    std::uint64_t generation_ = 0;
    std::uint64_t id_ = 0;
};

class coordinator final {
  public:
    coordinator() = default;
    coordinator(const coordinator &) = delete;
    coordinator &operator=(const coordinator &) = delete;

    lease acquire(access mode);
    std::uint64_t begin_close();
    drain_result drain(std::uint64_t owner, std::chrono::steady_clock::time_point deadline);
    bool reopen(std::uint64_t owner);
    gate_state state() const;
    std::size_t active_leases() const;
    void record_persistence_failure();
    bool persistence_failed() const;

  private:
    friend class lease;
    void release(std::uint64_t generation, std::uint64_t id) noexcept;

    mutable std::mutex mutex_;
    std::condition_variable drained_;
    gate_state state_ = gate_state::open;
    std::uint64_t generation_ = 1;
    std::uint64_t next_id_ = 1;
    std::uint64_t close_owner_ = 0;
    std::size_t active_ = 0;
    bool persistence_failed_ = false;
};

// Bound once at boot; ownership remains with app_main for the process lifetime.
void bind_global(coordinator &owner);
lease acquire_global(access mode);
void record_global_persistence_failure();
bool global_fenced();

} // namespace cyberdeck_sd_io
