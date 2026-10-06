#include <unity.h>

#include <cstring>

#include "core/nfc_tag.h"

void setUp() {}
void tearDown() {}

void test_family_and_variant_from_sak() {
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Family::MifareClassic),
                      static_cast<int>(nfcTag::familyFromSak(0x08)));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Family::MifareClassic),
                      static_cast<int>(nfcTag::familyFromSak(0x18)));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Family::Type2), static_cast<int>(nfcTag::familyFromSak(0x00)));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Family::Type2), static_cast<int>(nfcTag::familyFromSak(0x04)));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Family::Unknown), static_cast<int>(nfcTag::familyFromSak(0x20)));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Variant::Classic1K),
                      static_cast<int>(nfcTag::classicVariantFromSak(0x08)));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Variant::Classic4K),
                      static_cast<int>(nfcTag::classicVariantFromSak(0x18)));
}

void test_type2_variant_from_cc_size() {
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Variant::Ultralight),
                      static_cast<int>(nfcTag::type2VariantFromCcSize(0x06)));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Variant::Ntag213),
                      static_cast<int>(nfcTag::type2VariantFromCcSize(0x12)));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Variant::Ntag215),
                      static_cast<int>(nfcTag::type2VariantFromCcSize(0x3E)));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Variant::Ntag216),
                      static_cast<int>(nfcTag::type2VariantFromCcSize(0x6D)));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Variant::Unknown),
                      static_cast<int>(nfcTag::type2VariantFromCcSize(0x99)));
}

void test_classic_geometry() {
    TEST_ASSERT_EQUAL_UINT16(64, nfcTag::totalUnits(nfcTag::Variant::Classic1K));
    TEST_ASSERT_EQUAL_UINT16(256, nfcTag::totalUnits(nfcTag::Variant::Classic4K));
    TEST_ASSERT_EQUAL_UINT8(16, nfcTag::unitSize(nfcTag::Variant::Classic1K));
    // Block 0 is the manufacturer block; every 4th block is a trailer.
    TEST_ASSERT_FALSE(nfcTag::classicIsDataBlock(nfcTag::Variant::Classic1K, 0));
    TEST_ASSERT_TRUE(nfcTag::classicIsTrailerBlock(nfcTag::Variant::Classic1K, 3));
    TEST_ASSERT_FALSE(nfcTag::classicIsDataBlock(nfcTag::Variant::Classic1K, 3));
    TEST_ASSERT_TRUE(nfcTag::classicIsDataBlock(nfcTag::Variant::Classic1K, 1));
    TEST_ASSERT_TRUE(nfcTag::classicIsDataBlock(nfcTag::Variant::Classic1K, 62));
    TEST_ASSERT_TRUE(nfcTag::classicIsTrailerBlock(nfcTag::Variant::Classic1K, 63));
    TEST_ASSERT_EQUAL_UINT16(47, nfcTag::classicDataBlockCount(nfcTag::Variant::Classic1K));

    // 4K: sectors 32..39 are 16 blocks each, trailer on the last block.
    TEST_ASSERT_TRUE(nfcTag::classicIsTrailerBlock(nfcTag::Variant::Classic4K, 127));
    TEST_ASSERT_TRUE(nfcTag::classicIsDataBlock(nfcTag::Variant::Classic4K, 128));
    TEST_ASSERT_TRUE(nfcTag::classicIsTrailerBlock(nfcTag::Variant::Classic4K, 143));
    TEST_ASSERT_FALSE(nfcTag::classicIsDataBlock(nfcTag::Variant::Classic4K, 143));
    TEST_ASSERT_TRUE(nfcTag::classicIsTrailerBlock(nfcTag::Variant::Classic4K, 255));
    TEST_ASSERT_EQUAL_UINT16(215, nfcTag::classicDataBlockCount(nfcTag::Variant::Classic4K));
}

void test_type2_geometry_protects_config_pages() {
    TEST_ASSERT_EQUAL_UINT16(4, nfcTag::unitSize(nfcTag::Variant::Ntag213));
    TEST_ASSERT_EQUAL_UINT16(45, nfcTag::totalUnits(nfcTag::Variant::Ntag213));
    TEST_ASSERT_EQUAL_UINT16(39, nfcTag::type2UserLastPage(nfcTag::Variant::Ntag213));
    TEST_ASSERT_EQUAL_UINT16(129, nfcTag::type2UserLastPage(nfcTag::Variant::Ntag215));
    TEST_ASSERT_EQUAL_UINT16(225, nfcTag::type2UserLastPage(nfcTag::Variant::Ntag216));
    TEST_ASSERT_EQUAL_UINT16(144, nfcTag::type2UserCapacityBytes(nfcTag::Variant::Ntag213));
    TEST_ASSERT_EQUAL_UINT16(504, nfcTag::type2UserCapacityBytes(nfcTag::Variant::Ntag215));
    TEST_ASSERT_EQUAL_UINT16(888, nfcTag::type2UserCapacityBytes(nfcTag::Variant::Ntag216));
    TEST_ASSERT_EQUAL_UINT16(48, nfcTag::type2UserCapacityBytes(nfcTag::Variant::Ultralight));
    TEST_ASSERT_EQUAL_UINT16(0, nfcTag::type2UserCapacityBytes(nfcTag::Variant::Unknown));
}

