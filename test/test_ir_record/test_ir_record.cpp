#include <unity.h>

#include <ArduinoJson.h>

#include "core/ir_record.h"
#include "core/record_buffer.h"

void setUp() {}
void tearDown() {}

// raw[0] is the leading idle gap (dropped); the rest convert with tickUs into
// exact microseconds and no synthetic value leaks in.
void test_make_record_drops_gap_and_converts_microseconds() {
    const uint16_t raw[] = {100, 4500, 2250, 280};
    irRecord::Record record;

    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Ready),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 4, 2, "UNKNOWN", false, 0, 0, record)));
    TEST_ASSERT_EQUAL_UINT16(3, record.count);
    TEST_ASSERT_FALSE(record.decoded);
    TEST_ASSERT_EQUAL_UINT16(0, record.bits);
    TEST_ASSERT_EQUAL_UINT64(0, record.value);
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", record.protocol);
    TEST_ASSERT_EQUAL_UINT32(9000, record.timingsUs[0]);
    TEST_ASSERT_EQUAL_UINT32(4500, record.timingsUs[1]);
    TEST_ASSERT_EQUAL_UINT32(560, record.timingsUs[2]);
}

// Repeat and no-message decodes are ignored and must not touch a previous record.
void test_repeat_and_no_message_are_ignored_without_overwrite() {
    const uint16_t raw[] = {100, 20, 30};
    const uint16_t onlyGap[] = {100};
    irRecord::Record record;
    record.count = 7;
    record.timingsUs[0] = 12345;

    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Ignored),
                      static_cast<int>(irRecord::makeRecord(true, false, raw, 3, 2, "NEC", true, 32, 0xAB, record)));
    TEST_ASSERT_EQUAL_UINT16(7, record.count);
    TEST_ASSERT_EQUAL_UINT32(12345, record.timingsUs[0]);

    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Ignored),
                      static_cast<int>(irRecord::makeRecord(false, false, nullptr, 0, 2, "NEC", false, 0, 0, record)));
    TEST_ASSERT_EQUAL_UINT16(7, record.count);

    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Ignored),
                      static_cast<int>(irRecord::makeRecord(false, false, onlyGap, 1, 2, "NEC", false, 0, 0, record)));
    TEST_ASSERT_EQUAL_UINT16(7, record.count);
}

// 512 timings (513 raw entries incl. the gap) succeed; 513 timings and the
// library overflow flag are rejected without overwriting the old record.
void test_512_boundary_and_overflow_preserve_previous() {
    static uint16_t raw[514];
    for (size_t i = 0; i < 514; ++i) raw[i] = 1;

    irRecord::Record record;
    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Ready),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 513, 2, "UNKNOWN", false, 0, 0, record)));
    TEST_ASSERT_EQUAL_UINT16(512, record.count);
    TEST_ASSERT_EQUAL_UINT32(2, record.timingsUs[511]);

    irRecord::Record kept;
    kept.count = 5;
    kept.timingsUs[0] = 999;
    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::TooLong),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 514, 2, "UNKNOWN", false, 0, 0, kept)));
    TEST_ASSERT_EQUAL_UINT16(5, kept.count);
    TEST_ASSERT_EQUAL_UINT32(999, kept.timingsUs[0]);

    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::TooLong),
                      static_cast<int>(irRecord::makeRecord(false, true, raw, 513, 2, "UNKNOWN", false, 0, 0, kept)));
    TEST_ASSERT_EQUAL_UINT16(5, kept.count);
    TEST_ASSERT_EQUAL_UINT32(999, kept.timingsUs[0]);
}

// Zero tick factor is meaningless and a tick*x conversion that cannot fit
// uint32 is too long; both leave the previous record intact.
void test_units_and_safe_multiply() {
    const uint16_t raw[] = {1, 0xFFFF};
    irRecord::Record record;
    record.count = 9;

    // 65535 * 0x20000 overflows uint32.
    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::TooLong),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 2, 0x20000u, "UNKNOWN", false, 0, 0, record)));
    TEST_ASSERT_EQUAL_UINT16(9, record.count);

    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Invalid),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 2, 0, "UNKNOWN", false, 0, 0, record)));
    TEST_ASSERT_EQUAL_UINT16(9, record.count);
}

