#include "apps/control/cyberdeck_control_protocol.h"

#include <cstdio>
#include <cstring>

namespace cyberdeck_control {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

void wipe(std::uint8_t *buffer, std::size_t length)
{
    volatile std::uint8_t *bytes = buffer;
    for (std::size_t i = 0; i < length; ++i) bytes[i] = 0;
}

} // namespace

const char *error_code(error value)
{
    switch (value) {
    case error::none: return "none";
    case error::not_ready: return "not_ready";
    case error::no_challenge: return "no_challenge";
    case error::expired: return "expired";
    case error::unauthorized: return "unauthorized";
    case error::too_large: return "too_large";
    case error::empty: return "empty";
    case error::invalid_utf8: return "invalid_utf8";
    case error::too_many_lines: return "too_many_lines";
    case error::busy: return "busy";
    case error::screen_off: return "screen_off";
    case error::display_busy: return "display_busy";
    case error::invalid_request: return "invalid_request";
    case error::invalid_path: return "invalid_path";
    case error::conflict: return "conflict";
    case error::checksum: return "checksum";
    case error::storage_unavailable: return "storage_unavailable";
    case error::io_error: return "io_error";
    case error::internal: return "internal";
    }
    return "internal";
}

int http_status(error value)
{
    switch (value) {
    case error::none: return 200;
    case error::not_ready: return 503;
    case error::no_challenge:
    case error::expired:
    case error::unauthorized: return 401;
    case error::too_large: return 413;
    case error::empty:
    case error::invalid_utf8:
    case error::too_many_lines: return 400;
    case error::busy: return 429;
    case error::screen_off: return 409;
    case error::display_busy: return 503;
    case error::invalid_request:
    case error::invalid_path: return 400;
    case error::conflict: return 409;
    case error::checksum: return 422;
    case error::storage_unavailable: return 503;
    case error::io_error: return 500;
    case error::internal: return 500;
    }
    return 500;
}

bool hex_encode(const std::uint8_t *bytes, std::size_t length, char *out, std::size_t capacity)
{
    if (out == nullptr || capacity == 0) return false;
    if (bytes == nullptr && length != 0) return false;
    if (length > (capacity - 1) / 2) return false;
    for (std::size_t i = 0; i < length; ++i) {
        out[i * 2] = kHexDigits[bytes[i] >> 4];
        out[i * 2 + 1] = kHexDigits[bytes[i] & 0x0F];
    }
    out[length * 2] = '\0';
    return true;
}

