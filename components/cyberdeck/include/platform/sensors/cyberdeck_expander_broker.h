#pragma once

#include <chrono>
#include <mutex>

namespace cyberdeck_expander_broker {

class lease {
public:
    explicit lease(std::chrono::milliseconds timeout) : owns_(mutex_.try_lock_for(timeout)) {}
    ~lease() { if (owns_) mutex_.unlock(); }
    lease(const lease &) = delete;
    lease &operator=(const lease &) = delete;
    explicit operator bool() const { return owns_; }

private:
    inline static std::timed_mutex mutex_{};
    bool owns_;
};

} // namespace cyberdeck_expander_broker
