#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "features/bluetooth/cyberdeck_ble_types.h"

namespace cyberdeck_ble {

/* Bounded store limits. */
inline constexpr std::size_t k_max_bonds = 16;
inline constexpr std::size_t k_max_record_bytes = 96; /* one encoded record   */
inline constexpr std::size_t k_max_store_bytes = 2048;

/* Every encoded record starts with this marker so a truncated or foreign blob
 * is detected instead of being parsed field by field. */
inline constexpr const char *k_bond_magic = "CDB1";

struct bond_record {
    std::string address; /* normalized uppercase MAC */
    std::string name;    /* sanitized and bounded; may be "" */
    device_kind kind = device_kind::unknown;
    bool last_connected = false;
};

enum class store_result {
    ok,
    full,       /* k_max_bonds reached on insert */
    duplicate,  /* insert of an address that already exists */
    not_found,  /* update/remove of an unknown address */
    invalid,    /* unusable address or unrepresentable content */
    too_large,  /* encoded record exceeds k_max_record_bytes */
};

/*
 * Deterministic single-record encoding.  Field order is fixed:
 *   CDB1;addr=<address>;name=<name>;kind=<kind>;last=<0|1>\n
 * The name is sanitized, the address is normalized and the kind is the stable
 * lowercase token.  A trailing LF is always present.
 */
std::string encode_bond(const bond_record &record);

/*
 * Strict inverse of encode_bond.  Rejects a wrong marker, a truncated record,
 * a missing or duplicated field, an unknown field (which is how a key material
 * smuggling attempt is caught), trailing garbage and a non-representable
 * value.  On any failure `out` is left unchanged.
 */
bool decode_bond(const char *text, std::size_t len, bond_record &out);

class bond_store {
public:
    bond_store();
    ~bond_store();
    bond_store(const bond_store &) = delete;
    bond_store &operator=(const bond_store &) = delete;

    store_result add(const bond_record &record);
    store_result update(const bond_record &record);
    bool remove(const std::string &address);

    const bond_record *find(const std::string &address) const;
    std::size_t size() const;
    std::size_t capacity() const;

    /* Insertion ordered copy; the adapter reads this, it does not own it. */
    std::vector<bond_record> snapshot() const;

    /* Bounded whole-store encoding: one encode_bond line per record. */
    std::string serialize() const;

    /*
     * Replaces the content atomically.  Returns false and leaves the store
     * completely untouched when the payload is malformed, contains an unknown
     * field, or exceeds the bounded size.
     */
    bool deserialize(const char *text, std::size_t len);

    void clear();

private:
    struct Impl;
    Impl *pimpl_;
};

} // namespace cyberdeck_ble