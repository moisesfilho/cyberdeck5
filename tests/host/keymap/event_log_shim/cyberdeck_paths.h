#pragma once
#include <cstdlib>
#include <string>
inline const char *event_log_shim_root() {
    const char *root = std::getenv("EVENT_LOG_TEST_ROOT");
    return root ? root : "/tmp/cyberdeck-event-log-host-unspecified";
}
inline std::string event_log_shim_logs() {
    return std::string(event_log_shim_root()) + "/logs";
}
inline std::string event_log_shim_binary_path() {
    return event_log_shim_logs() + "/events.log";
}
inline std::string event_log_shim_text_path() {
    return event_log_shim_logs() + "/events.txt";
}
#define CYBERDECK_CONFIG_DIR event_log_shim_root()
#define CYBERDECK_LOG_DIR event_log_shim_logs().c_str()
#define CYBERDECK_LOG_PATH event_log_shim_binary_path().c_str()
#define CYBERDECK_TEXT_LOG_PATH event_log_shim_text_path().c_str()
