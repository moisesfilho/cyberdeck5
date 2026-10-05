#include "apps/runtime/cyberdeck_app_quota.h"

namespace cyberdeck_apps {
namespace {

bool within(const quota_usage &value, const quota_limits &limits)
{
    return value.resources <= limits.resources && value.grants <= limits.grants &&
           value.stack_bytes <= limits.stack_bytes && value.queue_depth <= limits.queue_depth &&
           value.bounded_read_bytes <= limits.bounded_read_bytes &&
           value.logger_events <= limits.logger_events && value.output_bytes <= limits.output_bytes &&
           value.lifecycle_timeout_ms <= limits.lifecycle_timeout_ms;
}

} // namespace

bool quota::allows(std::size_t application, const quota_usage &requested) const
{
    return application < usage_.size() && within(requested, limits_);
}

bool quota::reserve(std::size_t application, const quota_usage &requested)
{
    if (!allows(application, requested)) return false;
    usage_[application] = requested;
    if (++generations_[application] == 0) ++generations_[application];
    return true;
}

void quota::revoke(std::size_t application)
{
    if (application >= usage_.size()) return;
    usage_[application] = {};
    if (++generations_[application] == 0) ++generations_[application];
}

std::uint64_t quota::generation(std::size_t application) const
{
    return application < generations_.size() ? generations_[application] : 0;
}

} // namespace cyberdeck_apps
