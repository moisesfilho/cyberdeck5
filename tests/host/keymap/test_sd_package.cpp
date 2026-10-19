#include "apps/runtime/cyberdeck_sd_package.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using cyberdeck_apps::sd_package;
using cyberdeck_apps::sd_package_metadata;
using cyberdeck_apps::sd_package_view;

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)

// FNV-1a 32-bit reference vectors (isthe.com/chongo/tech/comp/fnv):
//   ""        -> 2166136261 (0x811c9dc5, offset basis)
//   "a"       -> 3826002220 (0xe40c292c)
//   "foobar"  -> 3214735720 (0xbf9cf968)
constexpr std::uint32_t k_fnv1a_empty = 2166136261u;
constexpr std::uint32_t k_fnv1a_a = 3826002220u;
constexpr std::uint32_t k_fnv1a_foobar = 3214735720u;

std::vector<std::uint8_t> make_bytes(std::size_t count, std::uint8_t seed)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(seed + index));
    }
    return bytes;
}

std::uint32_t checksum_of(const std::vector<std::uint8_t> &bytes)
{
    return sd_package::checksum(bytes.data(), bytes.size());
}

sd_package_view make_valid_view(std::vector<std::uint8_t> &storage,
                                std::string_view name,
                                std::string_view media_type,
                                std::uint32_t version,
                                std::uint32_t size)
{
    sd_package_view view{};
    view.metadata.name = name;
    view.metadata.media_type = media_type;
    view.metadata.format_version = sd_package_metadata::k_format_version;
    view.metadata.version = version;
    view.metadata.size = size;
    view.metadata.checksum = sd_package::checksum(storage.data(), size);
    view.data = storage.data();
    return view;
}

void test_checksum_known_reference_vectors()
{
    const std::string empty;
    const std::string a(1, 'a');
    const std::string foobar = "foobar";

    CHECK(sd_package::checksum(reinterpret_cast<const std::uint8_t *>(empty.data()), empty.size()) == k_fnv1a_empty);
    CHECK(sd_package::checksum(reinterpret_cast<const std::uint8_t *>(a.data()), a.size()) == k_fnv1a_a);
    CHECK(sd_package::checksum(reinterpret_cast<const std::uint8_t *>(foobar.data()), foobar.size()) == k_fnv1a_foobar);
}

void test_checksum_null_and_empty_semantics()
{
    // null pointer with non-zero size is rejected (returns 0).
    CHECK(sd_package::checksum(nullptr, 1) == 0u);
    CHECK(sd_package::checksum(nullptr, 12288) == 0u);

    // null pointer with zero size is the empty-input case (returns offset basis).
    CHECK(sd_package::checksum(nullptr, 0) == k_fnv1a_empty);

    // Empty valid buffer returns the offset basis.
    const std::vector<std::uint8_t> empty;
    CHECK(sd_package::checksum(empty.data(), 0) == k_fnv1a_empty);
}

void test_checksum_deterministic_and_distinct()
{
    const std::vector<std::uint8_t> first = make_bytes(64, 0x10);
    const std::vector<std::uint8_t> second = make_bytes(64, 0x10);
    const std::vector<std::uint8_t> different = make_bytes(64, 0x11);

    const std::uint32_t first_hash = checksum_of(first);
    const std::uint32_t second_hash = checksum_of(second);
    const std::uint32_t different_hash = checksum_of(different);

    CHECK(first_hash == second_hash);
    CHECK(first_hash != different_hash);
    CHECK(first_hash != 0u);
    CHECK(first_hash != k_fnv1a_empty);
}

void test_checksum_single_byte_sequence_matches_reference()
{
    // "foobar" built byte-by-byte to prove the iterative FNV-1a contract.
    const std::string foobar = "foobar";
    std::uint32_t hash = 2166136261u;
    for (char c : foobar) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    CHECK(hash == k_fnv1a_foobar);
    CHECK(hash == sd_package::checksum(reinterpret_cast<const std::uint8_t *>(foobar.data()), foobar.size()));
}

void test_is_data_or_assets_accepts_exact_values()
{
    sd_package_metadata metadata{};

    metadata.media_type = "data";
    CHECK(sd_package::is_data_or_assets(metadata));

    metadata.media_type = "assets";
    CHECK(sd_package::is_data_or_assets(metadata));
}

void test_is_data_or_assets_rejects_other_values()
{
    sd_package_metadata metadata{};

    const std::string rejected[] = {
        "", "Data", "DATA", "Assets", "ASSETS",
        "asset", "datas", "data ", " data", "application/octet-stream",
        "text/plain", "data\r", "data\n",
    };
    for (const auto &value : rejected) {
        metadata.media_type = value;
        CHECK(!sd_package::is_data_or_assets(metadata));
    }
}

