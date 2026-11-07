/*
 * TEST-CONTROL-PROTOCOL: comportamento do canal HTTP de comandos.
 *
 * A producao usa mbedtls; aqui as primitivas sao implementadas no proprio teste
 * e validadas contra vetores NIST/RFC 4231 antes de exercitar o protocolo. Isso
 * prova a construcao da mensagem, o nonce de uso unico e a comparacao em tempo
 * constante sem depender do link ESP-IDF e sem duplicar cripto de producao.
 */
#include "apps/control/cyberdeck_control_protocol.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char *name)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", name);
    }
}

template <typename A, typename B> void check_eq(A actual, B expected, const char *name)
{
    ++g_checks;
    if (!(actual == expected)) {
        ++g_failures;
        std::printf("FAIL: %s\n", name);
    }
}

/* ---------------- SHA-256 / HMAC-SHA256 (referencia do teste) -------------- */

struct sha256 {
    std::uint32_t state[8];
    std::uint64_t bits;
    std::uint8_t block[64];
    std::size_t used;
};

constexpr std::uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t rotate_right(std::uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32 - bits));
}

void sha256_compress(sha256 &context, const std::uint8_t *block)
{
    std::uint32_t words[64];
    for (int i = 0; i < 16; ++i) {
        words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotate_right(words[i - 15], 7) ^ rotate_right(words[i - 15], 18) ^
                                 (words[i - 15] >> 3);
        const std::uint32_t s1 = rotate_right(words[i - 2], 17) ^ rotate_right(words[i - 2], 19) ^
                                 (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    std::uint32_t a = context.state[0];
    std::uint32_t b = context.state[1];
    std::uint32_t c = context.state[2];
    std::uint32_t d = context.state[3];
    std::uint32_t e = context.state[4];
    std::uint32_t f = context.state[5];
    std::uint32_t g = context.state[6];
    std::uint32_t h = context.state[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const std::uint32_t choice = (e & f) ^ ((~e) & g);
        const std::uint32_t temp1 = h + s1 + choice + kRound[i] + words[i];
        const std::uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    context.state[0] += a;
    context.state[1] += b;
    context.state[2] += c;
    context.state[3] += d;
    context.state[4] += e;
    context.state[5] += f;
    context.state[6] += g;
    context.state[7] += h;
}

void sha256_init(sha256 &context)
{
    context.state[0] = 0x6a09e667;
    context.state[1] = 0xbb67ae85;
    context.state[2] = 0x3c6ef372;
    context.state[3] = 0xa54ff53a;
    context.state[4] = 0x510e527f;
    context.state[5] = 0x9b05688c;
    context.state[6] = 0x1f83d9ab;
    context.state[7] = 0x5be0cd19;
    context.bits = 0;
    context.used = 0;
}

void sha256_update(sha256 &context, const std::uint8_t *data, std::size_t length)
{
    static const std::uint8_t kEmpty = 0;
    if (data == nullptr) data = &kEmpty;
    for (std::size_t i = 0; i < length; ++i) {
        context.block[context.used++] = data[i];
        if (context.used == sizeof(context.block)) {
            sha256_compress(context, context.block);
            context.bits += 512;
            context.used = 0;
        }
    }
}

void sha256_final(sha256 &context, std::uint8_t *out)
{
    std::uint64_t bits = context.bits + context.used * 8;
    std::uint8_t padding = 0x80;
    sha256_update(context, &padding, 1);
    std::uint8_t zero = 0;
    while (context.used != 56) sha256_update(context, &zero, 1);
    std::uint8_t size[8];
    for (int i = 0; i < 8; ++i) size[i] = static_cast<std::uint8_t>(bits >> (56 - i * 8));
    for (std::uint8_t byte : size) {
        context.block[context.used++] = byte;
        if (context.used == 64) {
            sha256_compress(context, context.block);
            context.used = 0;
        }
    }
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<std::uint8_t>(context.state[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(context.state[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(context.state[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(context.state[i]);
    }
}

bool test_digest(const std::uint8_t *data, std::size_t length, std::uint8_t *out)
{
    sha256 context;
    sha256_init(context);
    sha256_update(context, data, length);
    sha256_final(context, out);
    return true;
}

bool test_mac(const std::uint8_t *key, std::size_t key_length, const std::uint8_t *data,
              std::size_t length, std::uint8_t *out)
{
    std::uint8_t block[64];
    std::memset(block, 0, sizeof(block));
    if (key_length > sizeof(block)) return false;
    std::memcpy(block, key, key_length);
    std::uint8_t inner[64];
    std::uint8_t outer[64];
    for (std::size_t i = 0; i < sizeof(block); ++i) {
        inner[i] = static_cast<std::uint8_t>(block[i] ^ 0x36);
        outer[i] = static_cast<std::uint8_t>(block[i] ^ 0x5c);
    }
    sha256 context;
    sha256_init(context);
    sha256_update(context, inner, sizeof(inner));
    sha256_update(context, data, length);
    std::uint8_t digest[32];
    sha256_final(context, digest);
    sha256_init(context);
    sha256_update(context, outer, sizeof(outer));
    sha256_update(context, digest, sizeof(digest));
    sha256_final(context, out);
    return true;
}

std::string to_hex(const std::uint8_t *bytes, std::size_t length)
{
    static const char *digits = "0123456789abcdef";
    std::string result;
    for (std::size_t i = 0; i < length; ++i) {
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 0x0F]);
    }
    return result;
}

/* ------------------------------- vetores ---------------------------------- */

void test_crypto_vectors()
{
    std::uint8_t out[32];
    check(test_digest(nullptr, 0, out), "sha256 of empty input");
    check_eq(to_hex(out, sizeof(out)),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
             "sha256('') matches NIST");
    const auto abc = reinterpret_cast<const std::uint8_t *>("abc");
    check(test_digest(abc, 3, out), "sha256 of abc");
    check_eq(to_hex(out, sizeof(out)),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
             "sha256('abc') matches NIST");
    /* RFC 4231, caso 2: key "Jefe", data "what do ya want for nothing?". */
    const auto key = reinterpret_cast<const std::uint8_t *>("Jefe");
    const auto data = reinterpret_cast<const std::uint8_t *>("what do ya want for nothing?");
    check(test_mac(key, 4, data, 28, out), "hmac rfc4231 case 2");
    check_eq(to_hex(out, sizeof(out)),
             std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"),
             "hmac-sha256 matches RFC 4231");
}

void test_hex()
{
    const std::uint8_t bytes[3] = {0x00, 0x7f, 0xff};
    char text[7];
    check(cyberdeck_control::hex_encode(bytes, 3, text, sizeof(text)), "hex encode fits");
    check_eq(std::string(text), std::string("007fff"), "hex encode is lowercase canonical");
    std::uint8_t decoded[3];
    check(cyberdeck_control::hex_decode("007fff", 6, decoded, 3), "hex decode accepts canonical");
    check(decoded[0] == 0 && decoded[1] == 0x7f && decoded[2] == 0xff, "hex decode round-trips");
    check(!cyberdeck_control::hex_decode("007FFF", 6, decoded, 3), "hex decode rejects uppercase");
    check(!cyberdeck_control::hex_decode("007ff", 5, decoded, 3), "hex decode rejects odd length");
    check(!cyberdeck_control::hex_decode("zz7fff", 6, decoded, 3), "hex decode rejects non-hex");
    char too_small[3];
    check(!cyberdeck_control::hex_encode(bytes, 3, too_small, sizeof(too_small)),
          "hex encode fails closed on small buffer");

    const std::uint8_t a[4] = {1, 2, 3, 4};
    const std::uint8_t b[4] = {1, 2, 3, 4};
    const std::uint8_t c[4] = {1, 2, 3, 5};
    check(cyberdeck_control::constant_time_equal(a, b, 4), "constant time equal matches");
    check(!cyberdeck_control::constant_time_equal(a, c, 4), "constant time equal detects a difference");
    check(!cyberdeck_control::constant_time_equal(nullptr, b, 4), "constant time equal rejects null");
}

cyberdeck_control::error authenticate(cyberdeck_control::nonce_store &store,
                                      const std::uint8_t *key, const std::string &nonce,
                                      const std::string &mac, const char *method, const char *uri,
                                      const std::string &body, std::int64_t now)
{
    return cyberdeck_control::verify(
        store, key, cyberdeck_control::k_key_bytes, nonce.c_str(), mac.c_str(), method, uri,
        reinterpret_cast<const std::uint8_t *>(body.data()), body.size(), now, test_digest, test_mac);
}

struct signed_request {
    std::string nonce;
    std::string mac;
};

signed_request sign(const std::uint8_t *key, const std::string &nonce, const char *method,
                    const char *uri, const std::string &body)
{
    std::uint8_t digest[32];
    test_digest(reinterpret_cast<const std::uint8_t *>(body.data()), body.size(), digest);
    char digest_hex[65];
    cyberdeck_control::hex_encode(digest, sizeof(digest), digest_hex, sizeof(digest_hex));
    char message[200];
    cyberdeck_control::build_message(nonce.c_str(), method, uri, digest_hex, message, sizeof(message));
    std::uint8_t mac[32];
    test_mac(key, 32, reinterpret_cast<const std::uint8_t *>(message), std::strlen(message), mac);
    return {nonce, to_hex(mac, sizeof(mac))};
}

void test_nonce_store()
{
    cyberdeck_control::nonce_store store;
    std::uint8_t random[16] = {0};
    random[0] = 0xAB;
    char hex[33]{};
    check(store.issue(random, sizeof(random), 1000, hex, sizeof(hex)), "nonce issues");
    check_eq(std::string(hex).size(), std::size_t(32), "nonce hex is 32 chars");
    check(store.outstanding(), "nonce is outstanding after issue");

    bool expired = false;
    check(store.pending(hex, 1000, &expired) && !expired, "nonce is pending before deadline");
    check(store.pending(hex, 1000 + cyberdeck_control::k_nonce_ttl_us - 1, &expired),
          "nonce is valid up to the deadline");
    expired = false;
    check(!store.pending(hex, 1000 + cyberdeck_control::k_nonce_ttl_us, &expired),
          "nonce rejects at the deadline");
    check(expired, "nonce reports expiry at the deadline");

    check(store.issue(random, sizeof(random), 1000, hex, sizeof(hex)), "nonce reissues");
    check(store.consume(hex, 1000), "nonce consumes once");
    check(!store.consume(hex, 1000), "nonce cannot be consumed twice");
    check(!store.pending(hex, 1000, &expired), "consumed nonce is no longer pending");

    check(store.issue(random, sizeof(random), 1000, hex, sizeof(hex)), "nonce reissues again");
    char other[33];
    std::uint8_t different[16] = {0};
    different[0] = 0xCD;
    check(cyberdeck_control::hex_encode(different, sizeof(different), other, sizeof(other)),
          "other nonce encoded");
    check(!store.pending(other, 1000, &expired), "nonce mismatch is not pending");
    check(!store.consume(other, 1000), "nonce mismatch does not consume");

    store.reset();
    check(!store.outstanding(), "reset clears the nonce");
}

void test_verify_flow()
{
    const std::uint8_t key[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                  17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
    cyberdeck_control::nonce_store store;
    std::uint8_t random[16] = {0};
    random[15] = 0x42;
    char nonce_hex[33]{};
    const std::int64_t now = 5'000'000;
    check(store.issue(random, sizeof(random), now, nonce_hex, sizeof(nonce_hex)), "challenge issued");
    const std::string nonce(nonce_hex);
    const std::string body = "help";
    const auto signed_body = sign(key, nonce, "POST", "/control/type", body);

    /* MAC invalido nao consome o desafio. */
    check(authenticate(store, key, nonce, std::string(64, '0'), "POST", "/control/type", body, now) ==
              cyberdeck_control::error::unauthorized,
          "bad mac is rejected");
    check(store.pending(nonce_hex, now, nullptr), "bad mac keeps the challenge alive");
    check(authenticate(store, key, nonce, signed_body.mac, "POST", "/control/type", body + "!",
                       now) == cyberdeck_control::error::unauthorized,
          "tampered body is rejected");
    check(authenticate(store, key, nonce, signed_body.mac, "POST", "/control/terminal", body, now) ==
              cyberdeck_control::error::unauthorized,
          "tampered uri is rejected");
    check(authenticate(store, key, nonce, signed_body.mac, "GET", "/control/type", body, now) ==
              cyberdeck_control::error::unauthorized,
          "tampered method is rejected");

    check(authenticate(store, key, nonce, signed_body.mac, "POST", "/control/type", body, now) ==
              cyberdeck_control::error::none,
          "authenticated request is accepted");
    check(!store.pending(nonce_hex, now, nullptr), "accepted request consumes the challenge");
    check(authenticate(store, key, nonce, signed_body.mac, "POST", "/control/type", body, now) ==
              cyberdeck_control::error::no_challenge,
          "replay is rejected");

    /* Expiracao e nonce desconhecido. */
    check(store.issue(random, sizeof(random), now, nonce_hex, sizeof(nonce_hex)), "challenge reissued");
    const std::string fresh(nonce_hex);
    const auto fresh_signed = sign(key, fresh, "GET", "/control/terminal", "");
    check(authenticate(store, key, fresh, fresh_signed.mac, "GET", "/control/terminal", "",
                       now + cyberdeck_control::k_nonce_ttl_us) == cyberdeck_control::error::expired,
          "expired challenge is rejected");
    check(authenticate(store, key, std::string(32, 'a'), fresh_signed.mac, "GET", "/control/terminal",
                       "", now) == cyberdeck_control::error::no_challenge,
          "unknown challenge is rejected");
    check(authenticate(store, key, "short", fresh_signed.mac, "GET", "/control/terminal", "",
                       now) == cyberdeck_control::error::unauthorized,
          "malformed nonce is unauthorized");

    /* Chave indisponivel. */
    check(authenticate(store, nullptr, fresh, fresh_signed.mac, "GET", "/control/terminal", "",
                       now) == cyberdeck_control::error::not_ready,
          "missing key is not_ready");

    /* Mesma chave, MAC de outra chave. */
    check(store.issue(random, sizeof(random), now, nonce_hex, sizeof(nonce_hex)), "challenge reissued");
    const std::string last(nonce_hex);
    std::uint8_t other_key[32];
    std::memcpy(other_key, key, sizeof(other_key));
    other_key[0] ^= 0xFF;
    const auto foreign = sign(other_key, last, "POST", "/control/type", body);
    check(authenticate(store, key, last, foreign.mac, "POST", "/control/type", body, now) ==
              cyberdeck_control::error::unauthorized,
          "mac from another key is rejected");

    char message[200];
    check(cyberdeck_control::build_message("n", "POST", "/control/type", "d", message,
                                           sizeof(message)),
          "message builds");
    check_eq(std::string(message), std::string("n\nPOST\n/control/type\nd"), "message layout is pinned");
    check(!cyberdeck_control::build_message("n", "POST", "/control/type", "d", message, 8),
          "message fails closed when the buffer is too small");
}

void test_utf8()
{
    const auto check_text = [](const char *text, std::size_t length, bool expected, const char *name) {
        check_eq(cyberdeck_control::valid_utf8(text, length), expected, name);
    };
    check_text("", 0, true, "utf8 empty is valid");
    check_text("abc", 3, true, "utf8 ascii is valid");
    check_text("\xc3\xa9", 2, true, "utf8 2-byte is valid");
    check_text("\xe2\x82\xac", 3, true, "utf8 3-byte is valid");
    check_text("\xf0\x9f\x98\x80", 4, true, "utf8 4-byte is valid");
    check_text("\xc3", 1, false, "utf8 truncated sequence is invalid");
    check_text("\xc0\xaf", 2, false, "utf8 overlong is invalid");
    check_text("\xed\xa0\x80", 3, false, "utf8 surrogate is invalid");
    check_text("\xf4\x90\x80\x80", 4, false, "utf8 above U+10FFFF is invalid");
    check_text("\x80", 1, false, "utf8 stray continuation is invalid");
}

void test_plan_text()
{
    using namespace cyberdeck_control;

    {
        char text[] = "pwd";
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text, 3, &normalized, &plan), error::none, "plan single line");
        check_eq(plan.count, std::size_t(1), "plan single line count");
        check_eq(plan.lines[0].size, std::size_t(3), "plan single line size");
        check_eq(std::string(plan.lines[0].data, plan.lines[0].size), std::string("pwd"),
                 "plan single line content");
        check_eq(normalized, std::size_t(3), "plan single line normalized length");
    }
    {
        char text[] = "help\nwifi";
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text, 9, &normalized, &plan), error::none, "plan two lines");
        check_eq(plan.count, std::size_t(2), "plan two lines count");
        check_eq(std::string(plan.lines[1].data, plan.lines[1].size), std::string("wifi"),
                 "plan second line content");
    }
    {
        char text[] = "help\n";
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text, 5, &normalized, &plan), error::none, "trailing newline accepted");
        check_eq(plan.count, std::size_t(1), "trailing newline does not add an empty line");
    }
    {
        char text[] = "help\r\nwifi\r\n";
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text, 12, &normalized, &plan), error::none, "crlf accepted");
        check_eq(plan.count, std::size_t(2), "crlf produces two lines");
        check_eq(normalized, std::size_t(10), "crlf is normalized to lf");
        check_eq(std::string(plan.lines[0].data, plan.lines[0].size), std::string("help"),
                 "crlf first line content");
    }
    {
        char text[] = "a\rb";
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text, 3, &normalized, &plan), error::none, "lone cr accepted");
        check_eq(plan.count, std::size_t(2), "lone cr splits lines");
    }
    {
        char text[] = "\n";
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text, 1, &normalized, &plan), error::none, "bare newline accepted");
        check_eq(plan.count, std::size_t(1), "bare newline is one empty line");
        check_eq(plan.lines[0].size, std::size_t(0), "bare newline line is empty");
    }
    {
        char text[] = "";
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text, 0, &normalized, &plan), error::empty, "empty text is rejected");
    }
    {
        std::vector<char> text(k_max_text_bytes + 1, 'a');
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text.data(), text.size(), &normalized, &plan), error::too_large,
                 "oversized text is rejected");
        check_eq(plan_text(text.data(), k_max_text_bytes, &normalized, &plan), error::none,
                 "maximum text size is accepted");
    }
    {
        char text[] = "a\xff";
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text, 2, &normalized, &plan), error::invalid_utf8,
                 "invalid utf8 is rejected");
    }
    {
        char text[] = "a\0b";
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text, 3, &normalized, &plan), error::invalid_utf8,
                 "embedded NUL is rejected");
    }
    {
        std::string many;
        for (std::size_t i = 0; i < k_max_lines + 1; ++i) many += "a\n";
        std::vector<char> text(many.begin(), many.end());
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text.data(), text.size(), &normalized, &plan), error::too_many_lines,
                 "too many lines is rejected");
    }
    {
        char text[] = "caf\xc3\xa9";
        std::size_t normalized = 0;
        text_plan plan{};
        check_eq(plan_text(text, 5, &normalized, &plan), error::none, "utf8 text accepted");
        check_eq(plan.lines[0].size, std::size_t(5), "utf8 text keeps all bytes");
    }
}

