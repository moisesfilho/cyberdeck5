/*
 * TDD RED host contract for the pure BLE bond store.
 *
 * Covers REQ-BLE-007 (bond persistence as a bounded, deterministic record set)
 * and REQ-BLE-010 (no secret is ever persisted, accepted or displayed).
 *
 * RED until the coder creates
 * components/cyberdeck/src/features/bluetooth/cyberdeck_ble_store.cpp with the
 * ABI declared in contracts/cyberdeck_ble_store.h.  No ESP-IDF, NimBLE,
 * esp_hosted, LVGL, NVS, FATFS, simulator or hardware is used here.
 */
#include "cyberdeck_ble_store.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    ++checks; \
    if (!((actual) == (expected))) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #actual); \
    } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    ++checks; \
    const std::string actual_value = (actual); \
    const std::string expected_value = (expected); \
    if (actual_value != expected_value) { \
        ++failures; \
        std::printf("FAIL %s:%d: expected '%s', actual '%s'\n", \
                    __FILE__, __LINE__, expected_value.c_str(), \
                    actual_value.c_str()); \
    } \
} while (0)

using cyberdeck_ble::bond_record;
using cyberdeck_ble::bond_store;
using cyberdeck_ble::device_kind;
using cyberdeck_ble::k_max_bonds;
using cyberdeck_ble::k_max_record_bytes;
using cyberdeck_ble::k_max_store_bytes;
using cyberdeck_ble::store_result;

bond_record make_record(const char *address, const char *name,
                        device_kind kind, bool last_connected,
                        cyberdeck_ble::address_type addr_type =
                            cyberdeck_ble::address_type::public_address)
{
    bond_record record;
    record.address = address;
    record.addr_type = addr_type;
    record.name = name == nullptr ? std::string() : std::string(name);
    record.kind = kind;
    record.last_connected = last_connected;
    return record;
}

void expect_decode(const char *text, const char *address, const char *name,
                   device_kind kind, bool last_connected,
                   cyberdeck_ble::address_type addr_type =
                       cyberdeck_ble::address_type::public_address)
{
    bond_record out = make_record("ZZ:ZZ:ZZ:ZZ:ZZ:ZZ", "Sentinel",
                                  device_kind::keyboard, true);
    const bool ok = cyberdeck_ble::decode_bond(text, std::string(text).size(), out);
    CHECK(ok);
    if (ok) {
        CHECK_STR(out.address, address);
        CHECK_STR(out.name, name);
        CHECK(out.addr_type == addr_type);
        CHECK(out.kind == kind);
        CHECK_EQ(out.last_connected ? 1 : 0, last_connected ? 1 : 0);
    }
}

void expect_decode_rejected(const char *text)
{
    bond_record out = make_record("ZZ:ZZ:ZZ:ZZ:ZZ:ZZ", "Sentinel",
                                  device_kind::keyboard, true);
    const bool ok = cyberdeck_ble::decode_bond(text, std::string(text).size(), out);
    CHECK(!ok);
    if (!ok) {
        /* Fail-closed: a rejected payload never mutates the destination. */
        CHECK_STR(out.address, "ZZ:ZZ:ZZ:ZZ:ZZ:ZZ");
        CHECK_STR(out.name, "Sentinel");
        CHECK(out.kind == device_kind::keyboard);
    }
}

void test_approved_bounds()
{
    CHECK_EQ(k_max_bonds, std::size_t(16));
    CHECK_EQ(k_max_record_bytes, std::size_t(96));
    CHECK_EQ(k_max_store_bytes, std::size_t(2048));
    CHECK_STR(cyberdeck_ble::k_bond_magic, "CDB1");
    CHECK_EQ(bond_store().capacity(), k_max_bonds);
}

void test_encode_is_deterministic_and_field_order_is_fixed()
{
    const bond_record record = make_record("aa:bb:cc:dd:ee:01", "Fone",
                                           device_kind::headset, true);
    const std::string encoded = cyberdeck_ble::encode_bond(record);
    CHECK_STR(encoded,
               "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=Fone;kind=headset;last=1\n");

    /* Deterministic across calls and independent of the caller's spelling. */
    CHECK_STR(cyberdeck_ble::encode_bond(record), encoded);
    const bond_record messy = make_record("AA-BB-CC-DD-EE-01", "Fone",
                                         device_kind::headset, true);
    CHECK_STR(cyberdeck_ble::encode_bond(messy), encoded);

    CHECK(encoded.size() <= k_max_record_bytes);
    CHECK(encoded.find('\n') == encoded.size() - 1);
    CHECK(encoded.find('\r') == std::string::npos);
    /* Field order is fixed, so a diff never reshuffles a persisted bond. */
    CHECK(encoded.find("addr=") < encoded.find("name="));
    CHECK(encoded.find("name=") < encoded.find("kind="));
    CHECK(encoded.find("kind=") < encoded.find("last="));

    /* Every approved kind has a stable token. */
    CHECK_STR(cyberdeck_ble::encode_bond(make_record("AA:BB:CC:DD:EE:01", "K",
                                                     device_kind::keyboard, false)),
               "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=K;kind=keyboard;last=0\n");
    CHECK_STR(cyberdeck_ble::encode_bond(make_record("AA:BB:CC:DD:EE:01", "M",
                                                     device_kind::mouse, false)),
               "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=M;kind=mouse;last=0\n");
    CHECK_STR(cyberdeck_ble::encode_bond(make_record("AA:BB:CC:DD:EE:01", nullptr,
                                                     device_kind::unknown, false)),
               "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=;kind=unknown;last=0\n");
}