void test_validate_accepts_valid_data_package()
{
    auto storage = make_bytes(256, 0x40);
    sd_package_view view = make_valid_view(storage, "notes", "data", 1, 256);
    CHECK(sd_package::validate(view));
}

void test_validate_accepts_valid_assets_package()
{
    auto storage = make_bytes(512, 0x50);
    sd_package_view view = make_valid_view(storage, "theme", "assets", 7, 512);
    CHECK(sd_package::validate(view));
}

void test_validate_accepts_exact_max_size()
{
    auto storage = make_bytes(sd_package::k_max_bytes, 0x60);
    sd_package_view view = make_valid_view(storage, "max", "data", 1, sd_package::k_max_bytes);
    CHECK(sd_package::validate(view));
}

void test_validate_rejects_wrong_format_version()
{
    auto storage = make_bytes(64, 0x70);
    sd_package_view view = make_valid_view(storage, "app", "data", 1, 64);

    view.metadata.format_version = 0;
    CHECK(!sd_package::validate(view));

    view.metadata.format_version = 2;
    CHECK(!sd_package::validate(view));

    view.metadata.format_version = 0xFFFFFFFFu;
    CHECK(!sd_package::validate(view));

    view.metadata.format_version = sd_package_metadata::k_format_version;
    CHECK(sd_package::validate(view));
}

void test_validate_rejects_empty_name()
{
    auto storage = make_bytes(64, 0x71);
    sd_package_view view = make_valid_view(storage, "", "data", 1, 64);
    CHECK(!sd_package::validate(view));

    view.metadata.name = "x";
    CHECK(sd_package::validate(view));
}

void test_validate_rejects_zero_version()
{
    auto storage = make_bytes(64, 0x72);
    sd_package_view view = make_valid_view(storage, "app", "data", 0, 64);
    CHECK(!sd_package::validate(view));

    view.metadata.version = 1;
    CHECK(sd_package::validate(view));
}

void test_validate_rejects_zero_size()
{
    auto storage = make_bytes(64, 0x73);
    sd_package_view view = make_valid_view(storage, "app", "data", 1, 0);
    CHECK(!sd_package::validate(view));
}

void test_validate_rejects_size_above_max()
{
    auto storage = make_bytes(sd_package::k_max_bytes + 1, 0x74);
    sd_package_view view = make_valid_view(storage, "app", "data", 1, sd_package::k_max_bytes + 1);
    CHECK(!sd_package::validate(view));

    view.metadata.size = sd_package::k_max_bytes;
    view.metadata.checksum = sd_package::checksum(storage.data(), sd_package::k_max_bytes);
    CHECK(sd_package::validate(view));
}

void test_validate_rejects_null_data()
{
    auto storage = make_bytes(64, 0x75);
    sd_package_view view = make_valid_view(storage, "app", "data", 1, 64);
    view.data = nullptr;
    CHECK(!sd_package::validate(view));
}

void test_validate_rejects_disallowed_media_type()
{
    auto storage = make_bytes(64, 0x76);
    sd_package_view view = make_valid_view(storage, "app", "application/octet-stream", 1, 64);
    CHECK(!sd_package::validate(view));

    view.metadata.media_type = "Data";
    CHECK(!sd_package::validate(view));

    view.metadata.media_type = "";
    CHECK(!sd_package::validate(view));

    view.metadata.media_type = "data";
    CHECK(sd_package::validate(view));
}

void test_validate_rejects_checksum_mismatch()
{
    auto storage = make_bytes(64, 0x77);
    sd_package_view view = make_valid_view(storage, "app", "data", 1, 64);

    view.metadata.checksum ^= 0xFFFFFFFFu;
    CHECK(!sd_package::validate(view));

    view.metadata.checksum = 0;
    CHECK(!sd_package::validate(view));

    view.metadata.checksum = sd_package::checksum(storage.data(), 64);
    CHECK(sd_package::validate(view));
}

void test_validate_rejects_checksum_mismatch_on_single_byte_flip()
{
    auto storage = make_bytes(128, 0x78);
    sd_package_view view = make_valid_view(storage, "app", "assets", 3, 128);

    storage[64] ^= 0x01;
    CHECK(!sd_package::validate(view));

    storage[64] ^= 0x01;
    CHECK(sd_package::validate(view));
}

