#include "apps/demo/cyberdeck_demo_app.h"

namespace cyberdeck_apps {
namespace {

constexpr manifest k_manifest{
    "cyberdeck.demo",
    "Demo application",
    "0.1.0",
    "Proof application for the compiled-in runtime",
    "demo",
    {}, 0, {}, 0, 1000, "1", app_type::demo,
    {"demo"}, 1, 2048, 2, {"demo"}, 1,
};

} // namespace

const manifest &demo_application::get_manifest() const { return k_manifest; }

bool demo_application::start()
{
    if (running_) return false;
    running_ = true;
    return true;
}

bool demo_application::stop()
{
    if (!running_) return false;
    running_ = false;
    return true;
}

result demo_application::execute(std::string_view, std::string_view)
{
    if (!running_) return {result_status::rejected, "demo: application is not running\n"};
    return {result_status::handled, "demo: compiled-in application is running\n"};
}

} // namespace cyberdeck_apps
