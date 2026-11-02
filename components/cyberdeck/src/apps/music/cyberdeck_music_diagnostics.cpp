#include "apps/music/cyberdeck_music_diagnostics.h"
#include "apps/music/cyberdeck_music_player.h"

#include <limits>

namespace cyberdeck_music {
std::uint64_t diagnostics_saturating_add(std::uint64_t value, std::uint64_t amount) {
    return amount > UINT64_MAX - value ? UINT64_MAX : value + amount;
}

void diagnostics_record_operation(std::uint64_t &calls, std::uint64_t &total_us,
                                  std::uint64_t &max_us, std::uint64_t started_us,
                                  std::uint64_t ended_us) {
    const std::uint64_t elapsed_us = ended_us >= started_us ? ended_us - started_us : 0;
    calls = diagnostics_saturating_add(calls, 1);
    total_us = diagnostics_saturating_add(total_us, elapsed_us);
    if (elapsed_us > max_us)
        max_us = elapsed_us;
}

void diagnostics_accumulate_pause(diagnostics_snapshot &stats, std::uint64_t started_us,
                                  std::uint64_t ended_us) {
    if (ended_us >= started_us)
        stats.pause_time_us = diagnostics_saturating_add(stats.pause_time_us, ended_us - started_us);
}
}
