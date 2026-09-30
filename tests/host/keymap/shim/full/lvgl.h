#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

struct lv_color_t { std::uint32_t value{}; };
struct _lv_obj_t {
    _lv_obj_t *parent{}; std::vector<_lv_obj_t *> children; std::string text;
    int32_t width{}, height{}, x{}, y{}; bool hidden{}; std::uint32_t color{};
};
using lv_obj_t = _lv_obj_t;
struct lv_event_t { lv_obj_t *target{}; };
using lv_event_cb_t = void (*)(lv_event_t *);
using lv_result_t = int;
inline constexpr lv_result_t LV_RESULT_OK = 0, LV_RESULT_INVALID = -1;
inline constexpr int LV_DIR_NONE=0, LV_DIR_ALL=1, LV_SCROLLBAR_MODE_OFF=0,
    LV_LAYOUT_FLEX=1, LV_LAYOUT_NONE=0, LV_FLEX_FLOW_ROW=0, LV_FLEX_ALIGN_START=0,
    LV_FLEX_ALIGN_CENTER=1, LV_FLEX_ALIGN_END=2, LV_TEXT_ALIGN_CENTER=1,
    LV_PART_MAIN=0, LV_PART_INDICATOR=1, LV_PART_KNOB=2, LV_OPA_TRANSP=0,
    LV_OPA_COVER=255, LV_RADIUS_CIRCLE=999, LV_EVENT_SIZE_CHANGED=1,
    LV_EVENT_FOCUSED=2, LV_EVENT_INSERT=3, LV_EVENT_VALUE_CHANGED=4,
    LV_EVENT_KEY=5;
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
inline void lv_obj_set_size(lv_obj_t *o,int32_t w,int32_t h){if(o){o->width=w;o->height=h;}}
inline void lv_obj_set_width(lv_obj_t *o,int32_t w){if(o)o->width=w;} inline void lv_obj_set_height(lv_obj_t *o,int32_t h){if(o)o->height=h;}
inline int32_t lv_obj_get_width(lv_obj_t *o){return o?o->width:0;} inline lv_obj_t *lv_obj_get_child(lv_obj_t *o,int i){return o&&i>=0&&i<(int)o->children.size()?o->children[i]:nullptr;}
inline void lv_obj_set_x(lv_obj_t*o,int32_t v){if(o)o->x=v;} inline void lv_obj_set_y(lv_obj_t*o,int32_t v){if(o)o->y=v;}
inline void lv_obj_set_hidden(lv_obj_t*o,bool v){if(o)o->hidden=v;} inline bool lv_obj_has_flag(lv_obj_t*o,int){return o&&o->hidden;}
inline void lv_obj_set_layout(lv_obj_t*,int){} inline void lv_obj_set_flex_flow(lv_obj_t*,int){} inline void lv_obj_set_flex_align(lv_obj_t*,int,int,int){} inline void lv_obj_set_flex_grow(lv_obj_t*,int){}
inline void lv_obj_set_scroll_dir(lv_obj_t*,int){} inline void lv_obj_set_scroll_chain(lv_obj_t*,bool){} inline void lv_obj_set_scrollbar_mode(lv_obj_t*,int){} inline void lv_obj_set_scrollable(lv_obj_t*,bool){} inline void lv_obj_set_overflow_visible(lv_obj_t*,bool){}
inline void lv_obj_set_style_bg_color(lv_obj_t*o,lv_color_t c,int){if(o)o->color=c.value;} inline void lv_obj_set_style_text_color(lv_obj_t*,lv_color_t,int){} inline void lv_obj_set_style_border_width(lv_obj_t*,int,int){} inline void lv_obj_set_style_border_color(lv_obj_t*,lv_color_t,int){}
inline void lv_obj_set_style_pad_all(lv_obj_t*,int,int){} inline void lv_obj_set_style_pad_column(lv_obj_t*,int,int){} inline void lv_obj_set_style_pad_row(lv_obj_t*,int,int){} inline void lv_obj_set_style_pad_left(lv_obj_t*,int,int){} inline void lv_obj_set_style_pad_right(lv_obj_t*,int,int){} inline void lv_obj_set_style_bg_opa(lv_obj_t*,int,int){} inline void lv_obj_set_style_radius(lv_obj_t*,int,int){}
inline void lv_obj_set_style_text_align(lv_obj_t*,int,int){} inline void lv_obj_set_style_arc_width(lv_obj_t*,int,int){} inline void lv_obj_set_style_arc_color(lv_obj_t*o,lv_color_t c,int){if(o)o->color=c.value;} inline void lv_obj_set_style_arc_opa(lv_obj_t*,int,int){} inline void lv_obj_set_style_opa(lv_obj_t*,int,int){}
inline void lv_obj_update_layout(lv_obj_t*){} inline void lv_obj_align(lv_obj_t*,int,int,int){} inline void lv_obj_add_state(lv_obj_t*,int){}
inline void lv_label_set_text(lv_obj_t*o,const char*t){if(o)o->text=t?t:"";} inline void lv_label_set_long_mode(lv_obj_t*,int){}
inline void lv_textarea_set_one_line(lv_obj_t*,bool){} inline void lv_textarea_set_max_length(lv_obj_t*,std::uint32_t){} inline void lv_keyboard_set_textarea(lv_obj_t*,lv_obj_t*){}
inline void lv_arc_set_bg_angles(lv_obj_t*,int,int){} inline void lv_arc_set_angles(lv_obj_t*,int,int){}
inline void lv_obj_add_event_cb(lv_obj_t*,lv_event_cb_t,int,void*){}
inline lv_obj_t *lv_event_get_target(lv_event_t *e){return e?e->target:nullptr;}
inline lv_result_t lv_async_call(void (*cb)(void*),void*p){cb(p);return LV_RESULT_OK;}
