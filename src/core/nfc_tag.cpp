#include "nfc_tag.h"

#include <cstring>

namespace nfcTag {

namespace {

constexpr uint8_t kDumpMagic[4] = {'A', 'T', 'K', 'N'};
constexpr uint8_t kDumpVersion = 1;

constexpr uint8_t kFlagTruncated = 0x01;
constexpr uint8_t kFlagHoles = 0x02;

bool validUnitSize(uint8_t unitSize) { return unitSize == 4 || unitSize == 16; }

} // namespace

Family familyFromSak(uint8_t sak) {
    switch (sak) {
    case 0x08:
    case 0x18:
        return Family::MifareClassic;
    case 0x00:
    case 0x04:
        return Family::Type2;
    default:
        return Family::Unknown;
    }
}

Variant classicVariantFromSak(uint8_t sak) {
    switch (sak) {
    case 0x08: return Variant::Classic1K;
    case 0x18: return Variant::Classic4K;
    default: return Variant::Unknown;
    }
}

Variant type2VariantFromCcSize(uint8_t ccSize) {
    switch (ccSize) {
    case 0x06: return Variant::Ultralight;
    case 0x12: return Variant::Ntag213;
    case 0x3E: return Variant::Ntag215;
    case 0x6D: return Variant::Ntag216;
    default: return Variant::Unknown;
    }
}

const char *familyName(Family family) {
    switch (family) {
    case Family::MifareClassic: return "mifare_classic";
    case Family::Type2: return "type2";
    case Family::Unknown: break;
    }
    return "unknown";
}

const char *variantName(Variant variant) {
    switch (variant) {
    case Variant::Classic1K: return "mifare_classic_1k";
    case Variant::Classic4K: return "mifare_classic_4k";
    case Variant::Ultralight: return "mifare_ultralight";
    case Variant::Ntag213: return "ntag213";
    case Variant::Ntag215: return "ntag215";
    case Variant::Ntag216: return "ntag216";
    case Variant::Unknown: break;
    }
    return "unknown";
}

uint16_t totalUnits(Variant variant) {
    switch (variant) {
    case Variant::Classic1K: return 64;
    case Variant::Classic4K: return 256;
    case Variant::Ultralight: return 16;
    case Variant::Ntag213: return 45;
    case Variant::Ntag215: return 135;
    case Variant::Ntag216: return 231;
    case Variant::Unknown: break;
    }
    return 0;
}

uint8_t unitSize(Variant variant) {
    switch (variant) {
    case Variant::Classic1K:
    case Variant::Classic4K: return 16;
    case Variant::Ultralight:
    case Variant::Ntag213:
    case Variant::Ntag215:
    case Variant::Ntag216: return 4;
    case Variant::Unknown: break;
    }
    return 0;
}

bool isType2(Variant variant) { return unitSize(variant) == 4; }

uint16_t type2UserLastPage(Variant variant) {
    switch (variant) {
    case Variant::Ultralight: return 15;  // 16 pages, all of 4..15 user
    case Variant::Ntag213: return 39;     // 40 dynamic lock, 41..44 config
    case Variant::Ntag215: return 129;    // 130 dynamic lock, 131..134 config
    case Variant::Ntag216: return 225;    // 226 dynamic lock, 227..230 config
    case Variant::Classic1K:
    case Variant::Classic4K:
    case Variant::Unknown: break;
    }
    return 0;
}

uint16_t type2UserCapacityBytes(Variant variant) {
    if (!isType2(variant) || variant == Variant::Unknown) return 0;
    const uint16_t first = 4;
    const uint16_t last = type2UserLastPage(variant);
    if (last < first) return 0;
    return static_cast<uint16_t>((last - first + 1) * 4);
}

uint16_t type2DynamicLockPage(Variant variant) {
    switch (variant) {
    case Variant::Ntag213: return 40;  // 0x28: Lock2/Lock3 + RFUI
    case Variant::Ntag215: return 130; // 0x82
    case Variant::Ntag216: return 226; // 0xE2
    case Variant::Ultralight:
    case Variant::Classic1K:
    case Variant::Classic4K:
    case Variant::Unknown: break;
    }
    return 0;
}

bool classicIsTrailerBlock(Variant variant, uint16_t block) {
    if (variant == Variant::Classic1K) return block < 64 && (block % 4) == 3;
    if (variant == Variant::Classic4K) {
        if (block >= 256) return false;
        if (block < 128) return (block % 4) == 3;
        return ((block - 128) % 16) == 15;
    }
    return false;
}

bool classicIsDataBlock(Variant variant, uint16_t block) {
    if (variant != Variant::Classic1K && variant != Variant::Classic4K) return false;
    if (block >= totalUnits(variant)) return false;
    if (block == 0) return false; // manufacturer/UID block
    return !classicIsTrailerBlock(variant, block);
}

uint16_t classicDataBlockCount(Variant variant) {
    const uint16_t units = totalUnits(variant);
    if (units == 0) return 0;
    uint16_t count = 0;
    for (uint16_t b = 0; b < units; ++b) {
        if (classicIsDataBlock(variant, b)) ++count;
    }
    return count;
}

namespace {

// XOR of `length` bytes, used for both Classic and Type 2 block check bytes.
uint8_t xorBytes(const uint8_t *data, size_t length) {
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) sum = static_cast<uint8_t>(sum ^ data[i]);
    return sum;
}

// Cascade tag transmitted as the first byte of a double-size UID's first
// anticollision level; BCC0 covers it (ISO/IEC 14443-3 §6.4.4).
constexpr uint8_t kCascadeTag = 0x88;

} // namespace