void test_encode_sanitizes_and_bounds_the_name()
{
    /* A hostile local name can never break the record layout. */
    const bond_record hostile = make_record("AA:BB:CC:DD:EE:01", "A\nB;C;D",
                                            device_kind::keyboard, false);
    const std::string encoded = cyberdeck_ble::encode_bond(hostile);
    CHECK(encoded.find('\n') == encoded.size() - 1);
    CHECK(encoded.find(";name=A B C D;") != std::string::npos);

    /* An oversized name is clamped so a record can never exceed the bound. */
    const bond_record oversized =
        make_record("AA:BB:CC:DD:EE:01", std::string(500, 'z').c_str(),
                    device_kind::keyboard, false);
    const std::string bounded = cyberdeck_ble::encode_bond(oversized);
    CHECK(bounded.size() <= k_max_record_bytes);

    /* An unusable address cannot be persisted at all. */
    CHECK(cyberdeck_ble::encode_bond(make_record("", "X", device_kind::keyboard,
                                                false)).empty());
    CHECK(cyberdeck_ble::encode_bond(make_record("nope", "X", device_kind::keyboard,
                                                false)).empty());
}

void test_decode_round_trips_and_rejects_everything_else()
{
    expect_decode("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=Fone;kind=headset;last=1\n",
                  "AA:BB:CC:DD:EE:01", "Fone", device_kind::headset, true);
    expect_decode("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=;kind=unknown;last=0\n",
                  "AA:BB:CC:DD:EE:01", "", device_kind::unknown, false);
    /* A record without its trailing LF is still a complete record. */
    expect_decode("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=K;kind=keyboard;last=0",
                  "AA:BB:CC:DD:EE:01", "K", device_kind::keyboard, false);

    /* Wrong marker, truncation and garbage. */
    expect_decode_rejected("CDB2;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01\n");
    expect_decode_rejected("CDB1;name=Fone;kind=headset;last=1\n");
    expect_decode_rejected("CDB1;kind=headset;last=1\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=phone;last=1\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=2\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=yes\n");
    expect_decode_rejected("CDB1;addr=nope;name=Fone;kind=headset;last=1\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1\nx");
    expect_decode_rejected("");
    expect_decode_rejected("not a record at all");

    /* Duplicated and reordered fields are rejected: the order is the contract. */
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr=AA:BB:CC:DD:EE:02;name=Fone;kind=headset;last=1\n");
    expect_decode_rejected("CDB1;name=Fone;addr=AA:BB:CC:DD:EE:01;kind=headset;last=1\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;kind=headset;name=Fone;last=1\n");

    /* A repeated field cannot be smuggled into the fixed field sequence. */
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;name=A;kind=headset;last=1;name=B\n");

    bond_record out = make_record("ZZ:ZZ:ZZ:ZZ:ZZ:ZZ", "Sentinel",
                                  device_kind::mouse, true);
    CHECK(!cyberdeck_ble::decode_bond(nullptr, 0, out));
    CHECK_STR(out.address, "ZZ:ZZ:ZZ:ZZ:ZZ:ZZ");
}

void test_decode_refuses_any_smuggled_secret_field()
{
    /* REQ-BLE-010: there is no field for key material, so a payload carrying one
     * is rejected instead of being silently accepted and re-persisted. */
    const char *forgeries[] = {
        "CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1;ltk=DEADBEEF\n",
        "CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1;irk=DEADBEEF\n",
        "CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1;key=DEADBEEF\n",
        "CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1;passkey=246813\n",
        "CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1;pin=1234\n",
    };
    for (const char *text : forgeries) {
        expect_decode_rejected(text);
    }

    /* The same rule applies to a whole-store payload. */
    bond_store store;
    CHECK(store.add(make_record("AA:BB:CC:DD:EE:01", "Fone", device_kind::headset,
                                true)) == store_result::ok);
    const std::string before = store.serialize();

    const std::string forged =
        "CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1\n"
        "CDB1;addr=AA:BB:CC:DD:EE:02;name=X;kind=keyboard;last=0;ltk=DEADBEEF\n";
    CHECK(!store.deserialize(forged.c_str(), forged.size()));
    CHECK_STR(store.serialize(), before);
    CHECK_EQ(store.size(), std::size_t(1));
}

