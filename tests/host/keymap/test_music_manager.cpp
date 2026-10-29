#include "apps/music/cyberdeck_music_manager.h"

#include <cstdio>
#include <vector>

namespace {
struct fixture {
    std::vector<cyberdeck_music_manager::action> actions;
    std::vector<int> order;
    bool close_ok = true;
    static bool perform(cyberdeck_music_manager::action value, void *context) {
        auto &self = *static_cast<fixture *>(context);
        self.actions.push_back(value);
        self.order.push_back(1);
        return true;
    }
    static bool close(void *context) {
        auto &self = *static_cast<fixture *>(context);
        self.order.push_back(2);
        return self.close_ok;
    }
};
int failures = 0;
void check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
}

int main() {
    using namespace cyberdeck_music_manager;
    fixture state;
    manager input;
    input.bind(fixture::perform, fixture::close, &state);
    check(!input.handle(key::enter), "inactive input is not consumed");
    input.open();
    check(input.handle(key::enter) && input.handle(key::enter) && state.actions.empty(),
          "repeated Enter while the player view is opening is consumed without playback");
    check(input.activate(), "successfully created player view activates input");
    check(input.handle(key::enter) && input.handle(key::left) && input.handle(key::right) && input.handle(key::up) &&
              input.handle(key::down) && input.handle(key::next) && input.handle(key::previous),
          "active controls are consumed");
    check(state.actions.size() == 7, "active controls map to seven player actions");
    state.close_ok = false;
    check(!input.handle(key::escape) && input.active(), "failed close retains active foreground ownership");
    state.close_ok = true;
    check(input.handle(key::escape) && !input.active(), "successful close releases foreground ownership");
    check(state.order.size() >= 9 && state.order[state.order.size() - 1] == 2,
          "close adapter runs after player actions and controls release");
    const int before = static_cast<int>(state.order.size());
    check(!input.handle(key::escape) && static_cast<int>(state.order.size()) == before,
          "repeated inactive escape does not stop or switch twice");
    input.open();
    input.deactivate();
    const auto actions_before_failed_open = state.actions.size();
    check(!input.handle(key::enter) && state.actions.size() == actions_before_failed_open,
          "failed view creation leaves no active controls or playback");
    std::printf("music manager: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
