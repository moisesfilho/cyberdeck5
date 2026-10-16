#pragma once

#include "apps/runtime/cyberdeck_app_runtime.h"

namespace cyberdeck_netdiag {

class application final : public cyberdeck_apps::application {
  public:
    const cyberdeck_apps::manifest &get_manifest() const override;
    bool start() override;
    bool stop() override;
    bool running() const override;
    cyberdeck_apps::result execute(std::string_view command, std::string_view args) override;

  private:
    bool running_ = false;
};

application &global_application();

} // namespace cyberdeck_netdiag