void test_store_add_update_remove_and_capacity()
{
    bond_store store;
    CHECK_EQ(store.size(), std::size_t(0));
    CHECK(store.find("AA:BB:CC:DD:EE:01") == nullptr);

    CHECK(store.add(make_record("aa:bb:cc:dd:ee:01", "Fone", device_kind::headset,
                                true)) == store_result::ok);
    const bond_record *stored = store.find("AA:BB:CC:DD:EE:01");
    CHECK(stored != nullptr);
    if (stored != nullptr) {
        CHECK_STR(stored->address, "AA:BB:CC:DD:EE:01");
        CHECK_STR(stored->name, "Fone");
        CHECK(stored->kind == device_kind::headset);
        CHECK(stored->last_connected);
    }

    /* An insert of an existing address is a duplicate, not a silent overwrite. */
    CHECK(store.add(make_record("AA:BB:CC:DD:EE:01", "Other",
                                device_kind::mouse, false)) ==
          store_result::duplicate);
    CHECK_EQ(store.size(), std::size_t(1));
    stored = store.find("AA:BB:CC:DD:EE:01");
    CHECK(stored != nullptr);
    if (stored != nullptr) CHECK_STR(stored->name, "Fone");

    CHECK(store.update(make_record("AA:BB:CC:DD:EE:01", "Fone Pro",
                                   device_kind::headset, false)) ==
          store_result::ok);
    stored = store.find("AA:BB:CC:DD:EE:01");
    CHECK(stored != nullptr);
    if (stored != nullptr) {
        CHECK_STR(stored->name, "Fone Pro");
        CHECK(!stored->last_connected);
    }
    CHECK_EQ(store.size(), std::size_t(1));

    CHECK(store.update(make_record("AA:BB:CC:DD:EE:FF", "Ghost",
                                   device_kind::keyboard, false)) ==
          store_result::not_found);
    CHECK(store.add(make_record("", "Bad", device_kind::keyboard, false)) ==
          store_result::invalid);
    CHECK(store.add(make_record("nope", "Bad", device_kind::keyboard, false)) ==
          store_result::invalid);
    CHECK_EQ(store.size(), std::size_t(1));

    for (std::size_t i = 0; i < k_max_bonds; ++i) {
        char address[18];
        std::snprintf(address, sizeof(address), "AA:BB:CC:DD:%02X:%02X",
                      static_cast<unsigned>(i), static_cast<unsigned>(i));
        if (i == 0) continue; /* already inserted above */
        const store_result result =
            store.add(make_record(address, "Bulk", device_kind::keyboard, false));
        if (result != store_result::ok) {
            std::printf("FAIL %s:%d: bulk insert %zu -> %d\n", __FILE__, __LINE__,
                        i, static_cast<int>(result));
            ++failures;
        }
        ++checks;
    }
    CHECK_EQ(store.size(), k_max_bonds);
    CHECK(store.add(make_record("AA:BB:CC:DD:EE:AA", "Overflow",
                                device_kind::keyboard, false)) ==
          store_result::full);
    CHECK_EQ(store.size(), k_max_bonds);
    CHECK(store.find("AA:BB:CC:DD:EE:AA") == nullptr);

    CHECK(!store.remove("AA:BB:CC:DD:EE:FF"));
    CHECK(store.remove("AA:BB:CC:DD:EE:01"));
    CHECK_EQ(store.size(), k_max_bonds - 1);
    CHECK(store.find("AA:BB:CC:DD:EE:01") == nullptr);
    CHECK(!store.remove(""));
    CHECK(!store.remove("nope"));

    store.clear();
    CHECK_EQ(store.size(), std::size_t(0));
    CHECK(store.snapshot().empty());
    CHECK_STR(store.serialize(), "");
}

void test_snapshot_is_insertion_ordered_and_independent()
{
    bond_store store;
    CHECK(store.add(make_record("AA:BB:CC:DD:EE:03", "C", device_kind::mouse,
                                false)) == store_result::ok);
    CHECK(store.add(make_record("AA:BB:CC:DD:EE:01", "A",
                                device_kind::keyboard, true)) == store_result::ok);
    CHECK(store.add(make_record("AA:BB:CC:DD:EE:02", "B", device_kind::headset,
                                false)) == store_result::ok);

    std::vector<bond_record> snapshot = store.snapshot();
    CHECK_EQ(snapshot.size(), std::size_t(3));
    if (snapshot.size() == 3) {
        CHECK_STR(snapshot[0].address, "AA:BB:CC:DD:EE:03");
        CHECK_STR(snapshot[1].address, "AA:BB:CC:DD:EE:01");
        CHECK_STR(snapshot[2].address, "AA:BB:CC:DD:EE:02");
    }

    /* The caller owns its copy: mutating it cannot corrupt the store. */
    snapshot[0].name = "Mutated";
    snapshot.clear();
    const std::vector<bond_record> again = store.snapshot();
    CHECK_EQ(again.size(), std::size_t(3));
    if (!again.empty()) CHECK_STR(again[0].name, "C");
}

