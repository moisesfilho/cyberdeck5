#pragma once

#include "apps/image/cyberdeck_image_list.h"

#include <array>
#include <cstddef>
#include <cstdint>

/* Value types shared by the image application and its LVGL view.
 *
 * Kept free of FreeRTOS/LVGL/ESP-IDF so the view header stays host-linkable and
 * the UI never has to include the FreeRTOS-side application header. */
namespace cyberdeck_image {

/* Task stack of the decode worker.  It is pinned here (not in the FreeRTOS-side
 * application) so the host stack-budget regression and the structural contract
 * derive their limit from the same source of truth.  A full directory page is
 * ~8 KiB and must never live on this stack (BUG-IMG-01/02). */
inline constexpr std::size_t k_worker_stack_bytes = 6144;

enum class viewer_state : std::uint8_t { idle, loading, ready, error };

/* Decoded frame handed to the view.  Ownership is transferred by value: the
 * receiver releases it through `application::release()` or, once installed,
 * through `cyberdeck_image_view::view::destroy()`. */
struct published_image {
    std::uint16_t *pixels = nullptr;
    std::size_t pixel_bytes = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t index = 0;
    std::size_t count = 0;
    bool truncated = false;
    std::array<char, k_max_name_bytes + 1> name{};
};

} // namespace cyberdeck_image