// The protocol name is byte-capped at kMaxProtocolBytes and must be present;
// an over-long or missing name is rejected without touching the record.
void test_protocol_length_bound() {
    const uint16_t raw[] = {1, 10};
    irRecord::Record record;
    record.count = 4;

    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Invalid),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 2, 2, nullptr, false, 0, 0, record)));
    TEST_ASSERT_EQUAL_UINT16(4, record.count);

    char overLong[irRecord::kMaxProtocolBytes + 2];
    for (size_t i = 0; i < irRecord::kMaxProtocolBytes + 1; ++i) overLong[i] = 'A';
    overLong[irRecord::kMaxProtocolBytes + 1] = '\0';
    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Invalid),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 2, 2, overLong, false, 0, 0, record)));
    TEST_ASSERT_EQUAL_UINT16(4, record.count);

    char maxName[irRecord::kMaxProtocolBytes + 1];
    for (size_t i = 0; i < irRecord::kMaxProtocolBytes; ++i) maxName[i] = 'A';
    maxName[irRecord::kMaxProtocolBytes] = '\0';
    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Ready),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 2, 2, maxName, false, 0, 0, record)));
    TEST_ASSERT_EQUAL_STRING(maxName, record.protocol);
}

// The status payload keeps the spec §7.3 consumer semantics: real protocol,
// null when there is no numeric decode, a normalized 64-bit hex string when
// there is (so uint64 precision survives JSON), and microsecond timings.
void test_json_preserves_value_and_timings() {
    const uint16_t raw[] = {100, 4500, 2250, 280};

    irRecord::Record unknown;
    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Ready),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 4, 2, "UNKNOWN", false, 0, 0, unknown)));
    JsonDocument unknownDoc;
    TEST_ASSERT_FALSE(deserializeJson(unknownDoc, irRecord::toJson(unknown)));
    TEST_ASSERT_EQUAL_STRING("ir_capture", unknownDoc["kind"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", unknownDoc["protocol"].as<const char *>());
    TEST_ASSERT_TRUE(unknownDoc["value"].isNull());
    JsonArray unknownTimings = unknownDoc["rawTimingsUs"].as<JsonArray>();
    TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(unknownTimings.size()));
    TEST_ASSERT_EQUAL_UINT32(9000, unknownTimings[0].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(4500, unknownTimings[1].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(560, unknownTimings[2].as<uint32_t>());

    irRecord::Record known;
    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Ready),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 3, 2, "SAMSUNG", true, 64,
                                                             0xFEDCBA9876543210ULL, known)));
    JsonDocument knownDoc;
    TEST_ASSERT_FALSE(deserializeJson(knownDoc, irRecord::toJson(known)));
    TEST_ASSERT_EQUAL_STRING("SAMSUNG", knownDoc["protocol"].as<const char *>());
    TEST_ASSERT_TRUE(knownDoc["value"].is<const char *>()); // string, never a lossy JSON number
    TEST_ASSERT_EQUAL_STRING("0xFEDCBA9876543210", knownDoc["value"].as<const char *>());
    JsonArray knownTimings = knownDoc["rawTimingsUs"].as<JsonArray>();
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(knownTimings.size()));
    TEST_ASSERT_EQUAL_UINT32(9000, knownTimings[0].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(4500, knownTimings[1].as<uint32_t>());
}

// The binary form round-trips every typed field losslessly and always fits the
// record buffer.
void test_encode_decode_round_trip_preserves_record() {
    const uint16_t raw[] = {100, 4500, 2250, 280, 560};
    irRecord::Record original;
    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Ready),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 5, 3, "SAMSUNG", true, 32,
                                                             0xFEDCBA9876543210ULL, original)));

    RecordBuffer buffer;
    uint8_t encoded[irRecord::kMaxEncodedBytes];
    size_t length = 0;
    TEST_ASSERT_TRUE(irRecord::encodeToBytes(original, encoded, sizeof(encoded), length));
    TEST_ASSERT_TRUE(buffer.replace(encoded, length)); // atomic retention into the shared buffer
    TEST_ASSERT_FALSE(buffer.empty());
    TEST_ASSERT_TRUE(buffer.size() <= RecordBuffer::capacity);

    irRecord::Record restored;
    TEST_ASSERT_TRUE(irRecord::decode(buffer.data(), buffer.size(), restored));
    TEST_ASSERT_EQUAL_UINT16(original.count, restored.count);
    TEST_ASSERT_TRUE(restored.decoded);
    TEST_ASSERT_EQUAL_UINT16(original.bits, restored.bits);
    TEST_ASSERT_EQUAL_UINT64(original.value, restored.value);
    TEST_ASSERT_EQUAL_STRING(original.protocol, restored.protocol);
    TEST_ASSERT_EQUAL_UINT32_ARRAY(original.timingsUs, restored.timingsUs, original.count);
}

