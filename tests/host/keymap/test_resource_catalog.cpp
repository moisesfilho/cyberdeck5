#include "apps/runtime/cyberdeck_resource_catalog.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using cyberdeck_apps::asset_checksum;
using cyberdeck_apps::asset_kind;
using cyberdeck_apps::asset_metadata;
using cyberdeck_apps::compiled_resources;
using cyberdeck_apps::resource_catalog;
using cyberdeck_apps::verify_asset;

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)

// FNV-1a 32-bit reference vectors (isthe.com/chongo/tech/comp/fnv):
//   ""        -> 2166136261 (0x811c9dc5, offset basis)
//   "a"       -> 3826002220 (0xe40c292c)
//   "foobar"  -> 3214735720 (0xbf9cf968)
// These are the published reference constants, independent of the code under
// test; the algorithm is never re-implemented as the oracle.
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

asset_metadata make_verified_asset(const std::vector<std::uint8_t> &storage,
                                   std::string_view id,
                                   asset_kind kind = asset_kind::data)
{
    asset_metadata asset{};
    asset.id = id;
    asset.media_type = "data";
    asset.bytes = storage.data();
    asset.size = storage.size();
    asset.checksum = asset_checksum(storage.data(), storage.size());
    asset.kind = kind;
    return asset;
}

void test_asset_checksum_reference_vectors()
{
    const std::string empty;
    const std::string a(1, 'a');
    const std::string foobar = "foobar";

    CHECK(asset_checksum(reinterpret_cast<const std::uint8_t *>(empty.data()), empty.size()) == k_fnv1a_empty);
    CHECK(asset_checksum(reinterpret_cast<const std::uint8_t *>(a.data()), a.size()) == k_fnv1a_a);
    CHECK(asset_checksum(reinterpret_cast<const std::uint8_t *>(foobar.data()), foobar.size()) == k_fnv1a_foobar);
}

void test_asset_checksum_null_and_empty_semantics()
{
    // null pointer with non-zero size is rejected (returns 0).
    CHECK(asset_checksum(nullptr, 1) == 0u);
    CHECK(asset_checksum(nullptr, 12288) == 0u);
    CHECK(asset_checksum(nullptr, 12289) == 0u);

    // null pointer with zero size is the empty-input case (offset basis).
    CHECK(asset_checksum(nullptr, 0) == k_fnv1a_empty);

    // Empty valid buffer also returns the offset basis.
    const std::vector<std::uint8_t> empty;
    CHECK(asset_checksum(empty.data(), 0) == k_fnv1a_empty);
}

void test_asset_checksum_determinism_and_sensitivity()
{
    const std::vector<std::uint8_t> first = make_bytes(64, 0x10);
    const std::vector<std::uint8_t> second = make_bytes(64, 0x10);
    const std::vector<std::uint8_t> different = make_bytes(64, 0x11);

    const std::uint32_t first_hash = asset_checksum(first.data(), first.size());
    const std::uint32_t second_hash = asset_checksum(second.data(), second.size());
    const std::uint32_t different_hash = asset_checksum(different.data(), different.size());

    CHECK(first_hash == second_hash);
    CHECK(first_hash != different_hash);
    CHECK(first_hash != 0u);
    CHECK(first_hash != k_fnv1a_empty);

    // A single flipped byte changes the hash, so corruption is observable.
    std::vector<std::uint8_t> flipped = first;
    flipped[31] ^= 0x01;
    CHECK(asset_checksum(flipped.data(), flipped.size()) != first_hash);
}

void test_asset_checksum_matches_reference_iteratively()
{
    // Byte-by-byte FNV-1a over "foobar" reproduces the published constant;
    // the oracle is the reference vector, not the checksum under test.
    const std::string foobar = "foobar";
    std::uint32_t hash = 2166136261u;
    for (char c : foobar) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    CHECK(hash == k_fnv1a_foobar);
    CHECK(hash == asset_checksum(reinterpret_cast<const std::uint8_t *>(foobar.data()), foobar.size()));
}

void test_verify_asset_accepts_valid_asset()
{
    auto storage = make_bytes(64, 0x20);
    const asset_metadata asset = make_verified_asset(storage, "app.theme");
    CHECK(verify_asset(asset));
}