void test_serialize_is_bounded_and_deserialize_is_atomic()
{
    bond_store store;
    for (std::size_t i = 0; i < k_max_bonds; ++i) {
        char address[18];
        std::snprintf(address, sizeof(address), "AA:BB:CC:DD:%02X:%02X",
                      static_cast<unsigned>(i >> 8), static_cast<unsigned>(i & 0xFF));
        CHECK(store.add(make_record(address, "Peripheral", device_kind::keyboard,
                                    i % 2 == 0)) == store_result::ok);
    }
    const std::string blob = store.serialize();
    CHECK(blob.size() <= k_max_store_bytes);
    CHECK(blob.size() <= k_max_bonds * k_max_record_bytes);
    CHECK(!blob.empty());
    CHECK(blob.find("ltk=") == std::string::npos);
    CHECK(blob.find("irk=") == std::string::npos);
    CHECK(blob.find("passkey=") == std::string::npos);

    /* Round trip into a fresh store. */
    bond_store restored;
    CHECK(restored.deserialize(blob.c_str(), blob.size()));
    CHECK_EQ(restored.size(), k_max_bonds);
    CHECK_STR(restored.serialize(), blob);

    /* An empty payload clears the store, and a null/empty payload is safe. */
    CHECK(restored.deserialize("", 0));
    CHECK_EQ(restored.size(), std::size_t(0));
    CHECK(restored.deserialize(nullptr, 0));
    CHECK_EQ(restored.size(), std::size_t(0));

    /* Any malformed line rejects the whole payload: the previous content
     * survives untouched, so a corrupted file can never wipe the bonds. */
    bond_store keeper;
    CHECK(keeper.add(make_record("AA:BB:CC:DD:EE:01", "Keep",
                                 device_kind::headset, true)) == store_result::ok);
    const std::string before = keeper.serialize();

    const char *corrupt[] = {
        "CDB1;addr=AA:BB:CC:DD:EE:02;name=Ok;kind=keyboard;last=0\nBROKEN\n",
        "CDB1;addr=AA:BB:CC:DD:EE:02;name=Ok;kind=keyboard;last=0\nCDB1;addr=bad;name=X;kind=keyboard;last=0\n",
        "CDB1;addr=AA:BB:CC:DD:EE:02;name=Ok;kind=keyboard;last=0\nCDB2;addr=AA:BB:CC:DD:EE:03;name=Y;kind=keyboard;last=0\n",
    };
    for (const char *text : corrupt) {
        CHECK(!keeper.deserialize(text, std::string(text).size()));
        CHECK_EQ(keeper.size(), std::size_t(1));
        CHECK_STR(keeper.serialize(), before);
    }

    /* A CRLF-free trailing blank line is tolerated; a stray CR is not. */
    CHECK(keeper.deserialize("CDB1;addr=AA:BB:CC:DD:EE:09;addr_type=0;name=N;kind=keyboard;last=0\n\n",
                             std::string("CDB1;addr=AA:BB:CC:DD:EE:09;addr_type=0;name=N;kind=keyboard;last=0\n\n").size()));
    CHECK_EQ(keeper.size(), std::size_t(1));
    CHECK(!keeper.deserialize("CDB1;addr=AA:BB:CC:DD:EE:09;addr_type=0;name=N;kind=keyboard;last=0\r\n",
                              std::string("CDB1;addr=AA:BB:CC:DD:EE:09;addr_type=0;name=N;kind=keyboard;last=0\r\n").size()));
    CHECK_EQ(keeper.size(), std::size_t(1));
}

void test_all_address_types_round_trip_without_aliasing()
{
    using cyberdeck_ble::address_type;
    const address_type types[] = {address_type::public_address,
                                  address_type::random_static,
                                  address_type::random_resolvable,
                                  address_type::random_non_resolvable};
    for (address_type type : types) {
        const bond_record input = make_record("AA:BB:CC:DD:EE:01", "Peer",
                                              device_kind::unknown, true, type);
        const std::string encoded = cyberdeck_ble::encode_bond(input);
        CHECK(encoded.find("addr_type=" +
                           std::to_string(static_cast<unsigned>(type))) !=
              std::string::npos);
        bond_record output = make_record("ZZ:ZZ:ZZ:ZZ:ZZ:ZZ", "sentinel",
                                         device_kind::keyboard, false);
        CHECK(cyberdeck_ble::decode_bond(encoded.c_str(), encoded.size(), output));
        CHECK(output.addr_type == type);
        bond_store store;
        CHECK(store.add(input) == store_result::ok);
        CHECK(store.find(input.address, type) != nullptr);
    }
}

void test_bond_survives_a_reboot_round_trip()
{
    /* REQ-BLE-007 as the user sees it: pair, reboot, still listed. */
    bond_store live;
    CHECK(live.add(make_record("AA:BB:CC:DD:EE:01", "Fone", device_kind::headset,
                               true)) == store_result::ok);
    CHECK(live.add(make_record("AA:BB:CC:DD:EE:02", "Mouse BT",
                               device_kind::mouse, false)) == store_result::ok);
    const std::string persisted = live.serialize();

    bond_store rebooted;
    CHECK(rebooted.deserialize(persisted.c_str(), persisted.size()));
    const std::vector<bond_record> snapshot = rebooted.snapshot();
    CHECK_EQ(snapshot.size(), std::size_t(2));
    if (snapshot.size() == 2) {
        CHECK_STR(snapshot[0].address, "AA:BB:CC:DD:EE:01");
        CHECK(snapshot[0].kind == device_kind::headset);
        CHECK(snapshot[0].last_connected);
        CHECK_STR(snapshot[1].address, "AA:BB:CC:DD:EE:02");
        CHECK(snapshot[1].kind == device_kind::mouse);
        CHECK(!snapshot[1].last_connected);
    }
    CHECK_STR(rebooted.serialize(), persisted);

    /* The name survives sanitization on both sides of the round trip. */
    bond_store hostile;
    CHECK(hostile.add(make_record("AA:BB:CC:DD:EE:03", "Tab\there",
                                  device_kind::keyboard, false)) == store_result::ok);
    const std::string hostile_blob = hostile.serialize();
    CHECK(hostile_blob.find('\t') == std::string::npos);
    bond_store hostile_rebooted;
    CHECK(hostile_rebooted.deserialize(hostile_blob.c_str(), hostile_blob.size()));
    const bond_record *kept = hostile_rebooted.find("AA:BB:CC:DD:EE:03");
    CHECK(kept != nullptr);
    if (kept != nullptr) CHECK_STR(kept->name, "Tab here");
}

