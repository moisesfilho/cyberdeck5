#pragma once

#include "apps/editor/cyberdeck_editor_model.h"
#include "apps/runtime/cyberdeck_app_facades.h"
#include "apps/runtime/cyberdeck_app_runtime.h"

namespace cyberdeck_editor {

class application final : public cyberdeck_apps::application {
  public:
    const cyberdeck_apps::manifest &get_manifest() const override;
    bool start() override;
    bool stop() override;
    bool running() const override {
        return running_;
    }
    cyberdeck_apps::result execute(std::string_view command, std::string_view args) override;
    void bind_input(cyberdeck_apps::input_facade input, cyberdeck_window_manager::view_context context);
    void unbind_input();
    bool handle_key(key pressed, std::string_view character = {});
    bool handle_shortcut(char shortcut);
    bool close_requested() const {
        return close_confirmation_;
    }
    void request_close() {
        close_confirmation_ = true;
        save_diagnostic_.clear();
    }
    void cancel_close() {
        close_confirmation_ = false;
        save_diagnostic_.clear();
    }
    bool discard_and_close();
    bool save_current();
    std::string_view save_diagnostic() const {
        return save_diagnostic_;
    }
    bool search(std::string_view needle);
    const model &document() const {
        return document_;
    }
    model &document() {
        return document_;
    }

  private:
    bool running_ = false;
    model document_{};
    std::string pending_save_as_;
    cyberdeck_apps::input_facade input_{};
    cyberdeck_window_manager::view_context view_context_{};
    bool close_confirmation_ = false;
    std::string save_diagnostic_;
};

application &global_application();

} // namespace cyberdeck_editor