// A maximum-size record (512 timings, 32-byte name) still fits 4096 bytes.
void test_max_record_fits_buffer() {
    static uint16_t raw[513];
    for (size_t i = 0; i < 513; ++i) raw[i] = 0xFFFF;

    char name[irRecord::kMaxProtocolBytes + 1];
    for (size_t i = 0; i < irRecord::kMaxProtocolBytes; ++i) name[i] = 'Z';
    name[irRecord::kMaxProtocolBytes] = '\0';

    irRecord::Record record;
    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Ready),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 513, 1, name, false, 0, 0, record)));

    RecordBuffer buffer;
    uint8_t encoded[irRecord::kMaxEncodedBytes];
    size_t length = 0;
    TEST_ASSERT_TRUE(irRecord::encodeToBytes(record, encoded, sizeof(encoded), length));
    TEST_ASSERT_TRUE(buffer.replace(encoded, length));
    TEST_ASSERT_TRUE(buffer.size() <= RecordBuffer::capacity);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(irRecord::kMaxEncodedBytes),
                             static_cast<uint32_t>(buffer.size()));
}

// An invalid record is refused; encodeToBytes writes nothing and the consumer
// only replaces the buffer on success, so the stored bytes survive intact.
void test_encode_invalid_record_preserves_buffer() {
    const uint8_t kept[] = {0x01, 0x02};

    irRecord::Record record;
    record.count = irRecord::kMaxTimings + 1;

    RecordBuffer buffer;
    TEST_ASSERT_TRUE(buffer.replace(kept, sizeof(kept)));

    uint8_t encoded[irRecord::kMaxEncodedBytes];
    size_t length = 123; // sentinel: a rejected encode must not touch the output
    TEST_ASSERT_FALSE(irRecord::encodeToBytes(record, encoded, sizeof(encoded), length));
    TEST_ASSERT_EQUAL_UINT32(123, static_cast<uint32_t>(length));
    TEST_ASSERT_EQUAL_UINT32(sizeof(kept), static_cast<uint32_t>(buffer.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kept, buffer.data(), sizeof(kept));
}

// Corrupt/truncated bytes are rejected; the destination record keeps its
// previous contents until a fully valid encoding is decoded.
void test_decode_rejects_malformed_without_overwrite() {
    const uint16_t raw[] = {1, 10, 20};
    irRecord::Record source;
    TEST_ASSERT_EQUAL(static_cast<int>(irRecord::Result::Ready),
                      static_cast<int>(irRecord::makeRecord(false, false, raw, 3, 2, "NEC", true, 32, 0x1234, source)));

    uint8_t bytes[irRecord::kMaxEncodedBytes];
    size_t validLength = 0;
    TEST_ASSERT_TRUE(irRecord::encodeToBytes(source, bytes, sizeof(bytes), validLength));

    irRecord::Record record;
    record.count = 3;
    record.timingsUs[0] = 777;

    TEST_ASSERT_FALSE(irRecord::decode(bytes, validLength - 1, record)); // truncated
    TEST_ASSERT_FALSE(irRecord::decode(nullptr, 0, record));             // no bytes
    bytes[0] = 'X';
    TEST_ASSERT_FALSE(irRecord::decode(bytes, validLength, record)); // bad magic
    bytes[0] = 'I';
    bytes[16] = static_cast<uint8_t>(irRecord::kMaxProtocolBytes + 1);
    TEST_ASSERT_FALSE(irRecord::decode(bytes, validLength, record)); // protocol over cap
    bytes[16] = 3;
    TEST_ASSERT_FALSE(irRecord::decode(bytes, validLength + 1, record)); // length mismatch

    TEST_ASSERT_EQUAL_UINT16(3, record.count);
    TEST_ASSERT_EQUAL_UINT32(777, record.timingsUs[0]);

    TEST_ASSERT_TRUE(irRecord::decode(bytes, validLength, record)); // valid again
    TEST_ASSERT_EQUAL_STRING("NEC", record.protocol);
    TEST_ASSERT_TRUE(record.decoded);
    TEST_ASSERT_EQUAL_UINT64(0x1234, record.value);
    TEST_ASSERT_EQUAL_UINT32(20, record.timingsUs[0]);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_make_record_drops_gap_and_converts_microseconds);
    RUN_TEST(test_repeat_and_no_message_are_ignored_without_overwrite);
    RUN_TEST(test_512_boundary_and_overflow_preserve_previous);
    RUN_TEST(test_units_and_safe_multiply);
    RUN_TEST(test_protocol_length_bound);
    RUN_TEST(test_json_preserves_value_and_timings);
    RUN_TEST(test_encode_decode_round_trip_preserves_record);
    RUN_TEST(test_max_record_fits_buffer);
    RUN_TEST(test_encode_invalid_record_preserves_buffer);
    RUN_TEST(test_decode_rejects_malformed_without_overwrite);
    return UNITY_END();
}
