#include "apps/wifi/cyberdeck_wifi_survey_export_controller.h"

namespace cyberdeck_wifi_survey_export {

bool controller::request_confirmation(bool masked, std::uint64_t deadline_ms) {
    if (active_)
        return false;
    confirmation_ = true;
    masked_ = masked;
    deadline_ms_ = deadline_ms;
    return true;
}

admission_result controller::confirm(bool masked, std::uint64_t now_ms) {
    if (!confirmation_)
        return admission_result::missing_confirmation;
    if (now_ms > deadline_ms_) {
        confirmation_ = false;
        return admission_result::expired;
    }
    if (masked != masked_) {
        confirmation_ = false;
        return admission_result::missing_confirmation;
    }
    if (active_)
        return admission_result::full;

    std::uint32_t token = ++next_token_;
    if (token == 0)
        token = ++next_token_;
    const admission_result result = output_.admit(token, masked_);
    if (result != admission_result::accepted)
        return result;
    active_ = true;
    started_ = false;
    active_token_ = token;
    confirmation_ = false;
    output_.commit(token);
    return result;
}

bool controller::started(std::uint32_t token) {
    if (!active_ || started_ || token != active_token_)
        return false;
    started_ = true;
    return true;
}

completion_result controller::complete(std::uint32_t token, bool persisted) {
    if (!active_ || !started_ || token != active_token_)
        return completion_result::ignored;
    active_ = false;
    started_ = false;
    active_token_ = 0;
    return persisted ? completion_result::published : completion_result::failed;
}

completion_result controller::fail_admitted(std::uint32_t token) {
    if (!active_ || token != active_token_)
        return completion_result::ignored;
    active_ = false;
    started_ = false;
    active_token_ = 0;
    return completion_result::failed;
}

void controller::cancel_confirmation() {
    if (!active_)
        confirmation_ = false;
}

} // namespace cyberdeck_wifi_survey_export
