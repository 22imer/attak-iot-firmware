// Portable NFC tag model: family/variant detection, memory geometry, the RAM
// dump container exchanged between nfc_read_dump and nfc_clone_uid, and the
// safe unit ranges used by erase. No Arduino, no I2C, no allocation — the
// PN532 module owns the bus; this owns the tag semantics so they are
// native-testable.
//
// References studied for technique only (original code):
//   - NXP MIFARE Classic 1K/4K: 16-byte blocks, 4-block sectors (4K adds
//     16-block sectors 32..39), block 0 manufacturer/UID, last block of each
//     sector is the trailer (keys + access bits).
//   - NXP NTAG213/215/216 and MIFARE Ultralight (NFC Forum Type 2): 4-byte
//     pages, page 0..2 UID/BCC/lock, page 3 Capability Container, user pages
//     from 4, configuration/password pages at the top of the small variants.
//   - libnfc / Proxmark3 magic-card notes (Gen2/CUID direct block-0 write).
#pragma once

#include <cstddef>
#include <cstdint>

namespace nfcTag {

enum class Family : uint8_t { Unknown = 0, MifareClassic = 1, Type2 = 2 };

enum class Variant : uint8_t {
    Unknown = 0,
    Classic1K = 1,
    Classic4K = 2,
    Ultralight = 3,
    Ntag213 = 4,
    Ntag215 = 5,
    Ntag216 = 6,
};

struct Identity {
    uint8_t uid[10]{};
    uint8_t uidLength = 0;
    uint8_t atqa[2]{};
    uint8_t sak = 0;
};

// SAK 0x08/0x18 are the MIFARE Classic 1K/4K signatures; SAK 0x00/0x04 are the
// common Type 2 (Ultralight/NTAG) signatures. SAK 0x20 (ISO14443-4, e.g.
// DESFire) and everything else is out of scope.
Family familyFromSak(uint8_t sak);
Variant classicVariantFromSak(uint8_t sak);
// Capability Container page-3 size byte -> variant. 0x06 Ultralight, 0x12
// NTAG213, 0x3E NTAG215, 0x6D NTAG216 (NXP datasheet / go-pn532 reference).
Variant type2VariantFromCcSize(uint8_t ccSize);

const char *familyName(Family family);
const char *variantName(Variant variant);

// Geometry. `unit` is a 16-byte Classic block or a 4-byte Type 2 page.
uint16_t totalUnits(Variant variant); // NTAG/Classic capacity, Unknown -> 0
uint8_t unitSize(Variant variant);    // 16 Classic, 4 Type 2, 0 Unknown
bool isType2(Variant variant);

// Type 2: last user page that may be written (excludes dynamic lock, config,
// password and PACK pages). Unknown -> 0 (caller must not write blind).
uint16_t type2UserLastPage(Variant variant);
uint16_t type2UserCapacityBytes(Variant variant);

// Page whose first two bytes are the dynamic lock bytes for a Type 2 variant,
// or 0 when the variant has none (Ultralight/Unknown). NTAG213 -> 40,
// NTAG215 -> 130, NTAG216 -> 226; a non-zero lock byte means some user pages
// from page 16 on are read-only.
uint16_t type2DynamicLockPage(Variant variant);

// MIFARE Classic block classification (block 0 and 16-byte trailers are fixed).
bool classicIsTrailerBlock(Variant variant, uint16_t block);
bool classicIsDataBlock(Variant variant, uint16_t block);

// Number of data blocks (writable user area) for a Classic variant.
uint16_t classicDataBlockCount(Variant variant);

// Writable-UID (CUID/Gen2 magic) manufacturer splice. Both helpers rewrite only
// the UID-bearing bytes in a caller-supplied copy of the target card's own
// manufacturer region, so the target's fixed fields survive: never a blind
// source-block copy. The caller reads the region from the target first, then
// these overwrite the UID bytes and recompute the check bytes.
//
// Classic block 0 (16 bytes): bytes 0..3 = UID, byte 4 = BCC = UID0^UID1^UID2^
// UID3, bytes 5..15 (SAK/ATQA/manufacturer data) preserved. Requires a 4-byte
// UID; returns false otherwise.
bool spliceClassicBlock0(const uint8_t *uid, uint8_t uidLength, uint8_t *block0);

// Type 2 manufacturer pages 0..2 (12 bytes, in/out).
//   7-byte UID (NTAG/Ultralight double-size, the norm): page0 = UID0..2 + BCC0,
//   page1 = UID3..6, page2 byte 0 = BCC1, page2 bytes 1..3 (internal + static
//   lock) preserved. BCC0 = 0x88 ^ UID0 ^ UID1 ^ UID2 (0x88 is the cascade
//   tag), BCC1 = UID3 ^ UID4 ^ UID5 ^ UID6.
//   4-byte UID: page0 = UID0..3, page1 byte 0 = BCC = UID0^UID1^UID2^UID3,
//   page1 bytes 1..3 and page2 preserved.
// Returns false for any other UID length.
bool spliceType2ManufacturerPages(const uint8_t *uid, uint8_t uidLength, uint8_t *pages);

// Fixed static lock bytes for Type 2 live in page 2, bytes 2 and 3.
constexpr uint8_t kType2StaticLockPage = 2;

// RAM dump container. `data` holds storedUnits * unitSize bytes, indexed by
// unit number (Classic block / Type 2 page); unread Classic sectors are 0xFF.
struct Dump {
    Identity id{};
    Family family = Family::Unknown;
    Variant variant = Variant::Unknown;
    uint8_t unitSize = 0;
    uint16_t storedUnits = 0;
    bool truncated = false;
    bool holes = false; // some stored units are 0xFF fill, not read data
    const uint8_t *data = nullptr;
    size_t dataLength = 0;
};

// Wire format: 26-byte header ('A','T','K','N', version, family, variant,
// unitSize, uidLength, sak, atqa[2], flags, reserved, storedUnits LE16,
// uid[10]) followed by the raw units. Truncates the unit payload to fit
// `capacity` and sets the truncated flag. Returns false only for invalid
// geometry or a null payload.
constexpr size_t kDumpHeaderBytes = 26;
bool encodeDump(const Dump &dump, uint8_t *out, size_t capacity, size_t &outLength);
bool decodeDump(const uint8_t *data, size_t length, Dump &out);

} // namespace nfcTag
