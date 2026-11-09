#include "apps/image/cyberdeck_image_navigation.h"

#include <cstring>

namespace cyberdeck_image {
namespace {

void copy_name(std::array<char, k_max_name_bytes + 1> &out, std::string_view value) {
    const std::size_t length = value.size() > k_max_name_bytes ? k_max_name_bytes : value.size();
    std::memcpy(out.data(), value.data(), length);
    out[length] = '\0';
}

} // namespace

completion_disposition completion_disposition_for(completion_origin origin, completion_outcome outcome,
                                                  bool current) noexcept {
    /* A superseded generation and a cancelled run are never diagnostics: the UI
     * already moved on (or is tearing down) and must not paint an old frame or
     * an old error. */
    if (!current)
        return completion_disposition::discard;
    switch (outcome) {
    case completion_outcome::cancelled:
        return completion_disposition::discard;
    case completion_outcome::ok:
        return completion_disposition::publish;
    case completion_outcome::failed:
        break;
    }
    return origin == completion_origin::navigation ? completion_disposition::recoverable_error
                                                   : completion_disposition::initial_error;
}

std::string viewer_chrome::position() const {
    return format_position(index, count, truncated);
}

viewer_chrome chrome_after_publish(std::string_view name, std::size_t index, std::size_t count, bool truncated) {
    viewer_chrome chrome{};
    copy_name(chrome.name, name);
    chrome.index = index;
    chrome.count = count;
    chrome.truncated = truncated;
    chrome.frame = true;
    return chrome;
}

viewer_chrome chrome_after_error(std::string_view name, std::size_t index, std::size_t count, bool truncated,
                                 std::string_view message) {
    viewer_chrome chrome{};
    copy_name(chrome.name, name);
    chrome.index = index;
    chrome.count = count;
    chrome.truncated = truncated;
    chrome.frame = false;
    chrome.message.assign(message.data(), message.size());
    return chrome;
}

viewer_chrome chrome_after_accept(const viewer_chrome &current, std::string_view name, std::size_t index,
                                  std::size_t count, bool truncated) {
    if (!current.has_error())
        return current;
    /* The error is gone the moment the next request is accepted: the indicator
     * follows the accepted target and no bitmap is claimed until the result
     * arrives (C9-ii). */
    viewer_chrome accepted = chrome_after_error(name, index, count, truncated, std::string_view{});
    accepted.frame = false;
    return accepted;
}

} // namespace cyberdeck_image