void test_validate_rejects_all_rules_simultaneously()
{
    sd_package_view view{};
    view.metadata.format_version = 99;
    view.metadata.name = "";
    view.metadata.media_type = "unknown";
    view.metadata.version = 0;
    view.metadata.size = 0;
    view.metadata.checksum = 0xFFFFFFFFu;
    view.data = nullptr;
    CHECK(!sd_package::validate(view));
}

// TEST-SDP-VALID-ISO-01: every validate() rule is violated ALONE on an
// otherwise-valid package, and restoring that single property makes the
// package validate again.  The restore assertion proves the rejection came
// from the violated property itself, not from a hidden second defect.
void test_validate_isolated_property_rejections()
{
    // format_version rule.
    {
        auto storage = make_bytes(64, 0x80);
        sd_package_view view = make_valid_view(storage, "iso", "data", 1, 64);
        CHECK(sd_package::validate(view));
        view.metadata.format_version = 0;
        CHECK(!sd_package::validate(view));
        view.metadata.format_version = sd_package_metadata::k_format_version;
        CHECK(sd_package::validate(view));
    }
    // name rule.
    {
        auto storage = make_bytes(64, 0x81);
        sd_package_view view = make_valid_view(storage, "iso", "data", 1, 64);
        CHECK(sd_package::validate(view));
        view.metadata.name = "";
        CHECK(!sd_package::validate(view));
        view.metadata.name = "iso";
        CHECK(sd_package::validate(view));
    }
    // version rule.
    {
        auto storage = make_bytes(64, 0x82);
        sd_package_view view = make_valid_view(storage, "iso", "data", 1, 64);
        CHECK(sd_package::validate(view));
        view.metadata.version = 0;
        CHECK(!sd_package::validate(view));
        view.metadata.version = 1;
        CHECK(sd_package::validate(view));
    }
    // size == 0 rule.
    {
        auto storage = make_bytes(64, 0x83);
        sd_package_view view = make_valid_view(storage, "iso", "data", 1, 64);
        CHECK(sd_package::validate(view));
        view.metadata.size = 0;
        CHECK(!sd_package::validate(view));
        view.metadata.size = 64;
        CHECK(sd_package::validate(view));
    }
    // size > k_max_bytes rule (storage holds the oversized payload so only
    // the size rule can be responsible for the rejection).
    {
        auto storage = make_bytes(sd_package::k_max_bytes + 1, 0x84);
        sd_package_view view = make_valid_view(storage, "iso", "data", 1, sd_package::k_max_bytes + 1);
        CHECK(!sd_package::validate(view));
        view.metadata.size = sd_package::k_max_bytes;
        view.metadata.checksum = sd_package::checksum(storage.data(), sd_package::k_max_bytes);
        CHECK(sd_package::validate(view));
    }
    // data-null rule: metadata (including the checksum of the real payload)
    // stays valid; only the pointer is absent.
    {
        auto storage = make_bytes(64, 0x85);
        sd_package_view view = make_valid_view(storage, "iso", "data", 1, 64);
        CHECK(sd_package::validate(view));
        view.data = nullptr;
        CHECK(!sd_package::validate(view));
        view.data = storage.data();
        CHECK(sd_package::validate(view));
    }
    // media_type rule.
    {
        auto storage = make_bytes(64, 0x86);
        sd_package_view view = make_valid_view(storage, "iso", "data", 1, 64);
        CHECK(sd_package::validate(view));
        view.metadata.media_type = "application/octet-stream";
        CHECK(!sd_package::validate(view));
        view.metadata.media_type = "data";
        CHECK(sd_package::validate(view));
    }
    // checksum rule: every other field stays valid.
    {
        auto storage = make_bytes(64, 0x87);
        sd_package_view view = make_valid_view(storage, "iso", "data", 1, 64);
        CHECK(sd_package::validate(view));
        view.metadata.checksum ^= 0xFFFFFFFFu;
        CHECK(!sd_package::validate(view));
        view.metadata.checksum = sd_package::checksum(storage.data(), 64);
        CHECK(sd_package::validate(view));
    }
}

// TEST-SDP-VALID-SIZE-03/04/05: exact size boundaries accepted or rejected.
// The minimum valid size is 1 (size 0 is its own rule) and k_max_bytes is the
// largest valid size.
void test_validate_size_boundaries()
{
    {
        auto storage = make_bytes(1, 0x88);
        sd_package_view view = make_valid_view(storage, "edge", "data", 1, 1);
        CHECK(sd_package::validate(view));
    }
    {
        auto storage = make_bytes(sd_package::k_max_bytes, 0x89);
        sd_package_view view = make_valid_view(storage, "edge", "data", 1, sd_package::k_max_bytes - 1);
        CHECK(sd_package::validate(view));

        view.metadata.size = sd_package::k_max_bytes;
        view.metadata.checksum = sd_package::checksum(storage.data(), sd_package::k_max_bytes);
        CHECK(sd_package::validate(view));
    }
    {
        auto storage = make_bytes(sd_package::k_max_bytes + 1, 0x8A);
        sd_package_view view = make_valid_view(storage, "edge", "data", 1, sd_package::k_max_bytes + 1);
        CHECK(!sd_package::validate(view));
    }
}