void test_legacy_four_field_blob_is_detected_but_v2_is_not()
{
    const char *legacy =
        "CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1\n";
    CHECK(cyberdeck_ble::is_legacy_bond_blob(legacy, std::string(legacy).size()));

    const std::string v2 =
        "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=2;name=Fone;kind=headset;last=1\n";
    CHECK(!cyberdeck_ble::is_legacy_bond_blob(v2.c_str(), v2.size()));

    bond_store preserved;
    CHECK(preserved.deserialize(v2.c_str(), v2.size()));
    CHECK_EQ(preserved.size(), std::size_t(1));
    const bond_record *record = preserved.find("AA:BB:CC:DD:EE:01",
                                                cyberdeck_ble::address_type::random_resolvable);
    CHECK(record != nullptr);
    if (record != nullptr) CHECK_STR(record->name, "Fone");

    /* The legacy marker is a policy signal, not a silently accepted record. */
    bond_store rejected;
    CHECK(!rejected.deserialize(legacy, std::string(legacy).size()));
    CHECK_EQ(rejected.size(), std::size_t(0));
}

/* REQ-COV-01/TEST-COV-STORE: deterministic edge coverage for the real missed
 * gcov branches (invalid/update/remove/find guards, record/store bounds,
 * field-level decode rejects, legacy scan edges). */
void test_coverage_update_remove_find_guards()
{
    bond_store store;
    /* Invalid addresses never touch the store on any mutating path. */
    CHECK(store.update(make_record("", "X", device_kind::keyboard, false)) ==
          store_result::invalid);
    CHECK(store.update(make_record("nope", "X", device_kind::keyboard, false)) ==
          store_result::invalid);
    CHECK(!store.remove(""));
    CHECK(!store.remove("nope"));
    CHECK(store.find("") == nullptr);
    CHECK(store.find("nope") == nullptr);
    CHECK(store.find("", cyberdeck_ble::address_type::public_address) == nullptr);
    CHECK(store.find("AA:BB:CC:DD:EE:01",
                     cyberdeck_ble::address_type::public_address) == nullptr);

    /* Same address with a different type is a different bond. */
    CHECK(store.add(make_record("AA:BB:CC:DD:EE:01", "Pub", device_kind::keyboard,
                                false)) == store_result::ok);
    CHECK(store.add(make_record("AA:BB:CC:DD:EE:01", "Rnd",
                                device_kind::keyboard, false,
                                cyberdeck_ble::address_type::random_static)) ==
          store_result::ok);
    CHECK(store.update(make_record("AA:BB:CC:DD:EE:01", "Rnd2",
                                   device_kind::mouse, true,
                                   cyberdeck_ble::address_type::random_static)) ==
          store_result::ok);
    const bond_record *typed = store.find(
        "AA:BB:CC:DD:EE:01", cyberdeck_ble::address_type::random_static);
    CHECK(typed != nullptr);
    if (typed != nullptr) CHECK_STR(typed->name, "Rnd2");
    CHECK(store.find("AA:BB:CC:DD:EE:01",
                     cyberdeck_ble::address_type::random_resolvable) == nullptr);
    /* Untyped find returns the first spelling inserted. */
    const bond_record *first = store.find("AA:BB:CC:DD:EE:01");
    CHECK(first != nullptr);
    if (first != nullptr) CHECK_STR(first->name, "Pub");
}

