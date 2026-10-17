#pragma once

#include <cstdint>

namespace cyberdeck_wifi_survey_export {

enum class admission_result { accepted, missing_confirmation, expired, full, unavailable };
enum class completion_result { ignored, published, failed };

class port {
  public:
    virtual ~port() = default;
    virtual admission_result admit(std::uint32_t token, bool masked) = 0;
    virtual void commit(std::uint32_t token) = 0;
};

class controller {
  public:
    explicit controller(port &output) : output_(output) {}
    bool request_confirmation(bool masked, std::uint64_t deadline_ms);
    admission_result confirm(bool masked, std::uint64_t now_ms);
    bool started(std::uint32_t token);
    completion_result complete(std::uint32_t token, bool persisted);
    completion_result fail_admitted(std::uint32_t token);
    void cancel_confirmation();
    bool busy() const {
        return active_;
    }
    bool confirmation_pending() const {
        return confirmation_;
    }

  private:
    port &output_;
    std::uint32_t next_token_{0};
    std::uint32_t active_token_{0};
    std::uint64_t deadline_ms_{0};
    bool confirmation_{false};
    bool masked_{true};
    bool active_{false};
    bool started_{false};
};

} // namespace cyberdeck_wifi_survey_export
