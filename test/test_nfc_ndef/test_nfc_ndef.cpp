#include <unity.h>

#include <string>

#include "core/nfc_ndef.h"

void setUp() {}
void tearDown() {}

void test_text_record_known_bytes() {
    uint8_t out[64];
    const size_t n = nfcNdef::encodeTextRecord("Hi", out, sizeof(out));
    const uint8_t expected[] = {0xD1, 0x01, 0x05, 'T', 0x02, 'e', 'n', 'H', 'i'};
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), n);
    TEST_ASSERT_EQUAL_MEMORY(expected, out, n);
}

void test_text_record_round_trip_utf8() {
    const std::string text = "Xin chào é";
    uint8_t out[128];
    const size_t n = nfcNdef::encodeTextRecord(text, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);

    std::string language;
    std::string decoded;
    TEST_ASSERT_TRUE(nfcNdef::decodeTextRecord(out, n, language, decoded));
    TEST_ASSERT_EQUAL_STRING("en", language.c_str());
    TEST_ASSERT_EQUAL_STRING(text.c_str(), decoded.c_str());
    TEST_ASSERT_EQUAL_UINT32(text.size(), decoded.size());
}

void test_text_record_rejects_empty_and_oversize() {
    uint8_t out[128];
    TEST_ASSERT_EQUAL_UINT32(0, nfcNdef::encodeTextRecord("", out, sizeof(out)));
    std::string tooLong(65, 'a');
    TEST_ASSERT_EQUAL_UINT32(0, nfcNdef::encodeTextRecord(tooLong, out, sizeof(out)));
    // 64 UTF-8 bytes is exactly the catalog cap and must be accepted.
    std::string atCap(64, 'a');
    TEST_ASSERT_TRUE(nfcNdef::encodeTextRecord(atCap, out, sizeof(out)) > 0);
}

void test_text_record_capacity_guard() {
    uint8_t out[8];
    TEST_ASSERT_EQUAL_UINT32(0, nfcNdef::encodeTextRecord("Hi", out, sizeof(out)));
}

void test_decode_rejects_non_text_and_malformed() {
    std::string language, text;
    // URI record type 'U' is not a Text record.
    const uint8_t uri[] = {0xD1, 0x01, 0x03, 'U', 0x01, 'x'};
    TEST_ASSERT_FALSE(nfcNdef::decodeTextRecord(uri, sizeof(uri), language, text));
    // Truncated payload.
    const uint8_t truncated[] = {0xD1, 0x01, 0x20, 'T', 0x02, 'e', 'n', 'H'};
    TEST_ASSERT_FALSE(nfcNdef::decodeTextRecord(truncated, sizeof(truncated), language, text));
    TEST_ASSERT_FALSE(nfcNdef::decodeTextRecord(nullptr, 0, language, text));
}

void test_find_ndef_tlv_after_lock_control() {
    const uint8_t data[] = {0x01, 0x03, 0xA0, 0x10, 0x44, 0x03, 0x05, 0xD1, 0x01,
                            0x01, 'T', 0x02, 'e', 'n', 0xFE};
    nfcNdef::Tlv tlv;
    TEST_ASSERT_TRUE(nfcNdef::findNdefTlv(data, sizeof(data), tlv));
    TEST_ASSERT_EQUAL_UINT32(5, tlv.offset);
    TEST_ASSERT_EQUAL_UINT32(2, tlv.header);
    TEST_ASSERT_EQUAL_UINT32(5, tlv.length);
}

void test_find_ndef_tlv_long_length() {
    uint8_t data[8] = {0x03, 0xFF, 0x01, 0x2C, 0x00, 0x00, 0x00, 0xFE};
    nfcNdef::Tlv tlv;
    TEST_ASSERT_TRUE(nfcNdef::findNdefTlv(data, sizeof(data), tlv));
    TEST_ASSERT_EQUAL_UINT32(0, tlv.offset);
    TEST_ASSERT_EQUAL_UINT32(4, tlv.header);
    TEST_ASSERT_EQUAL_UINT32(300, tlv.length);
}

void test_find_ndef_tlv_stops_at_terminator_and_null_padding() {
    const uint8_t padThenTerminator[] = {0x00, 0x00, 0x00, 0xFE, 0x03, 0x01, 0xAA};
    nfcNdef::Tlv tlv;
    TEST_ASSERT_FALSE(nfcNdef::findNdefTlv(padThenTerminator, sizeof(padThenTerminator), tlv));
}

