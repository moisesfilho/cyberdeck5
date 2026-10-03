#pragma once

#include "apps/runtime/cyberdeck_window_manager.h"

struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;

namespace cyberdeck_window_manager_adapter {

class adapter final {
public:
    bool init();
    void deinit();
    lv_obj_t *screen() const { return screen_; }
    lv_obj_t *content() const { return content_; }
    lv_obj_t *system_bar() const { return system_bar_; }
    cyberdeck_window_manager::manager &policy() { return policy_; }
    bool ready() const { return screen_ != nullptr; }

private:
    cyberdeck_window_manager::manager policy_;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *system_bar_ = nullptr;
    lv_obj_t *content_ = nullptr;
};

adapter &global();

} // namespace cyberdeck_window_manager_adapter