// TEST-SDP-VALID-VER-02: version 0 is rejected while 1 and the largest
// representable version are accepted.
void test_validate_version_boundaries()
{
    auto storage = make_bytes(32, 0x8B);
    sd_package_view view = make_valid_view(storage, "edge", "data", 0xFFFFFFFFu, 32);
    CHECK(sd_package::validate(view));

    view.metadata.version = 1;
    CHECK(sd_package::validate(view));

    view.metadata.version = 0;
    CHECK(!sd_package::validate(view));
}

// TEST-SDP-VALID-MEDIA-04: the exact acceptance rule of validate() — "data"
// and "assets" are the only accepted media types, checked on an otherwise-
// valid package so each rejection is attributable to media_type alone.
void test_validate_media_type_exact_in_validate()
{
    auto storage = make_bytes(48, 0x8C);
    sd_package_view view = make_valid_view(storage, "edge", "data", 1, 48);
    CHECK(sd_package::validate(view));

    view.metadata.media_type = "assets";
    CHECK(sd_package::validate(view));

    const std::string rejected[] = {
        "", "Data", "DATA", "Assets", "ASSETS",
        "asset", "datas", "data ", " data", "application/octet-stream",
        "text/plain", "data\r", "data\n",
    };
    for (const auto &value : rejected) {
        view.metadata.media_type = value;
        CHECK(!sd_package::validate(view));
        view.metadata.media_type = "data";
        CHECK(sd_package::validate(view));
    }
}

// TEST-SDP-VALID-NAME-02: the name rule is "must not be empty", not "must be
// trimmed" — a whitespace-only name is accepted and only the empty name is
// rejected.
void test_validate_name_semantics()
{
    auto storage = make_bytes(32, 0x8D);
    sd_package_view view = make_valid_view(storage, " ", "data", 1, 32);
    CHECK(sd_package::validate(view));

    view.metadata.name = "";
    CHECK(!sd_package::validate(view));

    view.metadata.name = "x";
    CHECK(sd_package::validate(view));
}

// TEST-SDP-VALID-CKSUM-04: validate() hashes exactly metadata.size bytes of
// the pointed-to data — a checksum computed over a shorter prefix of the same
// payload must not validate, while the full-length checksum does.
void test_validate_checksum_covers_metadata_size_prefix()
{
    auto storage = make_bytes(128, 0x8E);
    sd_package_view view = make_valid_view(storage, "edge", "data", 1, 128);
    CHECK(sd_package::validate(view));

    view.metadata.checksum = sd_package::checksum(storage.data(), 64);
    CHECK(!sd_package::validate(view));

    view.metadata.checksum = sd_package::checksum(storage.data(), 128);
    CHECK(sd_package::validate(view));
}
} // namespace

int main()
{
    test_checksum_known_reference_vectors();
    test_checksum_null_and_empty_semantics();
    test_checksum_deterministic_and_distinct();
    test_checksum_single_byte_sequence_matches_reference();
    test_is_data_or_assets_accepts_exact_values();
    test_is_data_or_assets_rejects_other_values();
    test_validate_accepts_valid_data_package();
    test_validate_accepts_valid_assets_package();
    test_validate_accepts_exact_max_size();
    test_validate_rejects_wrong_format_version();
    test_validate_rejects_empty_name();
    test_validate_rejects_zero_version();
    test_validate_rejects_zero_size();
    test_validate_rejects_size_above_max();
    test_validate_rejects_null_data();
    test_validate_rejects_disallowed_media_type();
    test_validate_rejects_checksum_mismatch();
    test_validate_rejects_checksum_mismatch_on_single_byte_flip();
    test_validate_rejects_all_rules_simultaneously();
    test_validate_isolated_property_rejections();
    test_validate_size_boundaries();
    test_validate_version_boundaries();
    test_validate_media_type_exact_in_validate();
    test_validate_name_semantics();
    test_validate_checksum_covers_metadata_size_prefix();
    if (failures) return 1;
    std::puts("PASS: cyberdeck_sd_package");
    return 0;
}