void test_coverage_encode_decode_field_bounds()
{
    /* sanitize_for_encode: empty input, C0 collapse edges, cap trimming and
     * leading/trailing space removal. */
    CHECK_STR(cyberdeck_ble::encode_bond(make_record("AA:BB:CC:DD:EE:01", nullptr,
                                                     device_kind::unknown, false)),
              "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=;kind=unknown;last=0\n");
    const std::string collapsed = cyberdeck_ble::encode_bond(
        make_record("AA:BB:CC:DD:EE:01", " \t A \t B ", device_kind::keyboard, false));
    CHECK(collapsed.find(";name=A  B;") != std::string::npos);
    /* DEL/C1 bytes collapse to a single separator, never a raw byte. */
    const char raw_name[] = {'X', '\x7F', '\x80', 'Y'};
    const std::string del = cyberdeck_ble::encode_bond(
        make_record("AA:BB:CC:DD:EE:01",
                    std::string(raw_name, sizeof(raw_name)).c_str(),
                    device_kind::keyboard, false));
    CHECK(del.find(";name=X Y;") != std::string::npos);
    /* A max-length sanitized name overflows k_max_record_bytes, so the
     * record is fail-closed (""), while a shorter name still serializes. */
    CHECK(cyberdeck_ble::encode_bond(
              make_record("AA:BB:CC:DD:EE:01", std::string(32, 'n').c_str(),
                          device_kind::keyboard, false))
               .empty());
    CHECK(!cyberdeck_ble::encode_bond(
              make_record("AA:BB:CC:DD:EE:01", std::string(20, 'n').c_str(),
                          device_kind::keyboard, false))
               .empty());

    /* decode_bond structural rejects: short magic, bad marker, short fields,
     * bad address, bad addr_type spellings, overlong/unsanitized names,
     * bad kind and bad last flag. */
    expect_decode_rejected("CDB1");
    expect_decode_rejected("XXXX;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=N;kind=keyboard;last=0\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=;name=N;kind=keyboard;last=0\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=12;name=N;kind=keyboard;last=0\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=x;name=N;kind=keyboard;last=0\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=9;name=N;kind=keyboard;last=0\n");
    {
        const std::string overlong(33, 'n');
        const std::string payload = "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=" +
                                    overlong + ";kind=keyboard;last=0\n";
        bond_record out = make_record("AA:BB:CC:DD:EE:01", "Keep",
                                      device_kind::keyboard, false);
        CHECK(!cyberdeck_ble::decode_bond(payload.c_str(), payload.size(), out));
        CHECK_STR(out.name, "Keep");
    }
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=A;B;kind=keyboard;last=0\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=N;kind=Keyboard;last=0\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=N;kind=keyboard;last=\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=N;kind=keyboard;last=10\n");
    /* Missing-field count and a duplicated trailing field are rejected. */
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=N;kind=keyboard\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=N;kind=keyboard;last=0;last=0\n");
    /* Address spellings that normalize cleanly still decode. */
    expect_decode("CDB1;addr=aa-bb-cc-dd-ee-01;addr_type=1;name=N;kind=mouse;last=1\n",
                  "AA:BB:CC:DD:EE:01", "N", device_kind::mouse, true,
                  cyberdeck_ble::address_type::random_static);
    expect_decode("CDB1;addr=AABBCCDDEEFF;addr_type=3;name=N;kind=keyboard;last=0\n",
                  "AA:BB:CC:DD:EE:FF", "N", device_kind::keyboard, false,
                  cyberdeck_ble::address_type::random_non_resolvable);
    /* Invalid normalized addresses are rejected without mutating the output. */
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:0;addr_type=0;name=N;kind=keyboard;last=0\n");
    expect_decode_rejected("CDB1;addr=AA BB CC DD EE 01;addr_type=0;name=N;kind=keyboard;last=0\n");
    /* A name that is already sanitized but empty decodes; one carrying a raw
     * control byte or an overlong field never does. */
    expect_decode("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=;kind=mouse;last=1\n",
                  "AA:BB:CC:DD:EE:01", "", device_kind::mouse, true);
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=A\x01" "B;kind=keyboard;last=0\n");
    /* Kind aliases and last-flag spellings outside {0,1} are rejected. */
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=N;kind=KEYBOARD;last=0\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=N;kind=;last=0\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=N;kind=keyboard;last=true\n");
    expect_decode_rejected("CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=N;kind=keyboard;last= 1\n");
}

void test_coverage_crud_normalization_and_missing_paths()
{
    bond_store store;
    /* CRUD accepts every deterministic address spelling callers may use. */
    CHECK(store.add(make_record("aa-bb-cc-dd-ee-10", "Dash", device_kind::keyboard,
                                false)) == store_result::ok);
    CHECK(store.add(make_record("AABBCCDDEE11", "Bare", device_kind::mouse,
                                true)) == store_result::ok);
    const bond_record *dashed = store.find("AA:BB:CC:DD:EE:10");
    CHECK(dashed != nullptr);
    if (dashed != nullptr) CHECK_STR(dashed->name, "Dash");
    const bond_record *bare = store.find("aa:bb:cc:dd:ee:11");
    CHECK(bare != nullptr);
    if (bare != nullptr) CHECK(bare->last_connected);
    /* Update normalizes as well, so spelling never forks a duplicate bond. */
    CHECK(store.update(make_record("AA-BB-CC-DD-EE-10", "Dash2",
                                   device_kind::keyboard, true)) ==
          store_result::ok);
    dashed = store.find("AA:BB:CC:DD:EE:10");
    CHECK(dashed != nullptr);
    if (dashed != nullptr) {
        CHECK_STR(dashed->name, "Dash2");
        CHECK(dashed->last_connected);
    }
    /* Remove matches the same normalized spelling and keeps the survivor. */
    CHECK(store.remove("aa:bb:cc:dd:ee:11"));
    CHECK(store.find("AA:BB:CC:DD:EE:11") == nullptr);
    CHECK_EQ(store.size(), std::size_t(1));
    /* Missing-typed lookups and removes never mutate the survivor. */
    CHECK(store.find("AA:BB:CC:DD:EE:10",
                     cyberdeck_ble::address_type::random_static) == nullptr);
    CHECK(!store.remove("AA:BB:CC:DD:EE:99"));
    CHECK_EQ(store.size(), std::size_t(1));
    /* Duplicate detection is per identity, not per address spelling. */
    CHECK(store.add(make_record("AA:BB:CC:DD:EE:10", "Dup", device_kind::mouse,
                                false)) == store_result::duplicate);
    CHECK(store.add(make_record("AA:BB:CC:DD:EE:10", "OtherType",
                                device_kind::mouse, false,
                                cyberdeck_ble::address_type::random_static)) ==
          store_result::ok);
    CHECK_EQ(store.size(), std::size_t(2));
    /* An unencodable overlong name is still stored logically, but encode stays
     * fail-closed so the serialized blob never carries a partial line. */
    CHECK(store.add(make_record("AA:BB:CC:DD:EE:20",
                                std::string(32, 'w').c_str(),
                                device_kind::keyboard, false)) ==
          store_result::ok);
    CHECK_EQ(store.size(), std::size_t(3));
    CHECK(store.serialize().find("AA:BB:CC:DD:EE:20") == std::string::npos);
}

