#pragma once
#include <cstddef>
#include <cstdlib>
#include "freertos/task.h"

#define MALLOC_CAP_SPIRAM 0x01U
#define MALLOC_CAP_8BIT 0x02U
#define MALLOC_CAP_INTERNAL 0x04U
inline unsigned music_test_last_heap_caps = 0;
inline bool music_test_fail_internal_allocation = false;
inline void *heap_caps_malloc(std::size_t size, unsigned caps) {
    music_test_last_heap_caps = caps;
    if (music_test_fail_internal_allocation && (caps & MALLOC_CAP_INTERNAL) != 0) return nullptr;
    return std::malloc(size);
}
inline void heap_caps_free(void *ptr) { std::free(ptr); }