void test_dump_round_trip() {
    uint8_t units[64];
    for (size_t i = 0; i < sizeof(units); ++i) units[i] = static_cast<uint8_t>(i);

    nfcTag::Dump dump;
    dump.id.uid[0] = 0x04;
    dump.id.uid[1] = 0xAB;
    dump.id.uid[2] = 0x01;
    dump.id.uid[3] = 0x02;
    dump.id.uidLength = 4;
    dump.id.sak = 0x08;
    dump.id.atqa[0] = 0x04;
    dump.id.atqa[1] = 0x00;
    dump.family = nfcTag::Family::MifareClassic;
    dump.variant = nfcTag::Variant::Classic1K;
    dump.unitSize = 16;
    dump.storedUnits = 4;
    dump.data = units;
    dump.dataLength = sizeof(units);

    uint8_t encoded[nfcTag::kDumpHeaderBytes + 64];
    size_t length = 0;
    TEST_ASSERT_TRUE(nfcTag::encodeDump(dump, encoded, sizeof(encoded), length));
    TEST_ASSERT_EQUAL_UINT32(nfcTag::kDumpHeaderBytes + 64, length);

    nfcTag::Dump decoded;
    TEST_ASSERT_TRUE(nfcTag::decodeDump(encoded, length, decoded));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Family::MifareClassic), static_cast<int>(decoded.family));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcTag::Variant::Classic1K), static_cast<int>(decoded.variant));
    TEST_ASSERT_EQUAL_UINT8(16, decoded.unitSize);
    TEST_ASSERT_EQUAL_UINT16(4, decoded.storedUnits);
    TEST_ASSERT_EQUAL_UINT8(4, decoded.id.uidLength);
    TEST_ASSERT_EQUAL_MEMORY(dump.id.uid, decoded.id.uid, sizeof(dump.id.uid));
    TEST_ASSERT_EQUAL_MEMORY(units, decoded.data, 64);
}

void test_dump_truncates_to_capacity_and_flags_it() {
    uint8_t units[256] = {};
    nfcTag::Dump dump;
    dump.id.uidLength = 4;
    dump.family = nfcTag::Family::MifareClassic;
    dump.variant = nfcTag::Variant::Classic4K;
    dump.unitSize = 16;
    dump.storedUnits = 256; // 4096 bytes of payload
    dump.data = units;
    dump.dataLength = sizeof(units);

    uint8_t encoded[nfcTag::kDumpHeaderBytes + 64];
    size_t length = 0;
    TEST_ASSERT_TRUE(nfcTag::encodeDump(dump, encoded, sizeof(encoded), length));
    TEST_ASSERT_EQUAL_UINT32(nfcTag::kDumpHeaderBytes + 64, length);

    nfcTag::Dump decoded;
    TEST_ASSERT_TRUE(nfcTag::decodeDump(encoded, length, decoded));
    TEST_ASSERT_EQUAL_UINT16(4, decoded.storedUnits);
    TEST_ASSERT_TRUE(decoded.truncated);
}

void test_dump_rejects_bad_input() {
    nfcTag::Dump decoded;
    uint8_t tooShort[10] = {};
    TEST_ASSERT_FALSE(nfcTag::decodeDump(tooShort, sizeof(tooShort), decoded));

    uint8_t encoded[nfcTag::kDumpHeaderBytes + 8];
    nfcTag::Dump dump;
    dump.id.uidLength = 4;
    dump.family = nfcTag::Family::Type2;
    dump.variant = nfcTag::Variant::Ntag213;
    dump.unitSize = 4;
    dump.storedUnits = 2;
    dump.data = encoded + nfcTag::kDumpHeaderBytes; // arbitrary non-null
    size_t length = 0;
    TEST_ASSERT_TRUE(nfcTag::encodeDump(dump, encoded, sizeof(encoded), length));

    encoded[0] = 'X'; // corrupt magic
    TEST_ASSERT_FALSE(nfcTag::decodeDump(encoded, length, decoded));
    encoded[0] = 'A';
    encoded[4] = 9; // unsupported version
    TEST_ASSERT_FALSE(nfcTag::decodeDump(encoded, length, decoded));
    encoded[4] = 1;
    // Declared units exceed the buffer.
    encoded[14] = 0xFF;
    encoded[15] = 0xFF;
    TEST_ASSERT_FALSE(nfcTag::decodeDump(encoded, length, decoded));
}

void test_encode_dump_rejects_invalid_unit_size() {
    uint8_t out[nfcTag::kDumpHeaderBytes + 8];
    size_t length = 0;
    nfcTag::Dump dump;
    dump.unitSize = 7;
    dump.storedUnits = 2;
    dump.data = out;
    TEST_ASSERT_FALSE(nfcTag::encodeDump(dump, out, sizeof(out), length));
}