bool hex_decode(const char *text, std::size_t length, std::uint8_t *out, std::size_t out_length)
{
    if (text == nullptr || (out == nullptr && out_length != 0)) return false;
    if (length != out_length * 2) return false;
    for (std::size_t i = 0; i < out_length; ++i) {
        const int high = hex_value(text[i * 2]);
        const int low = hex_value(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

bool constant_time_equal(const std::uint8_t *a, const std::uint8_t *b, std::size_t length)
{
    if (a == nullptr || b == nullptr) return false;
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < length; ++i) difference |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    return difference == 0;
}

void nonce_store::reset()
{
    wipe(value_, sizeof(value_));
    deadline_us_ = 0;
    valid_ = false;
}

bool nonce_store::issue(const std::uint8_t *random, std::size_t length, std::int64_t now_us,
                        char *hex, std::size_t capacity)
{
    reset();
    if (random == nullptr || length != k_nonce_bytes || now_us <= 0) return false;
    if (!hex_encode(random, length, hex, capacity)) return false;
    std::memcpy(value_, random, sizeof(value_));
    deadline_us_ = now_us + k_nonce_ttl_us;
    valid_ = true;
    return true;
}

bool nonce_store::pending(const char *hex, std::int64_t now_us, bool *expired) const
{
    if (expired != nullptr) *expired = false;
    if (!valid_ || hex == nullptr) return false;
    std::uint8_t candidate[k_nonce_bytes]{};
    const bool decoded = std::strlen(hex) == k_nonce_hex_chars &&
                         hex_decode(hex, k_nonce_hex_chars, candidate, sizeof(candidate));
    const bool match = decoded && constant_time_equal(candidate, value_, sizeof(value_));
    wipe(candidate, sizeof(candidate));
    if (!match) return false;
    if (now_us >= deadline_us_) {
        if (expired != nullptr) *expired = true;
        return false;
    }
    return true;
}

bool nonce_store::consume(const char *hex, std::int64_t now_us)
{
    if (!pending(hex, now_us, nullptr)) return false;
    reset();
    return true;
}

bool build_message(const char *nonce_hex, const char *method, const char *uri,
                   const char *body_digest_hex, char *out, std::size_t capacity)
{
    if (nonce_hex == nullptr || method == nullptr || uri == nullptr || body_digest_hex == nullptr ||
        out == nullptr || capacity == 0)
        return false;
    const int written = std::snprintf(out, capacity, "%s\n%s\n%s\n%s", nonce_hex, method, uri,
                                      body_digest_hex);
    return written > 0 && static_cast<std::size_t>(written) < capacity;
}

bool build_upload_message(const char *nonce_hex, const char *method, const char *uri,
                          const char *name, std::uint64_t declared_size,
                          const char *body_digest_hex, char *out, std::size_t capacity)
{
    if (nonce_hex == nullptr || method == nullptr || uri == nullptr || name == nullptr ||
        body_digest_hex == nullptr || out == nullptr || capacity == 0)
        return false;
    const int written = std::snprintf(out, capacity, "%s\n%s\n%s\n%s\n%s\n%llu\n%s", k_upload_domain,
                                      nonce_hex, method, uri, name,
                                      static_cast<unsigned long long>(declared_size),
                                      body_digest_hex);
    return written > 0 && static_cast<std::size_t>(written) < capacity;
}

error verify(nonce_store &store, const std::uint8_t *key, std::size_t key_length,
             const char *nonce_hex, const char *mac_hex, const char *method, const char *uri,
             const std::uint8_t *body, std::size_t body_length, std::int64_t now_us,
             digest_fn digest, mac_fn mac)
{
    if (key == nullptr || key_length != k_key_bytes || digest == nullptr || mac == nullptr)
        return error::not_ready;
    if (body == nullptr && body_length != 0) return error::internal;
    if (nonce_hex == nullptr || mac_hex == nullptr || method == nullptr || uri == nullptr)
        return error::unauthorized;
    if (std::strlen(nonce_hex) != k_nonce_hex_chars || std::strlen(mac_hex) != k_mac_hex_chars)
        return error::unauthorized;

    bool expired = false;
    if (!store.pending(nonce_hex, now_us, &expired)) {
        if (expired) {
            store.reset();
            return error::expired;
        }
        return error::no_challenge;
    }

    std::uint8_t body_digest[k_mac_bytes]{};
    if (!digest(body, body_length, body_digest)) return error::internal;
    char digest_hex[k_mac_hex_chars + 1]{};
    if (!hex_encode(body_digest, sizeof(body_digest), digest_hex, sizeof(digest_hex))) {
        wipe(body_digest, sizeof(body_digest));
        return error::internal;
    }
    wipe(body_digest, sizeof(body_digest));

    char message[k_max_mac_message_bytes]{};
    if (!build_message(nonce_hex, method, uri, digest_hex, message, sizeof(message)))
        return error::internal;

    std::uint8_t expected[k_mac_bytes]{};
    std::uint8_t provided[k_mac_bytes]{};
    const bool computed = mac(key, key_length, reinterpret_cast<const std::uint8_t *>(message),
                              std::strlen(message), expected);
    const bool decoded = hex_decode(mac_hex, k_mac_hex_chars, provided, sizeof(provided));
    const bool valid = computed && decoded && constant_time_equal(expected, provided, sizeof(expected));
    wipe(expected, sizeof(expected));
    wipe(provided, sizeof(provided));
    wipe(reinterpret_cast<std::uint8_t *>(message), sizeof(message));
    if (!valid) return error::unauthorized;
    if (!store.consume(nonce_hex, now_us)) return error::no_challenge;
    return error::none;
}

error verify_upload(nonce_store &store, const std::uint8_t *key, std::size_t key_length,
                    const char *nonce_hex, const char *mac_hex, const char *name,
                    std::uint64_t declared_size, const char *body_digest_hex,
                    std::int64_t now_us, mac_fn mac)
{
    if (key == nullptr || key_length != k_key_bytes || mac == nullptr) return error::not_ready;
    if (nonce_hex == nullptr || mac_hex == nullptr || body_digest_hex == nullptr ||
        name == nullptr)
        return error::unauthorized;
    if (std::strlen(nonce_hex) != k_nonce_hex_chars || std::strlen(mac_hex) != k_mac_hex_chars ||
        std::strlen(body_digest_hex) != k_mac_hex_chars)
        return error::unauthorized;

    /* O digest vem do cliente; exigi-lo canonico evita autenticar duas
     * grafias do mesmo valor e mantem a mensagem deterministica. */
    std::uint8_t digest_bytes[k_mac_bytes]{};
    if (!hex_decode(body_digest_hex, k_mac_hex_chars, digest_bytes, sizeof(digest_bytes))) {
        wipe(digest_bytes, sizeof(digest_bytes));
        return error::unauthorized;
    }
    wipe(digest_bytes, sizeof(digest_bytes));

    bool expired = false;
    if (!store.pending(nonce_hex, now_us, &expired)) {
        if (expired) {
            store.reset();
            return error::expired;
        }
        return error::no_challenge;
    }

    char message[k_max_upload_message_bytes]{};
    if (!build_upload_message(nonce_hex, k_upload_method, k_upload_uri, name, declared_size,
                              body_digest_hex, message, sizeof(message))) {
        wipe(reinterpret_cast<std::uint8_t *>(message), sizeof(message));
        return error::internal;
    }

    std::uint8_t expected[k_mac_bytes]{};
    std::uint8_t provided[k_mac_bytes]{};
    const bool computed = mac(key, key_length, reinterpret_cast<const std::uint8_t *>(message),
                              std::strlen(message), expected);
    const bool decoded = hex_decode(mac_hex, k_mac_hex_chars, provided, sizeof(provided));
    const bool valid = computed && decoded && constant_time_equal(expected, provided, sizeof(expected));
    wipe(expected, sizeof(expected));
    wipe(provided, sizeof(provided));
    wipe(reinterpret_cast<std::uint8_t *>(message), sizeof(message));
    if (!valid) return error::unauthorized;
    if (!store.consume(nonce_hex, now_us)) return error::no_challenge;
    return error::none;
}

bool valid_utf8(const char *text, std::size_t length)
{
    if (text == nullptr) return false;
    std::size_t i = 0;
    while (i < length) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        std::uint32_t codepoint = 0;
        std::uint32_t minimum = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            codepoint = c & 0x1F;
            minimum = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            codepoint = c & 0x0F;
            minimum = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            codepoint = c & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (i + extra >= length) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if (codepoint < minimum || codepoint > 0x10FFFF) return false;
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return false;
        i += extra + 1;
    }
    return true;
}

error plan_text(char *text, std::size_t length, std::size_t *normalized_length, text_plan *out)
{
    if (out == nullptr) return error::internal;
    out->count = 0;
    if (normalized_length != nullptr) *normalized_length = 0;
    if (text == nullptr || length == 0) return error::empty;
    if (length > k_max_text_bytes) return error::too_large;

    std::size_t written = 0;
    for (std::size_t i = 0; i < length; ++i) {
        char value = text[i];
        /* NUL e UTF-8 valido, mas nao tem representacao na linha de edicao. */
        if (value == '\0') return error::invalid_utf8;
        if (value == '\r') {
            if (i + 1 < length && text[i + 1] == '\n') continue;
            value = '\n';
        }
        text[written++] = value;
    }
    if (!valid_utf8(text, written)) return error::invalid_utf8;

    std::size_t start = 0;
    for (std::size_t i = 0; i <= written; ++i) {
        if (i != written && text[i] != '\n') continue;
        if (out->count >= k_max_lines) return error::too_many_lines;
        out->lines[out->count].data = text + start;
        out->lines[out->count].size = i - start;
        ++out->count;
        start = i + 1;
    }
    if (out->count > 1 && out->lines[out->count - 1].size == 0) --out->count;
    if (normalized_length != nullptr) *normalized_length = written;
    return error::none;
}

std::size_t json_escape(const char *text, std::size_t length, char *out, std::size_t capacity)
{
    if (out == nullptr || capacity == 0) return 0;
    if (text == nullptr && length != 0) return 0;
    std::size_t written = 0;
    const auto put = [&](char value) {
        if (written + 1 >= capacity) return false;
        out[written++] = value;
        return true;
    };
    for (std::size_t i = 0; i < length; ++i) {
        const auto value = static_cast<unsigned char>(text[i]);
        switch (value) {
        case '"':
            if (!put('\\') || !put('"')) return 0;
            break;
        case '\\':
            if (!put('\\') || !put('\\')) return 0;
            break;
        case '\n':
            if (!put('\\') || !put('n')) return 0;
            break;
        case '\r':
            if (!put('\\') || !put('r')) return 0;
            break;
        case '\t':
            if (!put('\\') || !put('t')) return 0;
            break;
        default:
            if (value < 0x20 || value == 0x7F) {
                char escape[7];
                std::snprintf(escape, sizeof(escape), "\\u%04x", value);
                for (const char *digit = escape; *digit != '\0'; ++digit) {
                    if (!put(*digit)) return 0;
                }
            } else if (!put(static_cast<char>(value))) {
                return 0;
            }
            break;
        }
    }
    out[written] = '\0';
    return written;
}

bool format_terminal_json(char *out, std::size_t capacity, const char *text, std::size_t length,
                          bool truncated)
{
    if (out == nullptr || capacity == 0) return false;
    const int prefix = std::snprintf(out, capacity, "{\"text\":\"");
    if (prefix <= 0 || static_cast<std::size_t>(prefix) >= capacity) return false;
    const std::size_t used = static_cast<std::size_t>(prefix);
    const std::size_t escaped = json_escape(text, length, out + used, capacity - used);
    if (escaped == 0 && length != 0) return false;
    const std::size_t remaining = capacity - used - escaped;
    const int tail = std::snprintf(out + used + escaped, remaining,
                                   "\",\"bytes\":%zu,\"truncated\":%s}", length,
                                   truncated ? "true" : "false");
    return tail > 0 && static_cast<std::size_t>(tail) < remaining;
}

} // namespace cyberdeck_control
