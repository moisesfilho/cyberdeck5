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
                        device_kind kind, bool last_connected)
{
    bond_record record;
    record.address = address;
    record.name = name == nullptr ? std::string() : std::string(name);
    record.kind = kind;
    record.last_connected = last_connected;
    return record;
}

void expect_decode(const char *text, const char *address, const char *name,
                   device_kind kind, bool last_connected)
{
    bond_record out = make_record("ZZ:ZZ:ZZ:ZZ:ZZ:ZZ", "Sentinel",
                                  device_kind::keyboard, true);
    const bool ok = cyberdeck_ble::decode_bond(text, std::string(text).size(), out);
    CHECK(ok);
    if (ok) {
        CHECK_STR(out.address, address);
        CHECK_STR(out.name, name);
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
              "CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1\n");

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
              "CDB1;addr=AA:BB:CC:DD:EE:01;name=K;kind=keyboard;last=0\n");
    CHECK_STR(cyberdeck_ble::encode_bond(make_record("AA:BB:CC:DD:EE:01", "M",
                                                     device_kind::mouse, false)),
              "CDB1;addr=AA:BB:CC:DD:EE:01;name=M;kind=mouse;last=0\n");
    CHECK_STR(cyberdeck_ble::encode_bond(make_record("AA:BB:CC:DD:EE:01", nullptr,
                                                     device_kind::unknown, false)),
              "CDB1;addr=AA:BB:CC:DD:EE:01;name=;kind=unknown;last=0\n");
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
    expect_decode("CDB1;addr=AA:BB:CC:DD:EE:01;name=Fone;kind=headset;last=1\n",
                  "AA:BB:CC:DD:EE:01", "Fone", device_kind::headset, true);
    expect_decode("CDB1;addr=AA:BB:CC:DD:EE:01;name=;kind=unknown;last=0\n",
                  "AA:BB:CC:DD:EE:01", "", device_kind::unknown, false);
    /* A record without its trailing LF is still a complete record. */
    expect_decode("CDB1;addr=AA:BB:CC:DD:EE:01;name=K;kind=keyboard;last=0",
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
    CHECK(keeper.deserialize("CDB1;addr=AA:BB:CC:DD:EE:09;name=N;kind=keyboard;last=0\n\n",
                             std::string("CDB1;addr=AA:BB:CC:DD:EE:09;name=N;kind=keyboard;last=0\n\n").size()));
    CHECK_EQ(keeper.size(), std::size_t(1));
    CHECK(!keeper.deserialize("CDB1;addr=AA:BB:CC:DD:EE:09;name=N;kind=keyboard;last=0\r\n",
                              std::string("CDB1;addr=AA:BB:CC:DD:EE:09;name=N;kind=keyboard;last=0\r\n").size()));
    CHECK_EQ(keeper.size(), std::size_t(1));
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
    test_bond_survives_a_reboot_round_trip();

    std::printf("ble bond store contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
