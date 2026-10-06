#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <deque>
#include <algorithm>

/* `lvgl.h` includes `lv_version.h`, so `LVGL_VERSION_MAJOR` is defined on every
 * device build of a `#if defined(LVGL_VERSION_MAJOR)` branch.  The host shim must
 * mirror that, or it silently compiles the branch out and stops testing it. */
#define LVGL_VERSION_MAJOR 9

struct lv_color_t { std::uint32_t value{}; };
using lv_coord_t = int32_t;
struct lv_font_t { int32_t line_height{16}; };
struct lv_font_glyph_dsc_t { int32_t adv_w{}; };
struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;
struct lv_event_t { lv_obj_t *target{}; void *user_data{}; };
using lv_event_cb_t = void (*)(lv_event_t *);
struct lv_point_t { int32_t x{}; int32_t y{}; };
struct lv_indev_t { int type{}; lv_point_t point{}; };

/* The enumerations a composed root sets stay ahead of `_lv_obj_t`, because the
 * object records its scroll state with the LVGL 9 defaults `lv_obj_constructor()`
 * installs: a fresh object scrolls in every direction, shows an automatic
 * scrollbar and chains scrolling to its parent.  A recorded value of 0 therefore
 * means production disabled it, not that the shim defaulted to it. */
inline constexpr int LV_DIR_NONE=0, LV_DIR_ALL=1, LV_SCROLLBAR_MODE_OFF=0,
    LV_SCROLLBAR_MODE_AUTO=3,
    LV_LAYOUT_FLEX=1, LV_LAYOUT_NONE=0, LV_FLEX_FLOW_ROW=0, LV_FLEX_FLOW_COLUMN=1,
    LV_FLEX_ALIGN_START=0,
    LV_FLEX_ALIGN_CENTER=1, LV_FLEX_ALIGN_END=2, LV_TEXT_ALIGN_CENTER=1,
    LV_PART_MAIN=0, LV_PART_INDICATOR=1, LV_PART_KNOB=2, LV_OPA_TRANSP=0,
     LV_OPA_COVER=255, LV_RADIUS_CIRCLE=999, LV_EVENT_SIZE_CHANGED=1,
     LV_EVENT_FOCUSED=2, LV_EVENT_INSERT=3, LV_EVENT_VALUE_CHANGED=4,
     LV_EVENT_KEY=5, LV_EVENT_PRESSED=6, LV_EVENT_RELEASED=7,
     LV_INDEV_TYPE_POINTER=1, LV_INDEV_TYPE_KEYPAD=2;
inline constexpr int LV_OBJ_FLAG_CLICKABLE=1, LV_OBJ_FLAG_CLICK_FOCUSABLE=2;

struct shim_event_cb { lv_event_cb_t callback{}; int filter{}; void *user_data{}; };

struct _lv_obj_t {
    _lv_obj_t *parent{}; std::vector<_lv_obj_t *> children; std::string text;
    int32_t width{}, height{}, x{}, y{}; bool hidden{};
    const lv_font_t *text_font{}; int32_t text_align{-1}; int32_t long_mode{-1};
    int32_t flex_main{-1}, flex_cross{-1}, flex_track{-1};
    /* -1 means the shim never saw a background/arc color applied, so "the bar is
     * painted black" cannot pass on a default that already reads as black. */
    std::uint32_t color{static_cast<std::uint32_t>(-1)};
    bool ignore_layout{};
    /* LVGL 9 exposes no public getter for a style property, for the layout or
     * for the flex attributes, so the shim records what the code under test
     * applied and the host suite reads the fields.  A style still holding -1 was
     * never set: that keeps an explicit zero distinguishable from an absent
     * style, so a "the chrome is zero" assertion cannot pass on a default. */
    int32_t border_width{-1}, pad_top{-1}, pad_bottom{-1}, pad_left{-1},
        pad_right{-1}, pad_row{-1}, pad_column{-1}, flex_grow{-1}, layout{-1},
        flex_flow{-1};
    /* Scroll state, defaulted as `lv_obj_constructor()` leaves a new object. */
    int32_t scroll_dir{LV_DIR_ALL}, scrollbar_mode{LV_SCROLLBAR_MODE_AUTO};
    bool scroll_chain{true};
    std::vector<shim_event_cb> event_callbacks;
};
using lv_result_t = int;
inline constexpr lv_result_t LV_RESULT_OK = 0, LV_RESULT_INVALID = -1;
inline constexpr int LV_SIZE_CONTENT=-1;
inline constexpr int LV_LABEL_LONG_CLIP=0;
inline constexpr const char *LV_SYMBOL_BLUETOOTH="BT", *LV_SYMBOL_CHARGE="CHG",
    *LV_SYMBOL_BATTERY_FULL="BAT", *LV_SYMBOL_MINUS="-";