void test_coverage_serialize_deserialize_and_legacy_edges()
{
    bond_store store;
    /* Deserialize rejects oversized blobs and duplicate records atomically. */
    const std::string big(cyberdeck_ble::k_max_store_bytes + 1, 'x');
    CHECK(!store.deserialize(big.c_str(), big.size()));
    CHECK_EQ(store.size(), std::size_t(0));
    const std::string dup =
        "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=A;kind=keyboard;last=0\n"
        "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=B;kind=mouse;last=1\n";
    CHECK(!store.deserialize(dup.c_str(), dup.size()));
    CHECK_EQ(store.size(), std::size_t(0));
    /* Legacy four-field lines are rejected atomically, never migrated silently. */
    const std::string legacy_line =
        "CDB1;addr=AA:BB:CC:DD:EE:01;name=A;kind=keyboard;last=0\n";
    CHECK(!store.deserialize(legacy_line.c_str(), legacy_line.size()));
    CHECK_EQ(store.size(), std::size_t(0));
    const std::string mixed_kinds =
        "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=A;kind=keyboard;last=0\n"
        "CDB1;addr=AA:BB:CC:DD:EE:02;addr_type=1;name=B;kind=mouse;last=1\n";
    CHECK(store.deserialize(mixed_kinds.c_str(), mixed_kinds.size()));
    CHECK_EQ(store.size(), std::size_t(2));
    const bond_record *known = store.find("AA:BB:CC:DD:EE:02",
                                          cyberdeck_ble::address_type::random_static);
    CHECK(known != nullptr);
    if (known != nullptr) {
        CHECK_STR(known->name, "B");
        CHECK(known->kind == device_kind::mouse);
        CHECK(known->last_connected);
    }
    /* An oversized single record is dropped by encode, so serialize truncates
     * the blob deterministically instead of emitting a partial line. */
    bond_store oversized_holder;
    CHECK(oversized_holder.add(make_record("AA:BB:CC:DD:EE:0A", "Tall",
                                           device_kind::keyboard, true)) ==
          store_result::ok);
    CHECK(oversized_holder.add(make_record("AA:BB:CC:DD:EE:0B",
                                           std::string(32, 'q').c_str(),
                                           device_kind::keyboard, false)) ==
          store_result::ok);
    const std::string truncated = oversized_holder.serialize();
    CHECK(truncated.find("AA:BB:CC:DD:EE:0A") != std::string::npos);
    CHECK(truncated.find("AA:BB:CC:DD:EE:0B") == std::string::npos);
    CHECK(!truncated.empty());
    CHECK(truncated.back() == '\n');
    /* A record without trailing LF on the last line still parses. */
    const std::string no_trailing_lf =
        "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=A;kind=keyboard;last=0";
    CHECK(store.deserialize(no_trailing_lf.c_str(), no_trailing_lf.size()));
    CHECK_EQ(store.size(), std::size_t(1));
    /* Blank lines are skipped; trailing garbage after a blank fails closed. */
    const std::string blank_ok =
        "CDB1;addr=AA:BB:CC:DD:EE:02;addr_type=1;name=B;kind=mouse;last=1\n\n";
    CHECK(store.deserialize(blank_ok.c_str(), blank_ok.size()));
    CHECK_EQ(store.size(), std::size_t(1));
    const std::string trailing_garbage =
        "CDB1;addr=AA:BB:CC:DD:EE:03;addr_type=0;name=C;kind=headset;last=0\n\nZZ";
    CHECK(!store.deserialize(trailing_garbage.c_str(), trailing_garbage.size()));
    CHECK_EQ(store.size(), std::size_t(1));
    /* CR-only trailing content is garbage too. */
    const std::string cr_only =
        "CDB1;addr=AA:BB:CC:DD:EE:03;addr_type=0;name=C;kind=headset;last=0\n\r";
    CHECK(!store.deserialize(cr_only.c_str(), cr_only.size()));

    /* Legacy scan edges: empty/null/oversized never detect; a blob whose
     * first line is legacy detects even with later v2 lines; a blob whose
     * legacy line lacks LF still detects; single-line loop exits cleanly. */
    CHECK(!cyberdeck_ble::is_legacy_bond_blob(nullptr, 0));
    CHECK(!cyberdeck_ble::is_legacy_bond_blob("", 0));
    CHECK(!cyberdeck_ble::is_legacy_bond_blob(big.c_str(), big.size()));
    const std::string mixed =
        "CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1\n"
        "CDB1;addr=AA:BB:CC:DD:EE:02;addr_type=0;name=X;kind=keyboard;last=0\n";
    CHECK(cyberdeck_ble::is_legacy_bond_blob(mixed.c_str(), mixed.size()));
    const char *no_lf = "CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1";
    CHECK(cyberdeck_ble::is_legacy_bond_blob(no_lf, std::string(no_lf).size()));
    const char *unrelated = "hello world";
    CHECK(!cyberdeck_ble::is_legacy_bond_blob(unrelated, std::string(unrelated).size()));
    /* A CDB1 line that already carries addr_type is not legacy. */
    const char *v2_single =
        "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=Fone;kind=headset;last=1";
    CHECK(!cyberdeck_ble::is_legacy_bond_blob(v2_single, std::string(v2_single).size()));
}

/* REQ-COV-01 cycle 2: deserialize loop-cap and trailing-blank edges plus
 * encode/sanitize/decode guards reachable through the public API. */