void test_json()
{
    using namespace cyberdeck_control;
    char out[64];
    check_eq(json_escape("plain", 5, out, sizeof(out)), std::size_t(5), "json escape plain length");
    check_eq(std::string(out), std::string("plain"), "json escape plain content");
    check_eq(json_escape("a\"b\\c", 5, out, sizeof(out)), std::size_t(7), "json escape quotes length");
    check_eq(std::string(out), std::string("a\\\"b\\\\c"), "json escape quotes content");
    check_eq(json_escape("\n\t\r", 3, out, sizeof(out)), std::size_t(6), "json escape controls length");
    check_eq(std::string(out), std::string("\\n\\t\\r"), "json escape controls content");
    check_eq(json_escape("\x01", 1, out, sizeof(out)), std::size_t(6), "json escape low control length");
    check_eq(std::string(out), std::string("\\u0001"), "json escape low control content");
    check_eq(json_escape("caf\xc3\xa9", 5, out, sizeof(out)), std::size_t(5),
             "json escape keeps utf8 bytes");
    check_eq(json_escape("abcdef", 6, out, 4), std::size_t(0), "json escape fails closed on small buffer");
    check_eq(json_escape("", 0, out, sizeof(out)), std::size_t(0), "json escape empty writes nothing");

    char json[64];
    check(format_terminal_json(json, sizeof(json), "ok> hi", 6, false), "terminal json formats");
    check_eq(std::string(json), std::string("{\"text\":\"ok> hi\",\"bytes\":6,\"truncated\":false}"),
             "terminal json layout is pinned");
    check(format_terminal_json(json, sizeof(json), "a", 1, true), "terminal json truncated formats");
    check_eq(std::string(json), std::string("{\"text\":\"a\",\"bytes\":1,\"truncated\":true}"),
             "terminal json truncated layout");
    check(!format_terminal_json(json, 8, "abcdefghij", 10, false),
          "terminal json fails closed when it does not fit");
}

void test_error_mapping()
{
    using namespace cyberdeck_control;
    check_eq(std::string(error_code(error::none)), std::string("none"), "error code none");
    check_eq(std::string(error_code(error::unauthorized)), std::string("unauthorized"),
             "error code unauthorized");
    check_eq(std::string(error_code(error::too_many_lines)), std::string("too_many_lines"),
             "error code too_many_lines");
    check_eq(http_status(error::none), 200, "http status none");
    check_eq(http_status(error::unauthorized), 401, "http status unauthorized");
    check_eq(http_status(error::too_large), 413, "http status too_large");
    check_eq(http_status(error::busy), 429, "http status busy");
    check_eq(http_status(error::screen_off), 409, "http status screen_off");
    check_eq(http_status(error::not_ready), 503, "http status not_ready");
}

} // namespace

int main()
{
    test_crypto_vectors();
    test_hex();
    test_nonce_store();
    test_verify_flow();
    test_utf8();
    test_plan_text();
    test_json();
    test_error_mapping();
    if (g_failures != 0) {
        std::printf("FAIL: %d/%d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("PASS: control protocol (%d checks)\n", g_checks);
    return 0;
}