inline lv_color_t lv_color_hex(std::uint32_t v) { return {v}; }
inline int lv_pct(int v) { return v; }
#define LV_PCT(v) (v)
inline int lv_obj_width(const lv_obj_t *o) { return o ? o->width : 0; }
inline lv_obj_t *lv_obj_create(lv_obj_t *p) { auto *o=new lv_obj_t; o->parent=p; if(p)p->children.push_back(o); return o; }
inline lv_obj_t *lv_label_create(lv_obj_t *p) { return lv_obj_create(p); }
inline lv_obj_t *lv_arc_create(lv_obj_t *p) { return lv_obj_create(p); }
inline lv_obj_t *lv_textarea_create(lv_obj_t *p) { return lv_obj_create(p); }
inline lv_obj_t *lv_keyboard_create(lv_obj_t *p) { return lv_obj_create(p); }
inline void lv_obj_delete(lv_obj_t *o) { delete o; }
/* LVGL 9 keeps the v8 spelling alive through its API map; mirror it so the
 * shim never decides which name a production adapter is allowed to use. */
inline void lv_obj_del(lv_obj_t *o) { lv_obj_delete(o); }
/* Single fake display root.  Only adapters that own the LVGL root may call
 * lv_scr_act(); a host test clears it to model a display that is not ready. */
inline lv_obj_t *&lv_shim_active_screen() { static lv_obj_t *screen = nullptr; return screen; }
inline lv_obj_t *lv_scr_act() { return lv_shim_active_screen(); }
inline void lv_obj_set_size(lv_obj_t *o,int32_t w,int32_t h){if(o){o->width=w;o->height=h;}}
inline void lv_obj_set_width(lv_obj_t *o,int32_t w){if(o)o->width=w;} inline void lv_obj_set_height(lv_obj_t *o,int32_t h){if(o)o->height=h;}
inline int32_t lv_obj_get_width(lv_obj_t *o){return o?o->width:0;} inline int32_t lv_obj_get_height(lv_obj_t *o){return o?o->height:0;} inline int32_t lv_obj_get_x(const lv_obj_t *o){return o?o->x:0;} inline int32_t lv_obj_get_y(const lv_obj_t *o){return o?o->y:0;} inline lv_obj_t *lv_obj_get_child(lv_obj_t *o,int i){return o&&i>=0&&i<(int)o->children.size()?o->children[i]:nullptr;}
inline void lv_obj_set_x(lv_obj_t*o,int32_t v){if(o)o->x=v;} inline void lv_obj_set_y(lv_obj_t*o,int32_t v){if(o)o->y=v;}
inline void lv_obj_set_pos(lv_obj_t*o,int32_t x,int32_t y){if(o){o->x=x;o->y=y;}}
inline int32_t lv_font_get_line_height(const lv_font_t*f){return f?f->line_height:0;}
inline void lv_obj_set_hidden(lv_obj_t*o,bool v){if(o)o->hidden=v;} inline bool lv_obj_has_flag(lv_obj_t*o,int){return o&&o->hidden;}
inline void lv_obj_add_flag(lv_obj_t*, int) {}
inline void lv_obj_clear_flag(lv_obj_t*, int) {}
/* LVGL 9 keeps ignore_layout as a property behind a dedicated setter, because
 * `lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT)` is LV_DEPRECATED there.
 * Mirror the 9.x setter/getter only: the host must not decide which spelling of
 * the overlay exclusion production is allowed to use. No outline API is
 * modeled; the header contract is intentionally exercised only by the bitmap
 * overlay path. */