void test_prefix_length() {
    const uint8_t withLock[] = {0x01, 0x03, 0xA0, 0x10, 0x44, 0x03, 0x05, 0x00};
    TEST_ASSERT_EQUAL_UINT32(5, nfcNdef::prefixLength(withLock, sizeof(withLock)));
    const uint8_t startsNdef[] = {0x03, 0x05, 0x00};
    TEST_ASSERT_EQUAL_UINT32(0, nfcNdef::prefixLength(startsNdef, sizeof(startsNdef)));
    const uint8_t empty[] = {0xFE, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT32(0, nfcNdef::prefixLength(empty, sizeof(empty)));
    const uint8_t nulls[] = {0x00, 0x00, 0x03, 0x00};
    TEST_ASSERT_EQUAL_UINT32(2, nfcNdef::prefixLength(nulls, sizeof(nulls)));
}

void test_encode_area_preserves_prefix_and_locates_ndef() {
    const uint8_t prefix[] = {0x01, 0x03, 0xA0, 0x10, 0x44};
    uint8_t message[16];
    const std::string text = "hello";
    const size_t messageBytes = nfcNdef::encodeTextRecord(text, message, sizeof(message));
    TEST_ASSERT_TRUE(messageBytes > 0);

    uint8_t out[128];
    nfcNdef::AreaPlan plan;
    const size_t total = nfcNdef::encodeArea(prefix, sizeof(prefix), message, messageBytes, out, sizeof(out), plan);
    TEST_ASSERT_TRUE(total > 0);
    TEST_ASSERT_EQUAL_UINT32(total, plan.totalBytes);
    TEST_ASSERT_EQUAL_UINT32(5 + 2, plan.ndefOffset);
    TEST_ASSERT_EQUAL_UINT32(messageBytes, plan.ndefLength);
    TEST_ASSERT_EQUAL_MEMORY(prefix, out, sizeof(prefix));
    TEST_ASSERT_EQUAL_UINT8(0x03, out[5]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(messageBytes), out[6]);
    TEST_ASSERT_EQUAL_MEMORY(message, out + 7, messageBytes);
    TEST_ASSERT_EQUAL_UINT8(0xFE, out[7 + messageBytes]);

    // The composed area must be rediscovable as one NDEF TLV.
    nfcNdef::Tlv tlv;
    TEST_ASSERT_TRUE(nfcNdef::findNdefTlv(out, plan.totalBytes, tlv));
    TEST_ASSERT_EQUAL_UINT32(plan.ndefOffset - tlv.header, tlv.offset);
    TEST_ASSERT_EQUAL_UINT32(messageBytes, tlv.length);
}

void test_encode_area_capacity_and_overflow_guard() {
    uint8_t message[16];
    const size_t messageBytes = nfcNdef::encodeTextRecord("hi", message, sizeof(message));
    uint8_t out[4];
    nfcNdef::AreaPlan plan;
    TEST_ASSERT_EQUAL_UINT32(0, nfcNdef::encodeArea(nullptr, 0, message, messageBytes, out, sizeof(out), plan));
    TEST_ASSERT_EQUAL_UINT32(0, nfcNdef::encodeArea(nullptr, 0, message, 300, out, sizeof(out), plan));
}

void test_encode_empty_area() {
    uint8_t out[16];
    const size_t n = nfcNdef::encodeEmptyArea(8, out, sizeof(out));
    const uint8_t expected[] = {0x03, 0x00, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT32(8, n);
    TEST_ASSERT_EQUAL_MEMORY(expected, out, 8);
    TEST_ASSERT_EQUAL_UINT32(0, nfcNdef::encodeEmptyArea(2, out, sizeof(out)));

    // An empty area must read back as an empty NDEF TLV.
    nfcNdef::Tlv tlv;
    TEST_ASSERT_TRUE(nfcNdef::findNdefTlv(out, n, tlv));
    TEST_ASSERT_EQUAL_UINT32(0, tlv.length);
}

void test_page_helpers() {
    TEST_ASSERT_EQUAL_UINT32(0, nfcNdef::pageAligned(0));
    TEST_ASSERT_EQUAL_UINT32(4, nfcNdef::pageAligned(1));
    TEST_ASSERT_EQUAL_UINT32(4, nfcNdef::pageAligned(4));
    TEST_ASSERT_EQUAL_UINT32(8, nfcNdef::pageAligned(5));
    TEST_ASSERT_EQUAL_UINT32(0, nfcNdef::pagesFor(0));
    TEST_ASSERT_EQUAL_UINT32(1, nfcNdef::pagesFor(1));
    TEST_ASSERT_EQUAL_UINT32(2, nfcNdef::pagesFor(5));
}

void test_scan_control_tlvs_verdicts() {
    // Planned write range for an NTAG213-style user area: bytes [16, 160).
    const size_t w0 = 16, w1 = 160;

    // No control TLV: NDEF starts the area; null data is accepted.
    const uint8_t plain[] = {0x03, 0x05, 0x00};
    TEST_ASSERT_EQUAL(static_cast<int>(nfcNdef::ControlTlvVerdict::Ok),
                      static_cast<int>(nfcNdef::scanControlTlvs(plain, sizeof(plain), w0, w1)));
    TEST_ASSERT_EQUAL(static_cast<int>(nfcNdef::ControlTlvVerdict::Ok),
                      static_cast<int>(nfcNdef::scanControlTlvs(nullptr, 0, w0, w1)));

    // Static-lock TLV: PageAddr 2, BytesPerPage 2^2=4, offset 2, 16 bits ->
    // bytes [10, 12), outside the write range: benign.
    const uint8_t staticLock[] = {0x01, 0x03, 0x22, 0x10, 0x02};
    TEST_ASSERT_EQUAL(static_cast<int>(nfcNdef::ControlTlvVerdict::Ok),
                      static_cast<int>(nfcNdef::scanControlTlvs(staticLock, sizeof(staticLock), w0, w1)));

    // Standard NTAG dynamic-lock TLV: PageAddr 10, BytesPerPage 2^4=16 ->
    // bytes [160, 162), exactly at the outer write edge: benign.
    const uint8_t dynamicLock[] = {0x01, 0x03, 0xA0, 0x10, 0x04};
    TEST_ASSERT_EQUAL(static_cast<int>(nfcNdef::ControlTlvVerdict::Ok),
                      static_cast<int>(nfcNdef::scanControlTlvs(dynamicLock, sizeof(dynamicLock), w0, w1)));

    // Lock bytes inside the user area: unsafe.
    const uint8_t dangerousLock[] = {0x01, 0x03, 0x44, 0x10, 0x02};
    TEST_ASSERT_EQUAL(static_cast<int>(nfcNdef::ControlTlvVerdict::DangerousLock),
                      static_cast<int>(nfcNdef::scanControlTlvs(dangerousLock, sizeof(dangerousLock), w0, w1)));

    // Same position/offset but BytesPerPage 2^1=2 maps it to byte 12, outside
    // the write range: proves the low-nibble page size is decoded.
    const uint8_t pageSizeMapsOut[] = {0x01, 0x03, 0x44, 0x08, 0x01};
    TEST_ASSERT_EQUAL(static_cast<int>(nfcNdef::ControlTlvVerdict::Ok),
                      static_cast<int>(nfcNdef::scanControlTlvs(pageSizeMapsOut, sizeof(pageSizeMapsOut), w0, w1)));

    // Size 00h means 256 lock bits -> 32 lock bytes, so it overlaps.
    const uint8_t size256[] = {0x01, 0x03, 0x44, 0x00, 0x02};
    TEST_ASSERT_EQUAL(static_cast<int>(nfcNdef::ControlTlvVerdict::DangerousLock),
                      static_cast<int>(nfcNdef::scanControlTlvs(size256, sizeof(size256), w0, w1)));

    // BytesPerPage nibble 0h is RFU: refuse rather than guess.
    const uint8_t rfuPageSize[] = {0x01, 0x03, 0x44, 0x08, 0x00};
    TEST_ASSERT_EQUAL(static_cast<int>(nfcNdef::ControlTlvVerdict::DangerousLock),
                      static_cast<int>(nfcNdef::scanControlTlvs(rfuPageSize, sizeof(rfuPageSize), w0, w1)));

    // Memory Control TLV reserving bytes inside the write range: reject.
    const uint8_t memoryControl[] = {0x02, 0x03, 0x44, 0x10, 0x02};
    TEST_ASSERT_EQUAL(static_cast<int>(nfcNdef::ControlTlvVerdict::ReservedRegion),
                      static_cast<int>(nfcNdef::scanControlTlvs(memoryControl, sizeof(memoryControl), w0, w1)));

    // Memory Control TLV reserving bytes outside the write range: benign.
    const uint8_t memoryControlOut[] = {0x02, 0x03, 0x22, 0x02, 0x02};
    TEST_ASSERT_EQUAL(static_cast<int>(nfcNdef::ControlTlvVerdict::Ok),
                      static_cast<int>(nfcNdef::scanControlTlvs(memoryControlOut, sizeof(memoryControlOut), w0, w1)));

    // Truncated control TLV value: refuse conservatively.
    const uint8_t truncated[] = {0x01, 0x03, 0x28, 0x08};
    TEST_ASSERT_EQUAL(static_cast<int>(nfcNdef::ControlTlvVerdict::DangerousLock),
                      static_cast<int>(nfcNdef::scanControlTlvs(truncated, sizeof(truncated), w0, w1)));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_text_record_known_bytes);
    RUN_TEST(test_text_record_round_trip_utf8);
    RUN_TEST(test_text_record_rejects_empty_and_oversize);
    RUN_TEST(test_text_record_capacity_guard);
    RUN_TEST(test_decode_rejects_non_text_and_malformed);
    RUN_TEST(test_find_ndef_tlv_after_lock_control);
    RUN_TEST(test_find_ndef_tlv_long_length);
    RUN_TEST(test_find_ndef_tlv_stops_at_terminator_and_null_padding);
    RUN_TEST(test_prefix_length);
    RUN_TEST(test_encode_area_preserves_prefix_and_locates_ndef);
    RUN_TEST(test_encode_area_capacity_and_overflow_guard);
    RUN_TEST(test_encode_empty_area);
    RUN_TEST(test_page_helpers);
    RUN_TEST(test_scan_control_tlvs_verdicts);
    return UNITY_END();
}