void test_coverage_cycle2_store_loops_and_guards()
{
    /* Loop-cap edge: exactly k_max_bonds records parse, then the loop exits
     * on the size guard (not on pos>=len) with trailing blank tolerated. */
    {
        bond_store store;
        std::string blob;
        for (std::size_t i = 0; i < cyberdeck_ble::k_max_bonds; ++i) {
            char address[18];
            std::snprintf(address, sizeof(address), "AA:BB:CC:DD:%02X:%02X",
                          static_cast<unsigned>(i >> 8),
                          static_cast<unsigned>(i & 0xFF));
            char line[128];
            std::snprintf(line, sizeof(line),
                          "CDB1;addr=%s;addr_type=0;name=D;kind=keyboard;last=0\n",
                          address);
            blob += line;
        }
        CHECK(store.deserialize(blob.c_str(), blob.size()));
        CHECK_EQ(store.size(), cyberdeck_ble::k_max_bonds);
        CHECK(blob.size() <= cyberdeck_ble::k_max_store_bytes);
    }
    /* Trailing single blank line is skipped by the line_len==0 path, then
     * the pos<len remainder scan runs over zero bytes (loop not entered). */
    {
        bond_store store;
        const std::string blob =
            "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=A;kind=keyboard;last=0\n\n";
        CHECK(store.deserialize(blob.c_str(), blob.size()));
        CHECK_EQ(store.size(), std::size_t(1));
    }
    /* Duplicate pair where the second record matches on address but differs
     * on addr_type parses fine (no false duplicate): both persist. */
    {
        bond_store store;
        const std::string blob =
            "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=A;kind=keyboard;last=0\n"
            "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=1;name=B;kind=mouse;last=1\n";
        CHECK(store.deserialize(blob.c_str(), blob.size()));
        CHECK_EQ(store.size(), std::size_t(2));
    }
    /* Duplicate pair where address AND addr_type match is rejected
     * atomically (existing.address == record.address true, addr_type true). */
    {
        bond_store store;
        const std::string blob =
            "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=A;kind=keyboard;last=0\n"
            "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=B;kind=mouse;last=1\n";
        CHECK(!store.deserialize(blob.c_str(), blob.size()));
        CHECK_EQ(store.size(), std::size_t(0));
    }
    /* encode_bond name/empty-name path via add: a bond with an empty name
     * serializes with name= empty and round-trips. */
    {
        bond_store store;
        CHECK(store.add(make_record("AA:BB:CC:DD:EE:70", "", device_kind::mouse,
                                    false)) == store_result::ok);
        const std::string blob = store.serialize();
        CHECK(blob.find(";name=;") != std::string::npos);
        bond_store back;
        CHECK(back.deserialize(blob.c_str(), blob.size()));
        const bond_record *kept = back.find("AA:BB:CC:DD:EE:70");
        CHECK(kept != nullptr);
        if (kept != nullptr) CHECK_STR(kept->name, "");
    }
    /* sanitize collapsing edge: leading separator then content trims both
     * ends (front-trim + back-trim loops entered). */
    {
        const std::string encoded = cyberdeck_ble::encode_bond(
            make_record("AA:BB:CC:DD:EE:71", "  padded  ", device_kind::keyboard,
                        false));
        CHECK(encoded.find(";name=padded;") != std::string::npos);
    }
    /* decode field loop: a record whose last field has no trailing ';' still
     * terminates at end (field_end == end break path on the final field). */
    {
        expect_decode("CDB1;addr=AA:BB:CC:DD:EE:72;addr_type=2;name=H;kind=headset;last=1",
                      "AA:BB:CC:DD:EE:72", "H", device_kind::headset, true,
                      cyberdeck_ble::address_type::random_resolvable);
    }
    /* Legacy scan: a CDB1 line shorter than 5 chars is skipped, a v2 second
     * line keeps the blob non-legacy, and a legacy second line detects. */
    {
        const char *short_line = "CDB1\n";
        CHECK(!cyberdeck_ble::is_legacy_bond_blob(short_line,
                                                  std::string(short_line).size()));
        const std::string v2_then_legacy =
            "CDB1;addr=AA:BB:CC:DD:EE:01;addr_type=0;name=A;kind=keyboard;last=0\n"
            "CDB1;addr=AA:BB:CC:DD:EE:02;name=B;kind=mouse;last=1\n";
        CHECK(cyberdeck_ble::is_legacy_bond_blob(v2_then_legacy.c_str(),
                                                 v2_then_legacy.size()));
    }
}

} // namespace

int main()
{
    test_approved_bounds();
    test_encode_is_deterministic_and_field_order_is_fixed();
    test_encode_sanitizes_and_bounds_the_name();
    test_decode_round_trips_and_rejects_everything_else();
    test_decode_refuses_any_smuggled_secret_field();
    test_store_add_update_remove_and_capacity();
    test_snapshot_is_insertion_ordered_and_independent();
    test_serialize_is_bounded_and_deserialize_is_atomic();
    test_all_address_types_round_trip_without_aliasing();
    test_bond_survives_a_reboot_round_trip();
    test_legacy_four_field_blob_is_detected_but_v2_is_not();
    test_coverage_update_remove_find_guards();
    test_coverage_encode_decode_field_bounds();
    test_coverage_crud_normalization_and_missing_paths();
    test_coverage_serialize_deserialize_and_legacy_edges();
    test_coverage_cycle2_store_loops_and_guards();

    std::printf("ble bond store contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