void test_verify_asset_rejects_empty_id()
{
    // id rule alone: size, bytes and checksum are all valid.
    auto storage = make_bytes(64, 0x21);
    asset_metadata asset = make_verified_asset(storage, "");
    CHECK(!verify_asset(asset));

    asset.id = "app.theme";
    CHECK(verify_asset(asset));
}

void test_verify_asset_rejects_size_above_max()
{
    // id and checksum stay valid; only the 12288-byte size rule rejects.
    auto storage = make_bytes(12289, 0x22);
    asset_metadata asset = make_verified_asset(storage, "big.payload");
    CHECK(!verify_asset(asset));

    asset.size = 12288;
    asset.checksum = asset_checksum(storage.data(), 12288);
    CHECK(verify_asset(asset));
}

void test_verify_asset_rejects_null_bytes_with_nonzero_size()
{
    // bytes rule alone: id, size and the checksum of the real payload stay.
    auto storage = make_bytes(64, 0x23);
    asset_metadata asset = make_verified_asset(storage, "null.payload");
    asset.bytes = nullptr;
    CHECK(!verify_asset(asset));

    asset.bytes = storage.data();
    CHECK(verify_asset(asset));
}

void test_verify_asset_empty_asset_semantics()
{
    // bytes == nullptr with size == 0 passes the guard chain: the checksum of
    // the empty input is the FNV offset basis, so an asset declaring exactly
    // that value verifies and any other value is rejected.
    asset_metadata asset{};
    asset.id = "empty";
    asset.bytes = nullptr;
    asset.size = 0;

    asset.checksum = k_fnv1a_empty;
    CHECK(verify_asset(asset));

    asset.checksum = 0u;
    CHECK(!verify_asset(asset));
}

void test_verify_asset_rejects_checksum_mismatch()
{
    auto storage = make_bytes(64, 0x24);
    asset_metadata asset = make_verified_asset(storage, "cksum");
    CHECK(verify_asset(asset));

    asset.checksum ^= 0xFFFFFFFFu;
    CHECK(!verify_asset(asset));

    asset.checksum = asset_checksum(storage.data(), 64);
    CHECK(verify_asset(asset));

    // Payload corrupted after the checksum was recorded.
    storage[16] ^= 0x01;
    CHECK(!verify_asset(asset));
}

void test_verify_asset_kind_is_not_validated()
{
    // verify_asset validates id, size, bytes and checksum only: the kind enum
    // does not participate, so the same valid asset verifies under every kind.
    auto storage = make_bytes(32, 0x25);
    const asset_kind kinds[] = {asset_kind::data, asset_kind::font,
                                asset_kind::image, asset_kind::text};
    for (asset_kind kind : kinds) {
        const asset_metadata asset = make_verified_asset(storage, "kinded", kind);
        CHECK(verify_asset(asset));
    }
}

void test_compiled_catalog_is_empty_and_fail_closed()
{
    // An absent catalog is valid: boot does not depend on optional assets.
    const resource_catalog &catalog = compiled_resources();
    CHECK(catalog.size() == 0);
    CHECK(&catalog == &compiled_resources());

    CHECK(catalog.at(0) == nullptr);
    CHECK(catalog.at(resource_catalog::k_max_assets - 1) == nullptr);
    CHECK(catalog.at(resource_catalog::k_max_assets) == nullptr);

    // find() fail-closes both for an empty id and for any unknown id.
    CHECK(catalog.find("") == nullptr);
    CHECK(catalog.find("app.theme") == nullptr);
}
} // namespace

int main()
{
    test_asset_checksum_reference_vectors();
    test_asset_checksum_null_and_empty_semantics();
    test_asset_checksum_determinism_and_sensitivity();
    test_asset_checksum_matches_reference_iteratively();
    test_verify_asset_accepts_valid_asset();
    test_verify_asset_rejects_empty_id();
    test_verify_asset_rejects_size_above_max();
    test_verify_asset_rejects_null_bytes_with_nonzero_size();
    test_verify_asset_empty_asset_semantics();
    test_verify_asset_rejects_checksum_mismatch();
    test_verify_asset_kind_is_not_validated();
    test_compiled_catalog_is_empty_and_fail_closed();
    if (failures) return 1;
    std::puts("PASS: cyberdeck_resource_catalog");
    return 0;
}