void test_classic_block0_splice_sets_uid_and_bcc_preserving_manufacturer() {
    const uint8_t uid[4] = {0x04, 0xDE, 0xAD, 0xBE};
    uint8_t block[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                         0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    uint8_t tail[11];
    std::memcpy(tail, block + 5, sizeof(tail));

    TEST_ASSERT_TRUE(nfcTag::spliceClassicBlock0(uid, 4, block));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(uid, block, 4);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(0x04 ^ 0xDE ^ 0xAD ^ 0xBE), block[4]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tail, block + 5, sizeof(tail)); // SAK/ATQA/manufacturer kept

    // MIFARE Classic only carries a 4-byte UID.
    TEST_ASSERT_FALSE(nfcTag::spliceClassicBlock0(uid, 7, block));
    TEST_ASSERT_FALSE(nfcTag::spliceClassicBlock0(nullptr, 4, block));
}

void test_type2_splice_7byte_uid_preserves_lock_and_internal() {
    const uint8_t uid[7] = {0x04, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};
    uint8_t pages[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0x5A, 0x01, 0x02};
    TEST_ASSERT_TRUE(nfcTag::spliceType2ManufacturerPages(uid, 7, pages));
    TEST_ASSERT_EQUAL_UINT8(uid[0], pages[0]);
    TEST_ASSERT_EQUAL_UINT8(uid[1], pages[1]);
    TEST_ASSERT_EQUAL_UINT8(uid[2], pages[2]);
    // BCC0 covers the cascade tag plus UID0..2.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(0x88 ^ uid[0] ^ uid[1] ^ uid[2]), pages[3]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(uid + 3, pages + 4, 4);
    // BCC1 = UID3 ^ UID4 ^ UID5 ^ UID6.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(uid[3] ^ uid[4] ^ uid[5] ^ uid[6]), pages[8]);
    TEST_ASSERT_EQUAL_UINT8(0x5A, pages[9]); // internal byte preserved
    TEST_ASSERT_EQUAL_UINT8(0x01, pages[10]); // static lock preserved
    TEST_ASSERT_EQUAL_UINT8(0x02, pages[11]);
}

void test_type2_splice_4byte_uid_and_rejects_other_lengths() {
    const uint8_t uid[4] = {0x04, 0xAA, 0xBB, 0xCC};
    uint8_t pages[12];
    for (size_t i = 0; i < sizeof(pages); ++i) pages[i] = static_cast<uint8_t>(0x10 + i);
    TEST_ASSERT_TRUE(nfcTag::spliceType2ManufacturerPages(uid, 4, pages));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(uid, pages, 4);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(0x04 ^ 0xAA ^ 0xBB ^ 0xCC), pages[4]);
    TEST_ASSERT_EQUAL_UINT8(0x15, pages[5]); // page 1 bytes 1..3 preserved
    TEST_ASSERT_EQUAL_UINT8(0x1B, pages[11]);

    TEST_ASSERT_FALSE(nfcTag::spliceType2ManufacturerPages(uid, 5, pages));
    TEST_ASSERT_FALSE(nfcTag::spliceType2ManufacturerPages(nullptr, 7, pages));
}

void test_type2_dynamic_lock_page() {
    TEST_ASSERT_EQUAL_UINT16(40, nfcTag::type2DynamicLockPage(nfcTag::Variant::Ntag213));
    TEST_ASSERT_EQUAL_UINT16(130, nfcTag::type2DynamicLockPage(nfcTag::Variant::Ntag215));
    TEST_ASSERT_EQUAL_UINT16(226, nfcTag::type2DynamicLockPage(nfcTag::Variant::Ntag216));
    TEST_ASSERT_EQUAL_UINT16(0, nfcTag::type2DynamicLockPage(nfcTag::Variant::Ultralight));
    TEST_ASSERT_EQUAL_UINT16(0, nfcTag::type2DynamicLockPage(nfcTag::Variant::Classic1K));
    TEST_ASSERT_EQUAL_UINT16(0, nfcTag::type2DynamicLockPage(nfcTag::Variant::Unknown));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_family_and_variant_from_sak);
    RUN_TEST(test_type2_variant_from_cc_size);
    RUN_TEST(test_classic_geometry);
    RUN_TEST(test_type2_geometry_protects_config_pages);
    RUN_TEST(test_dump_round_trip);
    RUN_TEST(test_dump_truncates_to_capacity_and_flags_it);
    RUN_TEST(test_dump_rejects_bad_input);
    RUN_TEST(test_encode_dump_rejects_invalid_unit_size);
    RUN_TEST(test_classic_block0_splice_sets_uid_and_bcc_preserving_manufacturer);
    RUN_TEST(test_type2_splice_7byte_uid_preserves_lock_and_internal);
    RUN_TEST(test_type2_splice_4byte_uid_and_rejects_other_lengths);
    RUN_TEST(test_type2_dynamic_lock_page);
    return UNITY_END();
}
