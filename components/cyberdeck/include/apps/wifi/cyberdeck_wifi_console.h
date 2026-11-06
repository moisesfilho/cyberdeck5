#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "apps/wifi/cyberdeck_wifi_survey.h"

namespace cyberdeck_wifi_console {

/* Wi-Fi console port.  Interactive menus, the saved-network picker, the audit
 * controller, the survey session and every render stay in the UI composition;
 * this layer only requests transitions and formats the legacy messages. */
struct port {
    struct status {
        bool available{false};
        bool enabled{false};
        bool connected{false};
        char ip[16]{};
    };

    enum class search_outcome { started, survey_active, disabled, scan_failed };
    enum class saved_outcome { opened, empty };
    enum class save_outcome { requested, unavailable };
    enum class survey_start_outcome { started, refused, disabled };
    enum class survey_action_outcome { applied, unavailable };

    virtual ~port() = default;

    virtual status current_status() = 0;
    /* Applies the survey and enable gates, starts the interactive scan and
     * owns the scan token; the menu itself is driven by the UI. */
    virtual search_outcome begin_search() = 0;
    /* Re-arms the saved list and hands it to the picker. */
    virtual saved_outcome open_saved() = 0;
    virtual void audit_begin() = 0;
    virtual save_outcome audit_save() = 0;
    virtual survey_start_outcome survey_start(std::uint32_t duration_seconds) = 0;
    virtual survey_action_outcome survey_cancel() = 0;
    virtual survey_action_outcome survey_export(bool confirm, bool masked) = 0;
};

/* Session-scoped survey duration.  The supervisor clears it when a new console
 * is created. */
struct state {
    std::uint32_t survey_duration_seconds = cyberdeck_wifi_survey::default_duration_seconds;
};

/* Handles `wifi` plus the `search`, `saved`, `audit [save]` and
 * `survey [duration|cancel|export]` subcommands. */
std::string execute(port &io, std::string_view args, state &current);

} // namespace cyberdeck_wifi_console