inline void lv_obj_set_ignore_layout(lv_obj_t*o,bool v){if(o)o->ignore_layout=v;}
inline bool lv_obj_is_ignore_layout(const lv_obj_t*o){return o&&o->ignore_layout;}
inline void lv_obj_set_layout(lv_obj_t*o,int v){if(o)o->layout=v;} inline void lv_obj_set_flex_flow(lv_obj_t*o,int v){if(o)o->flex_flow=v;} inline void lv_obj_set_flex_align(lv_obj_t*o,int main,int cross,int track){if(o){o->flex_main=main;o->flex_cross=cross;o->flex_track=track;}} inline void lv_obj_set_flex_grow(lv_obj_t*o,int v){if(o)o->flex_grow=v;}
inline void lv_obj_set_scroll_dir(lv_obj_t*o,int v){if(o)o->scroll_dir=v;} inline void lv_obj_set_scroll_chain(lv_obj_t*o,bool v){if(o)o->scroll_chain=v;} inline void lv_obj_set_scrollbar_mode(lv_obj_t*o,int v){if(o)o->scrollbar_mode=v;} inline void lv_obj_set_scrollable(lv_obj_t*,bool){} inline void lv_obj_set_overflow_visible(lv_obj_t*,bool){}
/* Public LVGL 9 read-backs for the scroll state, so the host suite asserts the
 * device-visible result instead of the spelling that produced it. */
inline int lv_obj_get_scroll_dir(const lv_obj_t*o){return o?o->scroll_dir:LV_DIR_NONE;} inline int lv_obj_get_scrollbar_mode(const lv_obj_t*o){return o?o->scrollbar_mode:LV_SCROLLBAR_MODE_OFF;}
/* `lv_obj_get_style_space_*_internal()` in LVGL 9 is the border width plus the
 * padding (with the default full border side), and `lv_obj_get_content_*` is
 * the object size minus the space on both sides.  Mirroring both lets the host
 * suite place a child the way the device does, from the applied chrome. */
inline int32_t lv_shim_style_inset(int32_t style){return style<0?0:style;}
inline int32_t lv_obj_get_content_width(const lv_obj_t*o){if(!o)return 0;return o->width-lv_shim_style_inset(o->border_width)-lv_shim_style_inset(o->pad_left)-lv_shim_style_inset(o->pad_right);}
inline int32_t lv_obj_get_content_height(const lv_obj_t*o){if(!o)return 0;return o->height-lv_shim_style_inset(o->border_width)-lv_shim_style_inset(o->pad_top)-lv_shim_style_inset(o->pad_bottom);}
inline void lv_obj_set_style_bg_color(lv_obj_t*o,lv_color_t c,int){if(o)o->color=c.value;} inline void lv_obj_set_style_text_color(lv_obj_t*,lv_color_t,int){} inline void lv_obj_set_style_border_width(lv_obj_t*o,int v,int){if(o)o->border_width=v;} inline void lv_obj_set_style_border_color(lv_obj_t*,lv_color_t,int){}
/* `lv_obj_set_style_pad_all()` sets the four sides only, exactly as LVGL 9 does
 * (it delegates to the per-side setters and leaves pad_row/pad_column alone). */
