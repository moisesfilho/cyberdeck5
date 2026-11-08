/*
 * TEST-UPLOAD-* (camada pura): mensagem autenticada do upload (dominio
 * separado), nonce/expiração/replay, validador de nome/tamanho, digest
 * incremental e a maquina de estados de publicacao com sinks injetados.
 *
 * A criptografia de producao vem do mbedtls; aqui o teste implementa SHA-256 e
 * HMAC-SHA256 de referencia (validados contra NIST/RFC 4231 no alvo
 * test_control_protocol) para nao duplicar cripto de producao.
 */
#include "apps/control/cyberdeck_control_protocol.h"
#include "apps/control/cyberdeck_control_upload.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char *name) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", name);
    }
}

template <typename A, typename B> void check_eq(A actual, B expected, const char *name) {
    ++g_checks;
    if (!(actual == expected)) {
        ++g_failures;
        std::printf("FAIL: %s\n", name);
    }
}

/* ------------------------------ SHA-256 ---------------------------------- */

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

std::uint32_t rotate_right(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

void sha256_compress(sha256 &context, const std::uint8_t *block) {
    std::uint32_t words[64];
    for (int i = 0; i < 16; ++i) {
        words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) | static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 =
            rotate_right(words[i - 15], 7) ^ rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
        const std::uint32_t s1 = rotate_right(words[i - 2], 17) ^ rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
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

void sha256_init(sha256 &context) {
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

void sha256_update(sha256 &context, const std::uint8_t *data, std::size_t length) {
    static const std::uint8_t kEmpty = 0;
    if (data == nullptr)
        data = &kEmpty;
    for (std::size_t i = 0; i < length; ++i) {
        context.block[context.used++] = data[i];
        if (context.used == sizeof(context.block)) {
            sha256_compress(context, context.block);
            context.bits += 512;
            context.used = 0;
        }
    }
}

void sha256_final(sha256 &context, std::uint8_t *out) {
    const std::uint64_t bits = context.bits + context.used * 8;
    std::uint8_t padding = 0x80;
    sha256_update(context, &padding, 1);
    std::uint8_t zero = 0;
    while (context.used != 56)
        sha256_update(context, &zero, 1);
    std::uint8_t size[8];
    for (int i = 0; i < 8; ++i)
        size[i] = static_cast<std::uint8_t>(bits >> (56 - i * 8));
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

bool reference_digest(const std::uint8_t *data, std::size_t length, std::uint8_t *out) {
    sha256 context;
    sha256_init(context);
    sha256_update(context, data, length);
    sha256_final(context, out);
    return true;
}

bool reference_mac(const std::uint8_t *key, std::size_t key_length, const std::uint8_t *data, std::size_t length,
                   std::uint8_t *out) {
    std::uint8_t block[64];
    std::memset(block, 0, sizeof(block));
    if (key_length > sizeof(block))
        return false;
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

std::string to_hex(const std::uint8_t *bytes, std::size_t length) {
    static const char *digits = "0123456789abcdef";
    std::string result;
    for (std::size_t i = 0; i < length; ++i) {
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 0x0F]);
    }
    return result;
}

std::string digest_hex(const std::vector<std::uint8_t> &data) {
    std::uint8_t digest[32];
    reference_digest(data.data(), data.size(), digest);
    return to_hex(digest, sizeof(digest));
}

/* ------------------------- upload message/auth ---------------------------- */

const std::uint8_t kKey[32] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                               17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

std::string sign_upload(const std::string &nonce, const std::string &name, std::uint64_t size,
                        const std::string &digest) {
    char message[cyberdeck_control::k_max_upload_message_bytes]{};
    if (!cyberdeck_control::build_upload_message(nonce.c_str(), "POST", cyberdeck_control::k_upload_uri, name.c_str(),
                                                 size, digest.c_str(), message, sizeof(message)))
        return {};
    std::uint8_t mac[32];
    reference_mac(kKey, sizeof(kKey), reinterpret_cast<const std::uint8_t *>(message), std::strlen(message), mac);
    return to_hex(mac, sizeof(mac));
}

void test_upload_message_layout() {
    using namespace cyberdeck_control;
    char message[k_max_upload_message_bytes]{};
    check(build_upload_message("nonce", "POST", k_upload_uri, "fixture.bmp", 42, "digest", message, sizeof(message)),
          "upload message builds");
    check_eq(std::string(message),
             std::string(k_upload_domain) + "\nnonce\nPOST\n/control/upload\nfixture.bmp\n42\ndigest",
             "upload message layout is pinned with the basename before size and digest");
    char other[k_max_upload_message_bytes]{};
    check(build_upload_message("nonce", "POST", k_upload_uri, "other.bmp", 42, "digest", other, sizeof(other)),
          "upload message builds for a different basename");
    check(std::string(message) != std::string(other), "the authenticated message binds the target basename");
    char small[16]{};
    check(!build_upload_message("nonce", "POST", k_upload_uri, "fixture.bmp", 42, "digest", small, sizeof(small)),
          "upload message fails closed on a small buffer");
    check(!build_upload_message(nullptr, "POST", k_upload_uri, "fixture.bmp", 42, "digest", message, sizeof(message)),
          "upload message rejects a null nonce");
    check(!build_upload_message("nonce", "POST", k_upload_uri, nullptr, 42, "digest", message, sizeof(message)),
          "upload message rejects a null basename");

    /* Capacidade derivada: o maior basename aceito pelo protocolo mais o maior
     * tamanho decimal de uint64 ainda cabem com NUL. Nenhum limite e repetido
     * como literal: tudo sai dos proprios headers. */
    const std::string longest_name = std::string(k_upload_name_capacity - 4, 'a') + ".bmp";
    check(build_upload_message("nonce", "POST", k_upload_uri, longest_name.c_str(), 18446744073709551615ULL,
                               std::string(64, 'f').c_str(), message, sizeof(message)),
          "the longest basename and size fit the derived capacity");
    check(std::strlen(message) + 1 <= sizeof(message), "the message stays NUL terminated");
    check(k_max_upload_message_bytes >= std::strlen(k_upload_domain) + 1 + k_nonce_hex_chars + 1 +
                                            std::strlen(k_upload_method) + 1 + std::strlen(k_upload_uri) + 1 +
                                            k_upload_name_capacity + 1 + k_upload_size_digits + 1 + k_mac_hex_chars + 1,
          "the derived capacity covers every documented field");

    /* A mensagem de 4 campos do canal nao coincide com a do upload: mesmo
     * nonce/URI/digest ainda produz MAC diferente. */
    char text_message[k_max_mac_message_bytes]{};
    check(build_message("nonce", "POST", k_upload_uri, "digest", text_message, sizeof(text_message)),
          "text message builds");
    check(std::string(message) != std::string(text_message), "upload and text messages are domain separated");
}

void test_verify_upload() {
    using namespace cyberdeck_control;
    nonce_store store;
    std::uint8_t random[k_nonce_bytes] = {0};
    random[0] = 0x5A;
    char nonce_hex[k_nonce_hex_chars + 1]{};
    const std::int64_t now = 10'000'000;
    check(store.issue(random, sizeof(random), now, nonce_hex, sizeof(nonce_hex)), "upload challenge issued");
    const std::string nonce(nonce_hex);
    const std::string name("fixture.bmp");
    const std::string digest(64, 'a');
    const std::uint64_t size = 2'764'854;

    check(verify_upload(store, kKey, sizeof(kKey), nonce.c_str(), std::string(64, '0').c_str(), name.c_str(), size,
                        digest.c_str(), now, reference_mac) == error::unauthorized,
          "upload rejects a bad mac");
    check(store.pending(nonce_hex, now, nullptr), "a bad upload mac keeps the challenge alive");

    const std::string good = sign_upload(nonce, name, size, digest);
    check(!good.empty(), "upload mac computed");
    check(verify_upload(store, kKey, sizeof(kKey), nonce.c_str(), good.c_str(), name.c_str(), size + 1, digest.c_str(),
                        now, reference_mac) == error::unauthorized,
          "upload rejects a tampered declared size");
    check(verify_upload(store, kKey, sizeof(kKey), nonce.c_str(), good.c_str(), "other.bmp", size, digest.c_str(), now,
                        reference_mac) == error::unauthorized,
          "upload rejects a basename that was not authenticated");
    check(verify_upload(store, kKey, sizeof(kKey), nonce.c_str(), good.c_str(), name.c_str(), size,
                        std::string(64, 'b').c_str(), now, reference_mac) == error::unauthorized,
          "upload rejects a tampered digest");
    check(store.pending(nonce_hex, now, nullptr), "tampered upload fields keep the challenge alive");

    /* Separacao de dominio: MAC sobre a mensagem de 4 campos nao valida. */
    char text_message[k_max_mac_message_bytes]{};
    check(build_message(nonce.c_str(), "POST", k_upload_uri, digest.c_str(), text_message, sizeof(text_message)),
          "text message for domain check builds");
    std::uint8_t text_mac[32];
    reference_mac(kKey, sizeof(kKey), reinterpret_cast<const std::uint8_t *>(text_message), std::strlen(text_message),
                  text_mac);
    check(verify_upload(store, kKey, sizeof(kKey), nonce.c_str(), to_hex(text_mac, 32).c_str(), name.c_str(), size,
                        digest.c_str(), now, reference_mac) == error::unauthorized,
          "a text-channel mac never authorizes an upload");

    check(verify_upload(store, kKey, sizeof(kKey), nonce.c_str(), good.c_str(), name.c_str(), size, digest.c_str(), now,
                        reference_mac) == error::none,
          "upload accepts a valid mac");
    check(!store.pending(nonce_hex, now, nullptr), "an accepted upload consumes the challenge");
    check(verify_upload(store, kKey, sizeof(kKey), nonce.c_str(), good.c_str(), name.c_str(), size, digest.c_str(), now,
                        reference_mac) == error::no_challenge,
          "upload replay is rejected");

    check(store.issue(random, sizeof(random), now, nonce_hex, sizeof(nonce_hex)), "upload challenge reissued");
    const std::string fresh(nonce_hex);
    const std::string fresh_mac = sign_upload(fresh, name, size, digest);
    check(verify_upload(store, kKey, sizeof(kKey), fresh.c_str(), fresh_mac.c_str(), name.c_str(), size, digest.c_str(),
                        now + k_nonce_ttl_us, reference_mac) == error::expired,
          "expired upload challenge is rejected");
    check(verify_upload(store, kKey, sizeof(kKey), "short", fresh_mac.c_str(), name.c_str(), size, digest.c_str(), now,
                        reference_mac) == error::unauthorized,
          "malformed upload nonce is unauthorized");
    check(verify_upload(store, kKey, sizeof(kKey), fresh.c_str(), fresh_mac.c_str(), name.c_str(), size, "not-a-digest",
                        now, reference_mac) == error::unauthorized,
          "malformed upload digest is unauthorized");
    check(verify_upload(store, nullptr, sizeof(kKey), fresh.c_str(), fresh_mac.c_str(), name.c_str(), size,
                        digest.c_str(), now, reference_mac) == error::not_ready,
          "missing key is not_ready for upload");
    check(verify_upload(store, kKey, sizeof(kKey), fresh.c_str(), fresh_mac.c_str(), name.c_str(), size, digest.c_str(),
                        now, nullptr) == error::not_ready,
          "missing mac primitive is not_ready");
    check(verify_upload(store, kKey, sizeof(kKey), fresh.c_str(), fresh_mac.c_str(), nullptr, size, digest.c_str(), now,
                        reference_mac) == error::unauthorized,
          "missing basename is unauthorized");
}

void test_names() {
    using namespace cyberdeck_control;
    const auto accepts = [](const std::string &name) { return validate_upload_name(name.data(), name.size()); };
    check(accepts("a.bmp"), "shortest bmp name accepted");
    check(accepts("A.BMP"), "uppercase extension accepted");
    check(accepts("viewer-upload-20261108-1736.bmp"), "documented fixture name accepted");
    check(accepts("name_1.2-3.Bmp"), "mixed case and punctuation accepted");
    check(accepts(std::string(124, 'a') + ".bmp"), "name exactly at the limit accepted");
    check(!accepts(std::string(125, 'a') + ".bmp"), "name one byte over the limit rejected");
    check(!accepts("") && !accepts("a.bm") && !accepts("bmp"), "missing extension rejected");
    check(!accepts("image.png") && !accepts("image.bmp.exe") && !accepts("image"), "other extensions rejected");
    check(!accepts(".hidden.bmp") && !accepts("..bmp"), "dot-leading names rejected");
    check(!accepts("a..bmp"), "double dot rejected");
    check(!accepts("a/b.bmp") && !accepts("a\\b.bmp"), "separators rejected");
    check(!accepts("a b.bmp") && !accepts("a\tb.bmp"), "space and control rejected");
    check(!accepts("caf\xc3\xa9.bmp"), "non-ascii rejected");
    check(!accepts(std::string("a.bmp") + '\0' + "tail"), "embedded NUL rejected");
    check(!validate_upload_name(nullptr, 0), "null name rejected");
}

void test_sizes() {
    using namespace cyberdeck_control;
    const auto parse = [](const std::string &text, std::uint64_t *out) {
        return parse_upload_size(text.data(), text.size(), out);
    };
    std::uint64_t value = 1;
    check(parse("0", &value) && value == 0, "size zero parses (handler maps it to empty)");
    check(parse("1", &value) && value == 1, "size one parses");
    check(parse(std::to_string(k_max_upload_bytes).c_str(), &value) && value == k_max_upload_bytes,
          "size parses at the cap");
    check(parse(std::to_string(k_max_upload_bytes + 1).c_str(), &value) && value == k_max_upload_bytes + 1,
          "size above the cap parses for a 413");
    check(!parse("00", &value), "leading zero rejected");
    check(!parse("+1", &value) && !parse("-1", &value), "signs rejected");
    check(!parse("1.0", &value) && !parse("1a", &value), "non-digits rejected");
    check(!parse("", &value), "empty size rejected");
    check(!parse("18446744073709551616", &value), "overflow rejected");
    check(!parse_upload_size(nullptr, 0, &value), "null size rejected");
    check(!parse_upload_size("1", 1, nullptr), "null output rejected");
}

/* --------------------------- state machine ------------------------------- */

struct fake_files {
    std::string temp = ".upload.fake.tmp";
    bool target_present = false;
    bool fail_open = false;
    bool fail_write = false;
    bool fail_close = false;
    int publish_code = cyberdeck_control::k_upload_publish_ok;
    bool opened = false;
    bool discarded = false;
    bool published = false;
    std::string discarded_name;
    std::string published_target;
    std::vector<std::uint8_t> written;
    /* Contadores usados por TEST-UPLOAD-STREAM-11 (cleanup exato). */
    int open_calls = 0;
    int write_calls = 0;
    int close_calls = 0;
    int publish_calls = 0;
    int discard_calls = 0;
    /* Ordem observada: `close` (fsync+fstat) precisa preceder `publish`. */
    std::vector<std::string> events;
};

bool fake_target_exists(void *context, const char *) {
    return static_cast<fake_files *>(context)->target_present;
}

bool fake_open(void *context, char *out, std::size_t capacity) {
    auto *files = static_cast<fake_files *>(context);
    ++files->open_calls;
    if (files->fail_open || files->temp.size() + 1 > capacity)
        return false;
    std::memcpy(out, files->temp.c_str(), files->temp.size() + 1);
    files->opened = true;
    return true;
}

bool fake_write(void *context, const std::uint8_t *data, std::size_t length) {
    auto *files = static_cast<fake_files *>(context);
    ++files->write_calls;
    if (files->fail_write)
        return false;
    files->written.insert(files->written.end(), data, data + length);
    return true;
}

bool fake_close(void *context, std::uint64_t expected) {
    auto *files = static_cast<fake_files *>(context);
    ++files->close_calls;
    files->events.emplace_back("close");
    return !files->fail_close && files->written.size() == expected;
}

int fake_publish(void *context, const char *temp_name, const char *target_name) {
    auto *files = static_cast<fake_files *>(context);
    ++files->publish_calls;
    files->events.emplace_back("publish");
    if (files->publish_code == cyberdeck_control::k_upload_publish_ok) {
        files->published = true;
        files->discarded_name = temp_name;
        files->published_target = target_name;
    }
    return files->publish_code;
}

void fake_discard(void *context, const char *temp_name) {
    auto *files = static_cast<fake_files *>(context);
    ++files->discard_calls;
    files->discarded = true;
    files->discarded_name = temp_name;
}

cyberdeck_control::upload_file_sink fake_sink(fake_files &files) {
    cyberdeck_control::upload_file_sink sink{};
    sink.context = &files;
    sink.target_exists = fake_target_exists;
    sink.open_temp = fake_open;
    sink.write = fake_write;
    sink.close_temp = fake_close;
    sink.publish = fake_publish;
    sink.discard = fake_discard;
    return sink;
}

struct fake_source {
    const std::vector<std::uint8_t> *data = nullptr;
    std::vector<std::size_t> segments;
    /* Roteiro explicito para TEST-UPLOAD-STREAM-11: >0 bytes, -1 timeout,
     * -2 erro, 0 EOF; a lista cicla. Vazio usa `data`/`segments`. */
    std::vector<int> script;
    std::size_t script_index = 0;
    std::size_t offset = 0;
    std::size_t segment_index = 0;
    int forced_error = 0;
    /* Cap de seguranca: uma mutacao que remova o deadline nao roda sem fim. */
    int calls = 0;
    int max_calls = 1'000'000;
    std::vector<std::size_t> capacities;
    const std::uint8_t *last_buffer = nullptr;
};

int fake_read(void *context, std::uint8_t *buffer, std::size_t capacity) {
    auto *source = static_cast<fake_source *>(context);
    ++source->calls;
    if (source->max_calls > 0 && source->calls > source->max_calls)
        return -2;
    source->capacities.push_back(capacity);
    source->last_buffer = buffer;
    if (source->forced_error != 0) {
        const int error = source->forced_error;
        source->forced_error = 0;
        return error;
    }
    if (!source->script.empty()) {
        const int step = source->script[source->script_index % source->script.size()];
        ++source->script_index;
        if (step <= 0)
            return step;
        if (source->data == nullptr)
            return -2;
        std::size_t want = static_cast<std::size_t>(step);
        const std::size_t remaining = source->data->size() - source->offset;
        if (want > remaining)
            want = remaining;
        if (want > capacity)
            want = capacity;
        if (want == 0)
            return 0;
        std::memcpy(buffer, source->data->data() + source->offset, want);
        source->offset += want;
        return static_cast<int>(want);
    }
    if (source->data == nullptr || source->offset >= source->data->size())
        return 0;
    std::size_t want = source->data->size() - source->offset;
    if (!source->segments.empty()) {
        want = std::min(want, source->segments[source->segment_index % source->segments.size()]);
        ++source->segment_index;
    }
    if (want > capacity)
        want = capacity;
    if (want == 0)
        return 0;
    std::memcpy(buffer, source->data->data() + source->offset, want);
    source->offset += want;
    return static_cast<int>(want);
}

/* Relogio injetado: valor por chamada (o ultimo repete). Nenhum wall clock. */
struct fake_clock {
    std::vector<std::int64_t> schedule;
    std::size_t index = 0;
    int calls = 0;
    std::int64_t value_us = 0;
};

std::int64_t fake_clock_now(void *context) {
    auto *clock = static_cast<fake_clock *>(context);
    if (!clock->schedule.empty()) {
        clock->value_us = clock->schedule[std::min(clock->index, clock->schedule.size() - 1)];
        ++clock->index;
    }
    ++clock->calls;
    return clock->value_us;
}

struct fake_digest {
    sha256 state;
};

bool fake_digest_start(void *context) {
    sha256_init(static_cast<fake_digest *>(context)->state);
    return true;
}

bool fake_digest_update(void *context, const std::uint8_t *data, std::size_t length) {
    sha256_update(static_cast<fake_digest *>(context)->state, data, length);
    return true;
}

bool fake_digest_finish(void *context, std::uint8_t *out) {
    sha256_final(static_cast<fake_digest *>(context)->state, out);
    return true;
}

cyberdeck_control::upload_digest_ops fake_digest_ops(fake_digest &digest) {
    return {&digest, fake_digest_start, fake_digest_update, fake_digest_finish};
}

struct run {
    cyberdeck_control::upload_status status = cyberdeck_control::upload_status::internal;
    fake_files files;
    fake_source source;
    fake_clock clock;
    fake_digest digest;
    cyberdeck_control::upload_outcome outcome;
    std::uint8_t scratch[cyberdeck_control::k_upload_chunk_bytes]{};
    std::uint8_t expected[32]{};
    bool digest_failure = false;
};

/* Monta o estado sem executar: cada cenario negativo comeca limpo (um upload
 * concluido antes contaminaria `opened`/`published`/`discarded`). */
run make_run(const std::vector<std::uint8_t> &body, const std::string &expected_hex,
             std::vector<std::size_t> segments = {}) {
    run result;
    result.source.data = &body;
    result.source.segments = std::move(segments);
    cyberdeck_control::hex_decode(expected_hex.c_str(), expected_hex.size(), result.expected, sizeof(result.expected));
    return result;
}

cyberdeck_control::upload_digest_ops run_digest_ops(run &result) {
    auto ops = fake_digest_ops(result.digest);
    if (result.digest_failure) {
        ops.update = [](void *, const std::uint8_t *, std::size_t) { return false; };
    }
    return ops;
}

cyberdeck_control::upload_clock run_clock(run &result) {
    return {&result.clock, fake_clock_now};
}

void execute(run &result, const char *name = "fixture.bmp") {
    result.status = cyberdeck_control::receive_upload(
        fake_sink(result.files), run_digest_ops(result), fake_read, &result.source, run_clock(result), name,
        result.source.data->size(), result.expected, result.scratch, sizeof(result.scratch), &result.outcome);
}

run run_upload(const std::vector<std::uint8_t> &body, std::uint64_t declared, const std::string &expected_hex,
               std::vector<std::size_t> segments = {}) {
    run result = make_run(body, expected_hex, std::move(segments));
    result.status = cyberdeck_control::receive_upload(
        fake_sink(result.files), run_digest_ops(result), fake_read, &result.source, run_clock(result), "fixture.bmp",
        declared, result.expected, result.scratch, sizeof(result.scratch), &result.outcome);
    return result;
}

void test_receive_happy_and_segmentation() {
    using namespace cyberdeck_control;
    std::vector<std::uint8_t> body;
    for (std::size_t i = 0; i < 10'000; ++i)
        body.push_back(static_cast<std::uint8_t>(i * 31 + 7));
    const std::string expected = digest_hex(body);

    const run baseline = run_upload(body, body.size(), expected);
    check_eq(baseline.status, upload_status::ok, "upload happy path publishes");
    check(baseline.files.published, "upload happy path publishes once");
    check_eq(baseline.files.published_target, std::string("fixture.bmp"), "upload publishes the requested name");
    check(!baseline.files.discarded, "a successful upload does not discard the temp");
    check_eq(baseline.files.events.size(), static_cast<std::size_t>(2),
             "the happy path closes once and publishes once");
    if (baseline.files.events.size() == 2) {
        check_eq(baseline.files.events[0], std::string("close"), "fsync/close happens before publication");
        check_eq(baseline.files.events[1], std::string("publish"), "publication is the last step before the response");
    }
    check_eq(baseline.outcome.bytes, static_cast<std::uint64_t>(body.size()), "upload reports the exact byte count");
    check_eq(to_hex(baseline.outcome.digest, 32), expected, "outcome digest matches one-shot");

    for (const std::vector<std::size_t> &segments :
         std::vector<std::vector<std::size_t>>{{1}, {7}, {4096}, {1, 7, 4096, 4096, 13}}) {
        const run segmented = run_upload(body, body.size(), expected, segments);
        check_eq(segmented.status, upload_status::ok, "segmented upload publishes");
        check(segmented.files.written == body, "segmented upload writes identical bytes");
        check_eq(to_hex(segmented.outcome.digest, 32), expected, "digest is independent of segmentation");
    }
}

/* Bordas do limite de corpo e da fronteira declarado/entregue. Derivadas de
 * `k_max_upload_bytes`: o teto deve ser ACEITO e o sucessor recusado, para que
 * a comparacao `>` vs `>=` seja distinguivel (M-UP-01). */
void test_receive_boundaries() {
    using namespace cyberdeck_control;
    {
        std::vector<std::uint8_t> full(k_max_upload_bytes, 0x5A);
        const std::string expected = digest_hex(full);
        const run at_cap = run_upload(full, full.size(), expected);
        check_eq(at_cap.status, upload_status::ok, "the declared cap is accepted");
        check_eq(at_cap.outcome.bytes, static_cast<std::uint64_t>(full.size()),
                 "the cap upload reports the exact size");
        check(at_cap.files.published, "the cap upload publishes");
    }
    {
        std::vector<std::uint8_t> body(128, 0x42);
        const std::string expected = digest_hex(body);
        std::vector<std::uint8_t> padded = body;
        padded.insert(padded.end(), 5, 0x99);
        const run result = run_upload(padded, body.size(), expected);
        check_eq(result.status, upload_status::ok, "bytes beyond the declared size are not an error");
        check_eq(result.files.written.size(), body.size(), "the published temp holds exactly the declared size");
        check_eq(result.source.offset, body.size(), "the reader is never asked for more than the declared size");
    }
    {
        /* Um leitor que entrega MENOS que o buffer pedido continua valido: quem
         * define o avanco e a contagem recebida, nao a capacidade. */
        std::vector<std::uint8_t> body = {1, 2, 3, 4, 5};
        const std::string expected = digest_hex(body);
        const run result = run_upload(body, body.size(), expected, {1});
        check_eq(result.status, upload_status::ok, "one-byte reads publish");
        check(result.files.written == body, "one-byte reads write identical bytes");
    }
}

void test_receive_negatives() {
    using namespace cyberdeck_control;
    std::vector<std::uint8_t> body(128, 0x42);
    const std::string expected = digest_hex(body);

    /* Falha depois de criar o temporario: cleanup obrigatorio. */
    const auto rejected = [](run &result, upload_status status, const char *name) {
        check_eq(result.status, status, name);
        check(!result.files.published, name);
        check(result.files.discarded, name);
        check_eq(result.files.discarded_name, result.files.temp, name);
    };
    /* Recusa antes de tocar o disco: nenhum temporario aberto/limpo. */
    const auto refused = [](run &result, upload_status status, const char *name) {
        check_eq(result.status, status, name);
        check(!result.files.opened, name);
        check(!result.files.published, name);
    };

    {
        run result = run_upload({}, 0, digest_hex({}));
        refused(result, upload_status::empty, "zero size is rejected before opening a temp");
    }
    {
        run result = run_upload(body, k_max_upload_bytes + 1, expected);
        refused(result, upload_status::too_large, "oversize is rejected before opening a temp");
    }
    {
        run result = make_run(body, expected);
        result.files.target_present = true;
        execute(result);
        refused(result, upload_status::conflict, "an existing target is a conflict before any temp");
    }
    {
        run result = make_run(body, expected);
        execute(result, "bad/name.bmp");
        refused(result, upload_status::invalid_name, "an invalid name never opens a temp");
    }
    {
        run result = run_upload(body, body.size(), std::string(64, 'c'));
        rejected(result, upload_status::checksum, "a digest mismatch aborts the upload");
        check(result.files.written == body, "digest mismatch still verifies the declared size");
    }
    {
        run result = run_upload(body, body.size() + 1, expected);
        rejected(result, upload_status::truncated, "a short body is truncated");
    }
    {
        run result = make_run(body, expected);
        result.source.forced_error = -1;
        execute(result);
        check_eq(result.status, upload_status::ok, "one recv timeout followed by progress does not abort");
        check(result.files.published, "a transient timeout still publishes the complete body");
    }
    {
        /* Defesa secundaria (modulo puro): 3 timeouts consecutivos sem
         * progresso abortam. */
        run result = make_run(body, expected);
        result.source.script = {-1};
        result.source.max_calls = 8;
        execute(result);
        rejected(result, upload_status::timeout, "three consecutive recv timeouts abort and clean up");
    }
    {
        run result = make_run(body, expected);
        result.source.forced_error = -2;
        execute(result);
        rejected(result, upload_status::io_error, "a socket error aborts and cleans up");
    }
    {
        run result = make_run(body, expected);
        result.files.fail_write = true;
        execute(result);
        rejected(result, upload_status::io_error, "a write failure aborts and cleans up");
    }
    {
        run result = make_run(body, expected);
        result.digest_failure = true;
        execute(result);
        rejected(result, upload_status::io_error, "a digest update failure cleans up");
    }
    {
        run result = make_run(body, expected);
        result.files.fail_close = true;
        execute(result);
        rejected(result, upload_status::io_error, "a failed fsync/close never publishes");
    }
    {
        run result = make_run(body, expected);
        result.files.publish_code = k_upload_publish_exists;
        execute(result);
        rejected(result, upload_status::conflict, "a publish race reports conflict");
    }
    {
        run result = make_run(body, expected);
        result.files.publish_code = k_upload_publish_failed;
        execute(result);
        rejected(result, upload_status::io_error, "a publish failure aborts and cleans up");
    }
    {
        run result = make_run(body, expected);
        result.files.fail_open = true;
        execute(result);
        check_eq(result.status, upload_status::io_error, "a temp open failure is an I/O error");
        check(!result.files.published && !result.files.discarded, "a temp that was never created is not discarded");
    }
    {
        run result = make_run(body, expected);
        result.status =
            receive_upload(fake_sink(result.files), run_digest_ops(result), fake_read, &result.source,
                           run_clock(result), "fixture.bmp", body.size(), result.expected, nullptr, 0, &result.outcome);
        check_eq(result.status, upload_status::internal, "a missing scratch buffer is internal");
        check(!result.files.opened, "a missing scratch buffer never opens a temp");
    }
    {
        /* TEST-UPLOAD-STREAM-11: sem fonte de tempo nao ha prazo mensuravel,
         * entao a transacao falha fechada antes de abrir o temporario. */
        run result = make_run(body, expected);
        const cyberdeck_control::upload_clock no_clock{nullptr, nullptr};
        result.status = receive_upload(fake_sink(result.files), run_digest_ops(result), fake_read, &result.source,
                                       no_clock, "fixture.bmp", body.size(), result.expected, result.scratch,
                                       sizeof(result.scratch), &result.outcome);
        check_eq(result.status, upload_status::internal, "a missing clock is internal");
        check(!result.files.opened, "a missing clock never opens a temp");
        check_eq(result.source.calls, 0, "a missing clock never reads the stream");
    }
}

/* --------------------- TEST-UPLOAD-STREAM-11: deadline ------------------- */

void check_chunk_contract(const run &result, std::uint64_t declared, const char *label) {
    using namespace cyberdeck_control;
    check(!result.source.capacities.empty(), label);
    if (result.source.capacities.empty())
        return;
    const std::uint64_t first_expected = declared < k_upload_chunk_bytes ? declared : k_upload_chunk_bytes;
    check_eq(result.source.capacities.front(), static_cast<std::size_t>(first_expected), label);
    bool bounded = true;
    for (std::size_t capacity : result.source.capacities) {
        bounded = bounded && capacity > 0 && capacity <= k_upload_chunk_bytes;
    }
    check(bounded, label);
    check(result.source.last_buffer == result.scratch, label);
}

void check_timeout_cleanup(const run &result, std::uint64_t declared, const char *label) {
    using namespace cyberdeck_control;
    check_eq(result.status, upload_status::timeout, label);
    check_eq(result.files.open_calls, 1, label);
    check_eq(result.files.discard_calls, 1, label);
    check_eq(result.files.discarded_name, result.files.temp, label);
    check_eq(result.files.publish_calls, 0, label);
    check(!result.files.published, label);
    check(result.files.written.size() < declared, label);
    check_eq(result.files.close_calls, 0, label);
    check_eq(result.outcome.bytes, std::uint64_t{0}, label);
    bool digest_zero = true;
    for (std::uint8_t byte : result.outcome.digest)
        digest_zero = digest_zero && byte == 0;
    check(digest_zero, label);
}

void test_receive_deadline() {
    using namespace cyberdeck_control;
    constexpr std::int64_t k_deadline = k_max_upload_duration_us;
    constexpr std::int64_t k_step = 20'000'000;
    /* Iteracoes ate a checagem do deadline disparar (start + N passos). */
    const int abort_iteration = static_cast<int>(k_deadline / k_step);
    const std::vector<std::int64_t> slow_schedule = {0,          k_step,     2 * k_step, 3 * k_step,
                                                     4 * k_step, 5 * k_step, 6 * k_step, 7 * k_step};

    const auto run_scenario = [](run &result, const std::vector<std::uint8_t> &body, std::uint64_t declared,
                                 const char *name) {
        result.source.data = &body;
        result.status = receive_upload(fake_sink(result.files), run_digest_ops(result), fake_read, &result.source,
                                       run_clock(result), name, declared, result.expected, result.scratch,
                                       sizeof(result.scratch), &result.outcome);
    };

    /* S11-a: progresso continuo (nunca -1) nao renova o orcamento. */
    {
        std::vector<std::uint8_t> body(1000, 0x5A);
        run result = make_run(body, digest_hex(body));
        result.source.script = {4};
        result.source.max_calls = 16;
        result.clock.schedule = slow_schedule;
        run_scenario(result, body, body.size(), "fixture.bmp");
        check_timeout_cleanup(result, body.size(), "S11-a continuous progress aborts on the absolute deadline");
        check(result.source.calls < result.source.max_calls,
              "S11-a the deadline stops the stream before the safety cap");
        check_eq(result.clock.calls, result.source.calls + 2,
                 "S11-a one clock read per iteration plus the start (abort has no read)");
        check_eq(result.source.calls + 1, abort_iteration, "S11-a the abort happens exactly at the deadline iteration");
        check_chunk_contract(result, body.size(), "S11-a chunks stay inside the fixed buffer");
    }

    /* S11-b: bytes parciais + -1 intercalados; o orcamento consecutivo nunca
     * esgota, entao so o deadline absoluto pode abortar. */
    {
        std::vector<std::uint8_t> body(1000, 0xA5);
        run result = make_run(body, digest_hex(body));
        result.source.script = {64, -1};
        result.source.max_calls = 16;
        result.clock.schedule = slow_schedule;
        run_scenario(result, body, body.size(), "fixture.bmp");
        check_timeout_cleanup(result, body.size(), "S11-b intermittent partial bytes abort on the deadline");
        check(result.files.write_calls >= 1, "S11-b progress was written before the deadline");
        check_eq(result.source.calls + 1, abort_iteration, "S11-b the secondary consecutive budget never fired");
        check_eq(result.clock.calls, result.source.calls + 2,
                 "S11-b one clock read per iteration plus the start (abort has no read)");
    }

    /* S11-c: controle dentro do prazo conclui e publica normalmente. */
    {
        std::vector<std::uint8_t> body(10'000, 0x22);
        const std::string expected = digest_hex(body);
        run result = make_run(body, expected);
        result.source.script = {1000};
        result.clock.schedule = {0,          10'000'000, 20'000'000, 30'000'000, 40'000'000, 50'000'000,
                                 60'000'000, 70'000'000, 80'000'000, 90'000'000, 100'000'000};
        run_scenario(result, body, body.size(), "fixture.bmp");
        check_eq(result.status, upload_status::ok, "S11-c a transfer inside the budget completes");
        check_eq(result.files.publish_calls, 1, "S11-c publish happens once");
        check_eq(result.files.close_calls, 1, "S11-c close happens once");
        check_eq(result.files.discard_calls, 0, "S11-c nothing is discarded");
        check_eq(result.outcome.bytes, static_cast<std::uint64_t>(body.size()),
                 "S11-c the outcome reports the exact byte count");
        check_eq(to_hex(result.outcome.digest, 32), expected, "S11-c the digest matches one-shot");
        check_eq(result.source.calls, static_cast<int>(body.size() / 1000), "S11-c every chunk was consumed");
        const auto close_at = std::find(result.files.events.begin(), result.files.events.end(), "close");
        const auto publish_at = std::find(result.files.events.begin(), result.files.events.end(), "publish");
        check(close_at < publish_at, "S11-c fsync/close precedes publication");
    }

    /* S11-d: fronteira derivada do header (D-1 continua, D e D+1 abortam). */
    {
        std::vector<std::uint8_t> body(64, 0x33);
        run result = make_run(body, digest_hex(body));
        result.source.script = {64};
        result.clock.schedule = {0, k_deadline - 1};
        run_scenario(result, body, body.size(), "fixture.bmp");
        check_eq(result.status, upload_status::ok, "S11-d D-1 still completes");
        check_eq(result.files.publish_calls, 1, "S11-d D-1 publishes once");
    }
    {
        std::vector<std::uint8_t> body(64, 0x34);
        run result = make_run(body, digest_hex(body));
        result.source.script = {64};
        result.clock.schedule = {0, k_deadline};
        run_scenario(result, body, body.size(), "fixture.bmp");
        check_timeout_cleanup(result, body.size(), "S11-d D aborts at the inclusive boundary");
    }
    {
        std::vector<std::uint8_t> body(64, 0x35);
        run result = make_run(body, digest_hex(body));
        result.source.script = {64};
        result.clock.schedule = {0, k_deadline + 1};
        run_scenario(result, body, body.size(), "fixture.bmp");
        check_timeout_cleanup(result, body.size(), "S11-d D+1 aborts");
    }

    /* S11-e: mesma constante de duracao para 64 B e para o teto de 3 MiB. */
    {
        std::vector<std::uint8_t> tiny(64, 0x44);
        run inside = make_run(tiny, digest_hex(tiny));
        inside.source.script = {64};
        inside.clock.schedule = {0, k_deadline - 1};
        run_scenario(inside, tiny, tiny.size(), "tiny.bmp");
        check_eq(inside.status, upload_status::ok, "S11-e a tiny body gets the same D-1 budget as the maximum body");

        run outside = make_run(tiny, digest_hex(tiny));
        outside.source.script = {64};
        outside.clock.schedule = {0, k_deadline};
        run_scenario(outside, tiny, tiny.size(), "tiny.bmp");
        check_timeout_cleanup(outside, tiny.size(), "S11-e a tiny body aborts at D");

        run capped = make_run(tiny, digest_hex(tiny));
        capped.source.script = {64};
        capped.clock.schedule = {0, k_deadline};
        run_scenario(capped, tiny, k_max_upload_bytes, "capped.bmp");
        check_eq(capped.status, upload_status::timeout, "S11-e the maximum declared body stops at the same D");
        check_eq(capped.files.open_calls, 1, "S11-e the max-body scenario opened its temp");
        check_eq(capped.files.discard_calls, 1, "S11-e the max-body scenario cleaned up");
        check_eq(capped.files.publish_calls, 0, "S11-e the max-body scenario never publishes");
    }

    /* S11-f: cobertura de cleanup ja exercitada em a/b/d/e por
     * check_timeout_cleanup; aqui so o estado inicial observavel. */
    {
        std::vector<std::uint8_t> body(64, 0x77);
        run result = make_run(body, digest_hex(body));
        result.source.script = {64};
        result.clock.schedule = {0, k_deadline};
        run_scenario(result, body, body.size(), "fixture.bmp");
        check_eq(result.files.open_calls, 1, "S11-f exactly one temp is opened");
        check_eq(result.files.discard_calls, 1, "S11-f exactly one owned temp is discarded");
        check_eq(result.files.write_calls, 0, "S11-f no byte is written after the deadline");
        check_eq(result.files.close_calls, 0, "S11-f close never runs after the failure");
    }

    /* S11-g: cada request tem orcamento novo (nada global/estatico). */
    {
        std::vector<std::uint8_t> big(1000, 0x55);
        run first = make_run(big, digest_hex(big));
        first.source.script = {4};
        first.source.max_calls = 16;
        first.clock.schedule = {0, k_deadline, k_deadline, k_deadline};
        run_scenario(first, big, big.size(), "first.bmp");
        check_timeout_cleanup(first, big.size(), "S11-g the first request exhausts its budget");

        std::vector<std::uint8_t> small(64, 0x66);
        run second = make_run(small, digest_hex(small));
        second.clock = first.clock;
        second.source.script = {64};
        run_scenario(second, small, small.size(), "second.bmp");
        check_eq(second.status, upload_status::ok, "S11-g the next request starts with a fresh deadline");
        check_eq(second.files.publish_calls, 1, "S11-g the second request publishes");
        check_eq(second.files.discard_calls, 0, "S11-g the second request never discards");
    }
}

/* ------------------------- real filesystem sink -------------------------- */

int write_file(const std::string &path, const std::string &data) {
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0)
        return -1;
    const ssize_t written = ::write(descriptor, data.data(), data.size());
    (void)::close(descriptor);
    return written == static_cast<ssize_t>(data.size()) ? 0 : -1;
}

std::string read_file(const std::string &path) {
    std::string data;
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0)
        return data;
    char buffer[512];
    ssize_t chunk = 0;
    while ((chunk = ::read(descriptor, buffer, sizeof(buffer))) > 0)
        data.append(buffer, chunk);
    (void)::close(descriptor);
    return data;
}

bool exists(const std::string &path) {
    struct stat info{};
    return ::lstat(path.c_str(), &info) == 0;
}

void test_upload_files_ownership() {
    using namespace cyberdeck_control;
    const char *tmpdir = std::getenv("TMPDIR");
    std::string parent = (tmpdir != nullptr && tmpdir[0] == '/') ? tmpdir : "/tmp";
    std::string root_template = parent + "/cyberdeck-upload-XXXXXX";
    std::vector<char> buffer(root_template.begin(), root_template.end());
    buffer.push_back('\0');
    const char *root_path = ::mkdtemp(buffer.data());
    check(root_path != nullptr, "upload temp directory created");
    if (root_path == nullptr)
        return;
    const std::string root(root_path);

    {
        upload_files files;
        check(upload_files_open(files, root.c_str()), "upload files bind the directory");
        const std::uint32_t candidate = files.sequence;
        const std::string keep = root + "/.upload." + std::to_string(candidate) + ".tmp";
        check(write_file(keep, "KEEP") == 0, "pre-existing candidate temp created");
        auto sink = upload_files_sink(files);
        char temp[64]{};
        check(sink.open_temp(sink.context, temp, sizeof(temp)), "open_temp skips an occupied candidate");
        check_eq(std::string(temp), ".upload." + std::to_string(candidate + 1) + ".tmp",
                 "the owned temp uses the next exclusive name");
        const std::string payload = "owned-bytes";
        check(sink.write(sink.context, reinterpret_cast<const std::uint8_t *>(payload.data()), payload.size()),
              "owned temp accepts the bytes");
        check(sink.close_temp(sink.context, payload.size()), "owned temp closes with the size");
        check_eq(sink.publish(sink.context, temp, "fixture.bmp"), k_upload_publish_ok,
                 "publish succeeds without clobber");
        check_eq(read_file(root + "/fixture.bmp"), payload, "published bytes are exact");
        check_eq(read_file(keep), std::string("KEEP"), "a pre-existing candidate is never removed");
        check(!exists(root + "/" + std::string(temp)), "the temp is gone after publishing");
        check_eq(sink.publish(sink.context, temp, "fixture.bmp"), k_upload_publish_failed,
                 "publishing an unowned temp fails closed");
        check(upload_files_open(files, root.c_str()), "upload files rebind the directory");
        check(!upload_files_open(files, "relative/dir"), "a relative directory is rejected");
        check(!upload_files_open(files, "/tmp/"), "a trailing slash is rejected");
    }

    {
        /* Alvo existente: `target_exists` recusa e o conteudo permanece. */
        check(write_file(root + "/taken.bmp", "ORIGINAL") == 0, "existing target created");
        upload_files files;
        check(upload_files_open(files, root.c_str()), "second upload binds the directory");
        auto sink = upload_files_sink(files);
        check(sink.target_exists(sink.context, "taken.bmp"), "an existing target is reported");
        check(!sink.target_exists(sink.context, "missing.bmp"), "a missing target is not reported");
        check_eq(::symlink("/etc/hostname", (root + "/link.bmp").c_str()), 0, "symlink fixture created");
        check(sink.target_exists(sink.context, "link.bmp"), "a symlink target is refused as existing");
        check_eq(read_file(root + "/taken.bmp"), std::string("ORIGINAL"), "target inspection never modifies the file");
    }

    {
        /* Ownership em falha: so o temporario desta transacao e removido. */
        upload_files files;
        check(upload_files_open(files, root.c_str()), "third upload binds the directory");
        auto sink = upload_files_sink(files);
        char temp[64]{};
        check(sink.open_temp(sink.context, temp, sizeof(temp)), "third temp created");
        check(sink.write(sink.context, reinterpret_cast<const std::uint8_t *>("partial"), 7),
              "third temp receives partial bytes");
        sink.discard(sink.context, temp);
        check(!exists(root + "/" + std::string(temp)), "discard removes only the owned temp");
        sink.discard(sink.context, temp);
        check(!exists(root + "/" + std::string(temp)), "discard is idempotent");
    }

    {
        /* Um nome de temporario que esta transacao NAO criou nunca e removido,
         * mesmo que o chamador peca o descarte (M-UP-07). */
        upload_files files;
        check(upload_files_open(files, root.c_str()), "foreign-discard upload binds the directory");
        auto sink = upload_files_sink(files);
        const std::string foreign = root + "/foreign.tmp";
        check(write_file(foreign, "FOREIGN") == 0, "foreign temp created");
        char own[64]{};
        check(sink.open_temp(sink.context, own, sizeof(own)), "owned temp created");
        sink.discard(sink.context, "foreign.tmp");
        check(exists(foreign), "discard never removes a temp name this transaction does not own");
        sink.discard(sink.context, own);
        check(!exists(root + "/" + std::string(own)), "the owned temp is still removed");
        check(exists(foreign), "the foreign temp survives the owned cleanup");
        check_eq(::unlink(foreign.c_str()), 0, "foreign temp fixture removed");
    }

    {
        /* receive_upload de ponta a ponta com o sink real. */
        std::vector<std::uint8_t> body(2000, 0x5A);
        const std::string expected = digest_hex(body);
        fake_source source;
        source.data = &body;
        source.segments = {97, 1000, 3};
        fake_digest digest;
        upload_outcome outcome{};
        std::uint8_t scratch[k_upload_chunk_bytes]{};
        std::uint8_t expected_bytes[32]{};
        check(hex_decode(expected.c_str(), expected.size(), expected_bytes, sizeof(expected_bytes)),
              "end-to-end digest decoded");
        upload_files files;
        check(upload_files_open(files, root.c_str()), "end-to-end upload binds the directory");
        fake_clock end_clock;
        const upload_clock end_clock_ops = {&end_clock, fake_clock_now};
        const upload_status status =
            receive_upload(upload_files_sink(files), fake_digest_ops(digest), fake_read, &source, end_clock_ops,
                           "full-res-fixture.bmp", body.size(), expected_bytes, scratch, sizeof(scratch), &outcome);
        check_eq(status, upload_status::ok, "end-to-end upload publishes");
        check_eq(read_file(root + "/full-res-fixture.bmp").size(), body.size(), "end-to-end file has the exact size");
        check_eq(to_hex(outcome.digest, 32), expected, "end-to-end digest matches");

        source.data = &body;
        source.offset = 0;
        source.segment_index = 0;
        const upload_status second =
            receive_upload(upload_files_sink(files), fake_digest_ops(digest), fake_read, &source, end_clock_ops,
                           "full-res-fixture.bmp", body.size(), expected_bytes, scratch, sizeof(scratch), &outcome);
        check_eq(second, upload_status::conflict, "a second upload never overwrites");
        check_eq(read_file(root + "/full-res-fixture.bmp").size(), body.size(),
                 "the original target remains byte-identical");
    }

    {
        /* Janela TOCTOU da publicacao: o alvo aparece DEPOIS do pre-check
         * (`target_exists`) e ANTES de publicar. `publish` precisa recusar sem
         * sobrescrever (host: `link` -> EEXIST; ESP: `stat` antes do `rename`)
         * e o conteudo original tem de sobreviver (M-UP-06). */
        upload_files files;
        check(upload_files_open(files, root.c_str()), "race upload binds the directory");
        auto sink = upload_files_sink(files);
        char temp[64]{};
        check(sink.open_temp(sink.context, temp, sizeof(temp)), "race temp created");
        check(sink.write(sink.context, reinterpret_cast<const std::uint8_t *>("RACED"), 5), "race temp receives bytes");
        check(sink.close_temp(sink.context, 5), "race temp closes with the size");
        check(!sink.target_exists(sink.context, "raced.bmp"), "the race target is absent while the pre-check runs");
        check(write_file(root + "/raced.bmp", "ORIGINAL") == 0, "the target appears mid-flight");
        check_eq(sink.publish(sink.context, temp, "raced.bmp"), k_upload_publish_exists,
                 "publication refuses a target that appeared after the pre-check");
        check_eq(read_file(root + "/raced.bmp"), std::string("ORIGINAL"), "a raced target is never overwritten");
        check(exists(root + "/" + std::string(temp)), "a refused publication keeps ownership of its temp");
        sink.discard(sink.context, temp);
        check(!exists(root + "/" + std::string(temp)), "the refused temp is cleaned up");
    }

    /* Limpeza do diretório de teste. */
    for (const char *name : {"/fixture.bmp", "/taken.bmp", "/link.bmp", "/full-res-fixture.bmp", "/raced.bmp"}) {
        (void)::unlink((root + name).c_str());
    }
    for (std::uint32_t index = 0; index < 16; ++index) {
        (void)::unlink((root + "/.upload." + std::to_string(index) + ".tmp").c_str());
    }
    (void)::rmdir(root.c_str());
}

void test_json() {
    using namespace cyberdeck_control;
    const std::string expected = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    std::uint8_t digest[32]{};
    check(hex_decode(expected.c_str(), expected.size(), digest, sizeof(digest)), "json digest decoded");
    char body[k_max_upload_json_bytes + 1]{};
    check(format_upload_json(body, sizeof(body), "fixture.bmp", 2'764'854, digest), "upload json formats");
    check_eq(std::string(body),
             std::string("{\"name\":\"fixture.bmp\",\"bytes\":2764854,\"sha256\":\"") + expected +
                 "\",\"path\":\"/screenshots/fixture.bmp\"}",
             "upload json layout is pinned");
    check(!format_upload_json(body, 16, "fixture.bmp", 1, digest), "upload json fails closed on a small buffer");
    check(!format_upload_json(nullptr, 32, "fixture.bmp", 1, digest), "upload json rejects a null buffer");
}

} // namespace

int main() {
    test_upload_message_layout();
    test_verify_upload();
    test_names();
    test_sizes();
    test_receive_happy_and_segmentation();
    test_receive_boundaries();
    test_receive_negatives();
    test_receive_deadline();
    test_upload_files_ownership();
    test_json();
    if (g_failures != 0) {
        std::printf("FAIL: %d/%d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("PASS: control upload protocol (%d checks)\n", g_checks);
    return 0;
}
