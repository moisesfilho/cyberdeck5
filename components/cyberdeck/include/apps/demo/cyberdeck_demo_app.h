#pragma once

#include "apps/runtime/cyberdeck_app_runtime.h"

namespace cyberdeck_apps {

class demo_application final : public application {
public:
    const manifest &get_manifest() const override;
    bool start() override;
    bool stop() override;
    bool running() const override { return running_; }
    result execute(std::string_view command, std::string_view args) override;

private:
    bool running_ = false;
};

} // namespace cyberdeck_apps