bool spliceClassicBlock0(const uint8_t *uid, uint8_t uidLength, uint8_t *block0) {
    if (uid == nullptr || block0 == nullptr) return false;
    if (uidLength != 4) return false; // MIFARE Classic UIDs are single-size
    for (uint8_t i = 0; i < 4; ++i) block0[i] = uid[i];
    block0[4] = xorBytes(uid, 4); // BCC over the four UID bytes
    // block0[5..15] (SAK, ATQA, manufacturer data) stay as the target card had.
    return true;
}

bool spliceType2ManufacturerPages(const uint8_t *uid, uint8_t uidLength, uint8_t *pages) {
    if (uid == nullptr || pages == nullptr) return false;
    if (uidLength == 7) {
        pages[0] = uid[0];
        pages[1] = uid[1];
        pages[2] = uid[2];
        pages[3] = static_cast<uint8_t>(kCascadeTag ^ uid[0] ^ uid[1] ^ uid[2]); // BCC0
        pages[4] = uid[3];
        pages[5] = uid[4];
        pages[6] = uid[5];
        pages[7] = uid[6];
        pages[8] = xorBytes(uid + 3, 4); // BCC1 = UID3^UID4^UID5^UID6
        // pages[9] (internal) and pages[10..11] (static lock) stay as the target
        // card had: overwriting lock bytes could permanently brick the tag.
        return true;
    }
    if (uidLength == 4) {
        for (uint8_t i = 0; i < 4; ++i) pages[i] = uid[i];
        pages[4] = xorBytes(uid, 4); // BCC stored in page 1 byte 0
        // pages[5..11] preserved from the target.
        return true;
    }
    return false;
}

bool encodeDump(const Dump &dump, uint8_t *out, size_t capacity, size_t &outLength) {
    outLength = 0;
    if (out == nullptr || capacity < kDumpHeaderBytes) return false;
    if (!validUnitSize(dump.unitSize)) return false;
    if (dump.storedUnits > 0 && dump.data == nullptr) return false;

    size_t units = dump.storedUnits;
    const size_t available = capacity - kDumpHeaderBytes;
    bool truncated = dump.truncated;
    if (units * dump.unitSize > available) {
        units = available / dump.unitSize;
        truncated = true;
    }
    const size_t dataBytes = units * dump.unitSize;

    std::memcpy(out, kDumpMagic, sizeof(kDumpMagic));
    out[4] = kDumpVersion;
    out[5] = static_cast<uint8_t>(dump.family);
    out[6] = static_cast<uint8_t>(dump.variant);
    out[7] = dump.unitSize;
    out[8] = dump.id.uidLength;
    out[9] = dump.id.sak;
    out[10] = dump.id.atqa[0];
    out[11] = dump.id.atqa[1];
    out[12] = static_cast<uint8_t>((truncated ? kFlagTruncated : 0) | (dump.holes ? kFlagHoles : 0));
    out[13] = 0;
    out[14] = static_cast<uint8_t>(units & 0xFF);
    out[15] = static_cast<uint8_t>((units >> 8) & 0xFF);
    std::memcpy(out + 16, dump.id.uid, sizeof(dump.id.uid));
    if (dataBytes > 0) std::memcpy(out + kDumpHeaderBytes, dump.data, dataBytes);
    outLength = kDumpHeaderBytes + dataBytes;
    return true;
}

bool decodeDump(const uint8_t *data, size_t length, Dump &out) {
    out = Dump{};
    if (data == nullptr || length < kDumpHeaderBytes) return false;
    if (std::memcmp(data, kDumpMagic, sizeof(kDumpMagic)) != 0) return false;
    if (data[4] != kDumpVersion) return false;
    if (data[5] > static_cast<uint8_t>(Family::Type2)) return false;
    if (data[8] > 10) return false;
    if (!validUnitSize(data[7])) return false;

    const uint16_t units = static_cast<uint16_t>(data[14]) | (static_cast<uint16_t>(data[15]) << 8);
    const size_t dataBytes = static_cast<size_t>(units) * data[7];
    if (dataBytes > length - kDumpHeaderBytes) return false;

    out.family = static_cast<Family>(data[5]);
    out.variant = static_cast<Variant>(data[6]);
    out.unitSize = data[7];
    out.id.uidLength = data[8];
    out.id.sak = data[9];
    out.id.atqa[0] = data[10];
    out.id.atqa[1] = data[11];
    out.truncated = (data[12] & kFlagTruncated) != 0;
    out.holes = (data[12] & kFlagHoles) != 0;
    out.storedUnits = units;
    std::memcpy(out.id.uid, data + 16, sizeof(out.id.uid));
    out.data = data + kDumpHeaderBytes;
    out.dataLength = dataBytes;
    return true;
}

} // namespace nfcTag
