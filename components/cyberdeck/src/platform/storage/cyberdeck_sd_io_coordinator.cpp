#include "platform/storage/cyberdeck_sd_io_coordinator.h"

#include <atomic>
#include <utility>

namespace cyberdeck_sd_io {

namespace {
std::atomic<coordinator *> global_owner{nullptr};
}

void bind_global(coordinator &owner) {
    coordinator *expected = nullptr;
    (void)global_owner.compare_exchange_strong(expected, &owner);
}

lease acquire_global(access mode) {
    auto *owner = global_owner.load(std::memory_order_acquire);
    return owner == nullptr ? lease{} : owner->acquire(mode);
}

void record_global_persistence_failure() {
    auto *owner = global_owner.load(std::memory_order_acquire);
    if (owner != nullptr)
        owner->record_persistence_failure();
}

bool global_fenced() {
    auto *owner = global_owner.load(std::memory_order_acquire);
    return owner == nullptr || owner->state() != gate_state::open;
}

void coordinator::record_persistence_failure() {
    std::lock_guard<std::mutex> lock(mutex_);
    persistence_failed_ = true;
}

bool coordinator::persistence_failed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return persistence_failed_;
}

lease::lease(coordinator *owner, std::uint64_t generation, std::uint64_t id) noexcept
    : owner_(owner), generation_(generation), id_(id) {}

lease::~lease() {
    release();
}

lease::lease(lease &&other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), generation_(other.generation_), id_(other.id_) {}

lease &lease::operator=(lease &&other) noexcept {
    if (this == &other)
        return *this;
    release();
    owner_ = std::exchange(other.owner_, nullptr);
    generation_ = other.generation_;
    id_ = other.id_;
    return *this;
}

void lease::release() noexcept {
    if (!owner_)
        return;
    coordinator *owner = std::exchange(owner_, nullptr);
    owner->release(generation_, id_);
}

lease coordinator::acquire(access) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != gate_state::open || next_id_ == 0)
        return {};
    const auto id = next_id_++;
    ++active_;
    return lease(this, generation_, id);
}

std::uint64_t coordinator::begin_close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != gate_state::open || next_id_ == 0)
        return 0;
    state_ = gate_state::closing;
    close_owner_ = next_id_++;
    return close_owner_;
}

drain_result coordinator::drain(std::uint64_t owner, std::chrono::steady_clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!owner || state_ != gate_state::closing || close_owner_ != owner)
        return drain_result::not_owner;
    if (!drained_.wait_until(lock, deadline, [this] { return active_ == 0; }))
        return drain_result::timed_out;
    state_ = gate_state::closed;
    return drain_result::drained;
}

bool coordinator::reopen(std::uint64_t owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!owner || state_ != gate_state::closing || close_owner_ != owner)
        return false;
    state_ = gate_state::open;
    close_owner_ = 0;
    return true;
}

gate_state coordinator::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::size_t coordinator::active_leases() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

void coordinator::release(std::uint64_t generation, std::uint64_t id) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!id || active_ == 0)
        return;
    (void)generation;
    --active_;
    if (active_ == 0)
        drained_.notify_all();
}

} // namespace cyberdeck_sd_io
