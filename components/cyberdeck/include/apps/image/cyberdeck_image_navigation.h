#pragma once

#include "apps/image/cyberdeck_image_list.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/* Pure decision model of the image viewer (REQ-IMGF-06/07, contracts C8/C9).
 *
 * Two questions are answered here once, so neither the FreeRTOS-side
 * application nor the LVGL surface restates them from booleans and strings:
 *
 *  - `completion_disposition_for()` classifies a finished job: a published
 *    frame, a *recoverable* error that keeps the viewer open (arrows continue
 *    to browse toward a valid image), the *initial* error that returns to the
 *    shell, or a discarded result (cancelled/stale).  `run_job()`, `publish()`
 *    and `fail()` call it instead of re-deriving the rule.
 *  - `chrome_after_*()` build the complete chrome of one state - name,
 *    indicator, error message and whether a bitmap is installed - so one
 *    application step never mixes a new index with an old frame or an old error.
 *
 * Nothing here touches LVGL, ESP-IDF or the filesystem, so the host harness
 * executes the same rules the firmware uses. */
namespace cyberdeck_image {

enum class completion_origin : std::uint8_t {
    /** `image <path>`: a failed load reports to the shell and leaves the app. */
    initial,
    /** Arrow navigation: a failed load keeps the viewer open with a typed error. */
    navigation,
};

enum class completion_outcome : std::uint8_t { ok, failed, cancelled };

enum class completion_disposition : std::uint8_t {
    publish,
    recoverable_error,
    initial_error,
    discard,
};

/** `current` is "this generation is still the newest requested one and the
 * application is not cancelling".  A stale or cancelled result is always
 * discarded, whatever the outcome, so a late frame/error never overwrites the
 * current state. */
completion_disposition completion_disposition_for(completion_origin origin, completion_outcome outcome,
                                                  bool current) noexcept;

/** Chrome of the viewer surface.  `frame` mirrors whether a bitmap is installed
 * under it; an empty `message` means "no error". */
struct viewer_chrome {
    std::array<char, k_max_name_bytes + 1> name{};
    std::size_t index = 0;
    std::size_t count = 0;
    bool truncated = false;
    bool frame = false;
    std::string message{};

    bool has_error() const noexcept {
        return !message.empty();
    }
    /** One-based `i/N`, `i/N+`, `0/0` for an empty page (`format_position`). */
    std::string position() const;
};

/** Success: the name/position of the frame that is about to be installed. */
viewer_chrome chrome_after_publish(std::string_view name, std::size_t index, std::size_t count, bool truncated);
/** Recoverable failure: no frame, the name/position of the entry that failed and
 * the typed reason (no trailing LF; the terminal channel adds its own). */
viewer_chrome chrome_after_error(std::string_view name, std::size_t index, std::size_t count, bool truncated,
                                 std::string_view message);
/** Navigation accepted by the application (D11).  A displayed error is cleared
 * immediately and the indicator moves to the accepted target without a frame;
 * a displayed frame keeps its own index until the result arrives, so the chrome
 * never shows a new index over an old bitmap (NEG-IMGF-RNAV-33/34). */
viewer_chrome chrome_after_accept(const viewer_chrome &current, std::string_view name, std::size_t index,
                                  std::size_t count, bool truncated);

/** Target of an accepted navigation request.  `application::navigate()` fills it
 * under the publication mutex, so the UI applies `chrome_after_accept()` in the
 * same key event instead of waiting for the next timer tick. */
struct navigation_target {
    std::array<char, k_max_name_bytes + 1> name{};
    std::size_t index = 0;
    std::size_t count = 0;
    bool truncated = false;
};

} // namespace cyberdeck_image
