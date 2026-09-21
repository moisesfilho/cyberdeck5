#include "platform/logging/event_log_recent.h"

extern "C" size_t event_log_recent_indices(size_t recent_count, size_t next, size_t count,
                                             size_t capacity, size_t *out_indices)
{
    if (out_indices == nullptr || count == 0 || capacity == 0 || recent_count == 0 ||
        recent_count > capacity || next >= capacity) {
        return 0;
    }

    const size_t selected = count < recent_count ? count : recent_count;
    size_t index = next >= selected ? next - selected : capacity - (selected - next);
    for (size_t i = 0; i < selected; ++i) {
        out_indices[i] = index;
        index = index + 1 < capacity ? index + 1 : 0;
    }
    return selected;
}