inline void lv_obj_set_style_pad_all(lv_obj_t*o,int v,int){if(o){o->pad_left=o->pad_right=o->pad_top=o->pad_bottom=v;}}
inline void lv_obj_set_style_pad_column(lv_obj_t*o,int v,int){if(o)o->pad_column=v;} inline void lv_obj_set_style_pad_row(lv_obj_t*o,int v,int){if(o)o->pad_row=v;} inline void lv_obj_set_style_pad_left(lv_obj_t*o,int v,int){if(o)o->pad_left=v;} inline void lv_obj_set_style_pad_right(lv_obj_t*o,int v,int){if(o)o->pad_right=v;} inline void lv_obj_set_style_pad_top(lv_obj_t*o,int v,int){if(o)o->pad_top=v;} inline void lv_obj_set_style_pad_bottom(lv_obj_t*o,int v,int){if(o)o->pad_bottom=v;} inline void lv_obj_set_style_bg_opa(lv_obj_t*,int,int){} inline void lv_obj_set_style_radius(lv_obj_t*,int,int){}
inline void lv_obj_set_style_text_align(lv_obj_t*o,int v,int){if(o)o->text_align=v;} inline void lv_obj_set_style_arc_width(lv_obj_t*,int,int){} inline void lv_obj_set_style_arc_color(lv_obj_t*o,lv_color_t c,int){if(o)o->color=c.value;} inline void lv_obj_set_style_arc_opa(lv_obj_t*,int,int){} inline void lv_obj_set_style_opa(lv_obj_t*,int,int){}
inline void lv_obj_set_style_text_font(lv_obj_t*o,const lv_font_t*f,int){if(o)o->text_font=f;}
inline void lv_obj_update_layout(lv_obj_t*){} inline void lv_obj_align(lv_obj_t*,int,int,int){} inline void lv_obj_add_state(lv_obj_t*,int){}
inline void lv_label_set_text(lv_obj_t*o,const char*t){if(o)o->text=t?t:"";} inline void lv_label_set_long_mode(lv_obj_t*o,int v){if(o)o->long_mode=v;}
inline void lv_textarea_set_one_line(lv_obj_t*,bool){} inline void lv_textarea_set_max_length(lv_obj_t*,std::uint32_t){} inline void lv_keyboard_set_textarea(lv_obj_t*,lv_obj_t*){}
inline void lv_arc_set_bg_angles(lv_obj_t*,int,int){} inline void lv_arc_set_angles(lv_obj_t*,int,int){}
inline void lv_obj_add_event_cb(lv_obj_t*o,lv_event_cb_t cb,int filter,void *user_data)
{if(o&&cb)o->event_callbacks.push_back({cb,filter,user_data});}
inline lv_obj_t *lv_event_get_target(lv_event_t *e){return e?e->target:nullptr;}
inline void *lv_event_get_user_data(lv_event_t *e){return e?e->user_data:nullptr;}
inline void lv_shim_emit_event(lv_obj_t *o, int filter)
{
    if (!o) return;
    for (const auto &entry : o->event_callbacks) {
        if (entry.filter == filter) {
            lv_event_t event{o, entry.user_data};
            entry.callback(&event);
        }
    }
}
inline lv_indev_t *&lv_shim_active_indev()
{
    static lv_indev_t *indev = nullptr;
    return indev;
}
inline lv_indev_t *lv_indev_active() { return lv_shim_active_indev(); }
inline int lv_indev_get_type(const lv_indev_t *indev) { return indev ? indev->type : 0; }
inline void lv_indev_get_point(const lv_indev_t *indev, lv_point_t *point)
{
    if (indev != nullptr && point != nullptr) *point = indev->point;
}
inline void lv_shim_set_pointer(lv_indev_t *indev, int32_t y)
{
    if (indev != nullptr) {
        indev->type = LV_INDEV_TYPE_POINTER;
        indev->point.y = y;
    }
    lv_shim_active_indev() = indev;
}
inline bool lv_font_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *glyph,
                                  std::uint32_t codepoint, std::uint32_t)
{
    if (!font || !glyph) return false;
    /* Deterministic monospaced approximation for the host harness: ASCII glyphs
     * are half the line height and non-ASCII glyphs occupy one full cell. */
    glyph->adv_w = codepoint < 0x80 ? std::max<int32_t>(1, font->line_height / 2)
                                    : std::max<int32_t>(1, font->line_height);
    return true;
}
inline int32_t lv_shim_text_width(const lv_font_t *font, const std::string &text)
{
    int32_t width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[i]);
        const std::size_t length = first < 0x80 ? 1 : first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : 2;
        lv_font_glyph_dsc_t glyph{};
        lv_font_get_glyph_dsc(font, &glyph, first < 0x80 ? first : 0x80, 0);
        width += glyph.adv_w;
        i += std::min(length, text.size() - i);
    }
    return width;
}
struct shim_async_call { void (*callback)(void*){}; void *parameter{}; };
inline std::deque<shim_async_call> &lv_shim_async_calls()
{
    static std::deque<shim_async_call> calls;
    return calls;
}
inline lv_result_t &lv_shim_next_async_result()
{
    static lv_result_t result = LV_RESULT_OK;
    return result;
}
inline void lv_shim_reset_async()
{
    lv_shim_async_calls().clear();
    lv_shim_next_async_result() = LV_RESULT_OK;
}
inline void lv_shim_set_next_async_result(lv_result_t result)
{
    lv_shim_next_async_result() = result;
}
inline std::size_t lv_shim_pending_async_calls()
{
    return lv_shim_async_calls().size();
}
inline bool lv_shim_run_one_async()
{
    if (lv_shim_async_calls().empty()) return false;
    const shim_async_call call = lv_shim_async_calls().front();
    lv_shim_async_calls().pop_front();
    if (call.callback != nullptr) call.callback(call.parameter);
    return true;
}
inline lv_result_t lv_async_call(void (*cb)(void*),void*p)
{
    const lv_result_t result = lv_shim_next_async_result();
    lv_shim_next_async_result() = LV_RESULT_OK;
    if (result == LV_RESULT_OK) lv_shim_async_calls().push_back({cb, p});
    return result;
}
