#pragma once

#include "apps/image/cyberdeck_image_fit.h"
#include "apps/image/cyberdeck_image_navigation.h"
#include "apps/image/cyberdeck_image_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;
struct _lv_event_t;
using lv_event_t = _lv_event_t;

/* Full-content LVGL surface of the image viewer.
 *
 * The view is the single owner of the displayed RGB565 frame: `set_image()`
 * takes the decoded buffer by value, repoints the `lv_image_dsc_t` and frees
 * the frame it was showing, and `destroy()` releases the last one.  Nothing
 * else keeps a pointer to those pixels, so no frame can outlive or be freed
 * under the widget.  Layout math comes from the pure `fit_to_area()` helper. */
namespace cyberdeck_image_view {

/* Short local names for the pure value types; the view never sees the
 * FreeRTOS-side application header. */
using cyberdeck_image::fit_result;
using cyberdeck_image::published_image;

class view final {
  public:
    bool create(lv_obj_t *parent);
    void destroy();
    bool exists() const {
        return surface_ != nullptr;
    }
    /* Installs one decoded frame.  Returns false (and keeps the frame
     * untouched, so the caller can release it) when the view does not exist.
     * A new frame always clears a recoverable error still on screen. */
    bool set_image(published_image &image);
    void set_status(std::string_view name, std::string_view position);
    /* Applies the complete chrome of one state (name, position, error) in a
     * single step (C9).  A non-empty `chrome.message` first repoints the widget
     * away from the frame and releases it exactly once, so a recoverable error
     * is never painted over a stale bitmap; the surface stays alive, so the
     * arrows keep navigating (REQ-IMGF-06). */
    void apply_chrome(const cyberdeck_image::viewer_chrome &chrome);
    std::string_view status() const {
        return status_text_;
    }
    std::string_view position_text() const {
        return position_text_;
    }
    std::string_view error_text() const {
        return error_text_;
    }
    bool has_error() const {
        return !error_text_.empty();
    }
    bool has_image() const {
        return image_ != nullptr && pixels_ != nullptr;
    }
    /* Geometry of the last layout, for host assertions and diagnostics. */
    const fit_result &placement() const {
        return placement_;
    }
    lv_obj_t *image() const {
        return image_;
    }
    lv_obj_t *surface() const {
        return surface_;
    }

  private:
    static void resized(lv_event_t *event);
    static void parent_resized(lv_event_t *event);
    void layout();
    void clear_image();
    void clear_error();
    /* Repoints the widget away from the frame *before* releasing it. */
    void drop_frame();
    lv_obj_t *parent_ = nullptr;
    lv_obj_t *surface_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *position_ = nullptr;
    lv_obj_t *footer_ = nullptr;
    lv_obj_t *image_ = nullptr;
    /* Content-area label of a recoverable navigation error; hidden while a
     * bitmap is displayed. */
    lv_obj_t *error_ = nullptr;
    fit_result placement_{};
    /* The descriptor must outlive the widget: `lv_image_set_src()` stores the
     * pointer, it does not copy the header.  It is kept type-erased here so
     * this header never has to include LVGL; the entity is a `lv_image_dsc_t`
     * owned by the view and freed in `destroy()`. */
    void *descriptor_ = nullptr;
    std::uint16_t *pixels_ = nullptr;
    std::size_t pixel_bytes_ = 0;
    std::uint32_t image_width_ = 0;
    std::uint32_t image_height_ = 0;
    std::string status_text_;
    std::string position_text_;
    std::string error_text_;
};

} // namespace cyberdeck_image_view
