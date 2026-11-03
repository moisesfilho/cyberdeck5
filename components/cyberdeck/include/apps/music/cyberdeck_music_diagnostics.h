#pragma once

#include <cstdint>

namespace cyberdeck_music {
struct diagnostics_snapshot;
std::uint64_t diagnostics_saturating_add(std::uint64_t value, std::uint64_t amount);
void diagnostics_record_operation(std::uint64_t &calls, std::uint64_t &total_us,
                                 std::uint64_t &max_us, std::uint64_t started_us,
                                 std::uint64_t ended_us);
void diagnostics_accumulate_pause(diagnostics_snapshot &stats, std::uint64_t started_us,
                                  std::uint64_t ended_us);
}
