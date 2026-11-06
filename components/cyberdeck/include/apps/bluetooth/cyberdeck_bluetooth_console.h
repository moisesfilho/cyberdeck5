#pragma once

#include <string>
#include <string_view>

namespace cyberdeck_bluetooth_console {

/* Bluetooth console port.  The scan list, the state machine, the transient
 * ownership rules and every render stay in the UI composition; this layer only
 * requests the transition and reports the legacy message. */
struct port {
    virtual ~port() = default;
    /* Clears the notice and the previous list, marks the transient output as
     * uncommitted, starts the interactive search and submits its actions. */
    virtual void start_search() = 0;
    /* Re-arms the restored bond list and unblocks the background cycle, which
     * is what makes Enter on a listed bond reconnect. */
    virtual void open_paired() = 0;
};

/* Handles `bluetooth search` and `bluetooth paired`. */
std::string execute(port &io, std::string_view args);

} // namespace cyberdeck_bluetooth_console
