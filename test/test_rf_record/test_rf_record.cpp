#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>

#include "core/rf_record.h"

void setUp() {}
void tearDown() {}

using rfRecord::Record;
using rfRecord::Result;

namespace {

// A small, valid 433 MHz burst: 8 durations, all <= cap, total 4800 us.
const uint16_t kPulses[] = {400, 400, 800, 800, 400, 1200, 400, 400};
constexpr size_t kPulseCount = sizeof(kPulses) / sizeof(kPulses[0]);

Record validRecord() {
    Record record;
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Ready),
                      static_cast<int>(rfRecord::makeRecord(433920000u, true, kPulses, kPulseCount, record)));
    return record;
}

} // namespace

// ---------------------------------------------------------------------------
// makeRecord validation
// ---------------------------------------------------------------------------

void test_make_record_empty_rejected() {
    Record record;
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Empty),
                      static_cast<int>(rfRecord::makeRecord(433920000u, true, kPulses, 0, record)));
}

void test_make_record_null_and_invalid_fields() {
    Record record;
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Invalid),
                      static_cast<int>(rfRecord::makeRecord(433920000u, true, nullptr, 4, record)));

    // Frequency outside every calibrated band.
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Invalid),
                      static_cast<int>(rfRecord::makeRecord(370000000u, true, kPulses, kPulseCount, record)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Invalid),
                      static_cast<int>(rfRecord::makeRecord(1000000u, true, kPulses, kPulseCount, record)));

    const uint16_t zeroPulse[] = {400, 0, 400};
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Invalid),
                      static_cast<int>(rfRecord::makeRecord(433920000u, true, zeroPulse, 3, record)));
}

void test_make_record_too_long_count_and_total() {
    Record record;
    const uint16_t one = 400;
    TEST_ASSERT_EQUAL(static_cast<int>(Result::TooLong),
                      static_cast<int>(rfRecord::makeRecord(433920000u, true, &one, rfRecord::kMaxDurations + 1, record)));

    const uint16_t bigPulses[] = {60000, 60000}; // 120000 > kMaxTotalDurationUs
    TEST_ASSERT_EQUAL(static_cast<int>(Result::TooLong),
                      static_cast<int>(rfRecord::makeRecord(433920000u, true, bigPulses, 2, record)));
}

void test_make_record_ready_copies_exactly() {
    Record record;
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Ready),
                      static_cast<int>(rfRecord::makeRecord(433920000u, false, kPulses, kPulseCount, record)));
    TEST_ASSERT_EQUAL_UINT32(433920000u, record.freqHz);
    TEST_ASSERT_EQUAL_UINT16(kPulseCount, record.count);
    TEST_ASSERT_FALSE(record.firstLevelHigh);
    for (size_t i = 0; i < kPulseCount; ++i) TEST_ASSERT_EQUAL_UINT16(kPulses[i], record.durationsUs[i]);
    TEST_ASSERT_EQUAL_UINT32(4800u, rfRecord::totalDurationUs(record));
}

// ---------------------------------------------------------------------------
// encode / decode
// ---------------------------------------------------------------------------

void test_encode_decode_round_trip_is_lossless() {
    const Record record = validRecord();

    uint8_t bytes[rfRecord::kMaxEncodedBytes];
    size_t length = 0;
    TEST_ASSERT_TRUE(rfRecord::encodeToBytes(record, bytes, sizeof(bytes), length));
    TEST_ASSERT_EQUAL_UINT32(rfRecord::kEncodedHeaderBytes + 2 * kPulseCount, length);
    TEST_ASSERT_EQUAL_CHAR('R', bytes[0]);
    TEST_ASSERT_EQUAL_CHAR('F', bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(1, bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0x01, bytes[3]);

    Record decoded;
    TEST_ASSERT_TRUE(rfRecord::decode(bytes, length, decoded));
    TEST_ASSERT_EQUAL_UINT32(record.freqHz, decoded.freqHz);
    TEST_ASSERT_EQUAL_UINT16(record.count, decoded.count);
    TEST_ASSERT_EQUAL(record.firstLevelHigh, decoded.firstLevelHigh);
    for (uint16_t i = 0; i < record.count; ++i) {
        TEST_ASSERT_EQUAL_UINT16(record.durationsUs[i], decoded.durationsUs[i]);
    }
}

void test_encode_rejects_undersized_buffer_and_bad_frequency() {
    const Record record = validRecord();
    uint8_t tooSmall[rfRecord::kEncodedHeaderBytes + 2 * kPulseCount - 1];
    size_t length = 123;
    TEST_ASSERT_FALSE(rfRecord::encodeToBytes(record, tooSmall, sizeof(tooSmall), length));
    TEST_ASSERT_EQUAL_UINT32(123, length); // untouched on failure

    Record bad = record;
    bad.freqHz = 1000000u;
    uint8_t bytes[rfRecord::kMaxEncodedBytes];
    TEST_ASSERT_FALSE(rfRecord::encodeToBytes(bad, bytes, sizeof(bytes), length));
}

void test_decode_rejects_corruption_and_truncation() {
    const Record record = validRecord();
    uint8_t bytes[rfRecord::kMaxEncodedBytes];
    size_t length = 0;
    TEST_ASSERT_TRUE(rfRecord::encodeToBytes(record, bytes, sizeof(bytes), length));

    Record out;
    uint8_t copy[rfRecord::kMaxEncodedBytes];

    std::memcpy(copy, bytes, length);
    TEST_ASSERT_FALSE(rfRecord::decode(copy, length - 1, out)); // truncated

    std::memcpy(copy, bytes, length);
    copy[0] = 'X';
    TEST_ASSERT_FALSE(rfRecord::decode(copy, length, out)); // bad magic

    std::memcpy(copy, bytes, length);
    copy[2] = 2;
    TEST_ASSERT_FALSE(rfRecord::decode(copy, length, out)); // bad version

    std::memcpy(copy, bytes, length);
    copy[3] = 0x80;
    TEST_ASSERT_FALSE(rfRecord::decode(copy, length, out)); // unknown flag bit

    std::memcpy(copy, bytes, length);
    copy[4] = 0x00; // freq low byte -> 1 MHz, out of band
    copy[5] = 0x00;
    copy[6] = 0x00;
    copy[7] = 0x00;
    TEST_ASSERT_FALSE(rfRecord::decode(copy, length, out));

    std::memcpy(copy, bytes, length);
    copy[10] = 0;
    copy[11] = 0; // first duration zero
    TEST_ASSERT_FALSE(rfRecord::decode(copy, length, out));

    std::memcpy(copy, bytes, length);
    copy[8] = 0;
    copy[9] = 0; // count zero
    TEST_ASSERT_FALSE(rfRecord::decode(copy, length, out));

    // Wrong declared length for the count.
    std::memcpy(copy, bytes, length);
    copy[8] = static_cast<uint8_t>(kPulseCount + 1);
    TEST_ASSERT_FALSE(rfRecord::decode(copy, length, out));
}

void test_decode_rejects_over_cap_count() {
    // Build a syntactically valid header with count = kMaxDurations + 1 and a
    // matching body length; decode must reject on the cap before copying.
    const size_t count = rfRecord::kMaxDurations + 1;
    const size_t total = rfRecord::kEncodedHeaderBytes + 2 * count;
    static uint8_t bytes[rfRecord::kEncodedHeaderBytes + 2 * (rfRecord::kMaxDurations + 1)];
    TEST_ASSERT_TRUE(total <= sizeof(bytes));
    bytes[0] = 'R';
    bytes[1] = 'F';
    bytes[2] = 1;
    bytes[3] = 0x01;
    bytes[4] = 0xC0;
    bytes[5] = 0xDC;
    bytes[6] = 0xCA;
    bytes[7] = 0x19; // 433920000
    bytes[8] = static_cast<uint8_t>(count & 0xFF);
    bytes[9] = static_cast<uint8_t>(count >> 8);
    for (size_t i = 0; i < count; ++i) {
        bytes[rfRecord::kEncodedHeaderBytes + 2 * i] = 0x10;
        bytes[rfRecord::kEncodedHeaderBytes + 2 * i + 1] = 0x00;
    }
    Record out;
    TEST_ASSERT_FALSE(rfRecord::decode(bytes, total, out));
}

// ---------------------------------------------------------------------------
// JSON summary
// ---------------------------------------------------------------------------

void test_json_summary_is_bounded_and_complete_on_totals() {
    Record record;
    uint16_t many[200];
    for (size_t i = 0; i < 200; ++i) many[i] = 100;
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Ready),
                      static_cast<int>(rfRecord::makeRecord(433920000u, true, many, 200, record)));

    const std::string json = rfRecord::toJson(record);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, json));
    TEST_ASSERT_EQUAL_STRING("rf_record", doc["kind"]);
    TEST_ASSERT_EQUAL_UINT32(433920000u, doc["freqHz"]);
    TEST_ASSERT_EQUAL_UINT32(200, doc["pulseCount"]);
    TEST_ASSERT_EQUAL_UINT32(20000, doc["totalUs"]);
    TEST_ASSERT_TRUE(doc["firstLevelHigh"]);
    TEST_ASSERT_EQUAL_UINT32(rfRecord::kMaxJsonDurations, doc["durationUs"].size());
    TEST_ASSERT_EQUAL_UINT32(100, doc["durationUs"][0]);
}

// ---------------------------------------------------------------------------
// Frequency bands
// ---------------------------------------------------------------------------

void test_frequency_bands_are_inclusive_at_edges() {
    TEST_ASSERT_TRUE(rfRecord::supportedFrequencyHz(300000000u));
    TEST_ASSERT_TRUE(rfRecord::supportedFrequencyHz(348000000u));
    TEST_ASSERT_FALSE(rfRecord::supportedFrequencyHz(348000001u));
    TEST_ASSERT_FALSE(rfRecord::supportedFrequencyHz(386000000u)); // no PA table
    TEST_ASSERT_TRUE(rfRecord::supportedFrequencyHz(387000000u));
    TEST_ASSERT_TRUE(rfRecord::supportedFrequencyHz(464000000u));
    TEST_ASSERT_FALSE(rfRecord::supportedFrequencyHz(464000001u));
    TEST_ASSERT_TRUE(rfRecord::supportedFrequencyHz(779000000u));
    TEST_ASSERT_TRUE(rfRecord::supportedFrequencyHz(928000000u));
    TEST_ASSERT_FALSE(rfRecord::supportedFrequencyHz(928000001u));
    TEST_ASSERT_FALSE(rfRecord::supportedFrequencyHz(0u));
}

void test_frequency_mhz_rounds_to_nearest_hz() {
    TEST_ASSERT_TRUE(rfRecord::supportedFrequencyMhz(433.92));
    TEST_ASSERT_TRUE(rfRecord::supportedFrequencyMhz(300.0));
    TEST_ASSERT_TRUE(rfRecord::supportedFrequencyMhz(928.0));
    TEST_ASSERT_FALSE(rfRecord::supportedFrequencyMhz(349.5));
    TEST_ASSERT_FALSE(rfRecord::supportedFrequencyMhz(300.0 - 0.0006));
    TEST_ASSERT_FALSE(rfRecord::supportedFrequencyMhz(1000.0));
}

void test_supported_band_requires_single_span() {
    TEST_ASSERT_TRUE(rfRecord::supportedBandMhz(433.0, 434.0));
    TEST_ASSERT_TRUE(rfRecord::supportedBandMhz(300.0, 348.0));
    TEST_ASSERT_FALSE(rfRecord::supportedBandMhz(433.0, 465.0)); // crosses bands
    TEST_ASSERT_FALSE(rfRecord::supportedBandMhz(300.0, 400.0)); // crosses gap
    TEST_ASSERT_FALSE(rfRecord::supportedBandMhz(434.0, 433.0)); // inverted
    TEST_ASSERT_FALSE(rfRecord::supportedBandMhz(433.0, 433.0)); // zero span
}

// ---------------------------------------------------------------------------
// Sweep plan
// ---------------------------------------------------------------------------

void test_sweep_point_count_and_positions() {
    TEST_ASSERT_EQUAL_UINT32(6, rfRecord::sweepPointCount(433.0, 434.0, 200, 64));
    TEST_ASSERT_EQUAL_UINT32(2, rfRecord::sweepPointCount(433.0, 433.001, 1, 64));
    TEST_ASSERT_EQUAL_UINT32(1, rfRecord::sweepPointCount(433.0, 433.0 + 0.0005, 1, 64));

    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::sweepPointCount(433.0, 434.0, 200, 5)); // exceeds cap
    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::sweepPointCount(433.0, 434.0, 0, 64));   // zero step
    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::sweepPointCount(433.0, 928.0, 200, 64)); // multi-band
    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::sweepPointCount(434.0, 433.0, 200, 64)); // inverted

    TEST_ASSERT_TRUE(std::abs(rfRecord::sweepPointMhz(433.0, 200, 3) - 433.6) < 1e-9);
}

// ---------------------------------------------------------------------------
// Hex payload
// ---------------------------------------------------------------------------

void test_hex_decode_strict() {
    uint8_t out[32];
    size_t length = 99;

    TEST_ASSERT_TRUE(rfRecord::decodeHex("00ff10", 6, out, sizeof(out), length));
    TEST_ASSERT_EQUAL_UINT32(3, length);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x10, out[2]);

    TEST_ASSERT_TRUE(rfRecord::decodeHex("AaBb", 4, out, sizeof(out), length));
    TEST_ASSERT_EQUAL_UINT8(0xAA, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, out[1]);

    TEST_ASSERT_FALSE(rfRecord::decodeHex("", 0, out, sizeof(out), length)); // empty
    TEST_ASSERT_FALSE(rfRecord::decodeHex("abc", 3, out, sizeof(out), length)); // odd
    TEST_ASSERT_FALSE(rfRecord::decodeHex("gg", 2, out, sizeof(out), length));  // non-hex
    TEST_ASSERT_FALSE(rfRecord::decodeHex("00 1", 4, out, sizeof(out), length)); // space

    uint8_t small[2];
    length = 99; // successful decodes above updated length; re-arm the sentinel
    TEST_ASSERT_FALSE(rfRecord::decodeHex("000102", 6, small, sizeof(small), length)); // over capacity
    TEST_ASSERT_EQUAL_UINT32(99, length); // untouched on failure
}

// ---------------------------------------------------------------------------
// RSSI conversion and bounded sweep/replay/custom summaries
// ---------------------------------------------------------------------------

void test_rssi_conversion_matches_driver() {
    // Vendor formula: signed byte / 2 - 74 (raw >= 128 reads as raw - 256).
    TEST_ASSERT_EQUAL_INT(-74, rfRecord::rssiToDbm(0x00));
    TEST_ASSERT_EQUAL_INT(-74, rfRecord::rssiToDbm(0xFF)); // signed -1 -> 0 - 74
    TEST_ASSERT_EQUAL_INT(-138, rfRecord::rssiToDbm(0x80)); // signed -128 -> -64 - 74
    TEST_ASSERT_EQUAL_INT(-42, rfRecord::rssiToDbm(0x40));  // 64 -> 32 - 74
}

void test_sweep_json_reports_peak_and_bounded_body() {
    int dbm[rfRecord::kMaxScanPoints];
    for (size_t i = 0; i < rfRecord::kMaxScanPoints; ++i) dbm[i] = -100;
    dbm[5] = -30; // strongest point

    const std::string json = rfRecord::sweepToJson("rf_scan", 433000000u, 25, dbm, rfRecord::kMaxScanPoints);
    TEST_ASSERT_TRUE(json.size() > 0);
    TEST_ASSERT_TRUE(json.size() < 1024);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, json));
    TEST_ASSERT_EQUAL_STRING("rf_scan", doc["kind"]);
    TEST_ASSERT_EQUAL_UINT32(433000000u, doc["startHz"]);
    TEST_ASSERT_EQUAL_UINT32(25, doc["stepKhz"]);
    TEST_ASSERT_EQUAL_UINT32(rfRecord::kMaxScanPoints, doc["count"]);
    TEST_ASSERT_EQUAL_UINT32(rfRecord::kMaxScanPoints, doc["dbm"].size());
    TEST_ASSERT_EQUAL_INT(-30, doc["peakDbm"]);
    TEST_ASSERT_EQUAL_UINT32(433000000u + 25u * 1000u * 5u, doc["peakHz"]);
}

void test_sweep_json_rejects_bad_input() {
    int one[1] = {-50};
    TEST_ASSERT_EQUAL_STRING("", rfRecord::sweepToJson("rf_scan", 433000000u, 25, nullptr, 1).c_str());
    TEST_ASSERT_EQUAL_STRING("", rfRecord::sweepToJson("rf_scan", 433000000u, 25, one, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("", rfRecord::sweepToJson(nullptr, 433000000u, 25, one, 1).c_str());
    int over[rfRecord::kMaxScanPoints + 1] = {};
    TEST_ASSERT_EQUAL_STRING("",
                             rfRecord::sweepToJson("rf_spectrum", 433000000u, 25, over, rfRecord::kMaxScanPoints + 1).c_str());
}

void test_replay_and_custom_summaries() {
    const Record record = validRecord();

    JsonDocument replay;
    TEST_ASSERT_FALSE(deserializeJson(replay, rfRecord::replayToJson(record, 3)));
    TEST_ASSERT_EQUAL_STRING("rf_replay", replay["kind"]);
    TEST_ASSERT_EQUAL_UINT32(433920000u, replay["freqHz"]);
    TEST_ASSERT_EQUAL_UINT16(kPulseCount, replay["pulseCount"]);
    TEST_ASSERT_EQUAL_UINT32(4800u, replay["totalUs"]);
    TEST_ASSERT_TRUE(replay["firstLevelHigh"]);
    TEST_ASSERT_EQUAL_UINT32(3, replay["repeat"]);

    JsonDocument custom;
    TEST_ASSERT_FALSE(deserializeJson(custom, rfRecord::customTxToJson(433920000u, 4, 1000, 2)));
    TEST_ASSERT_EQUAL_STRING("rf_custom_tx", custom["kind"]);
    TEST_ASSERT_EQUAL_UINT32(433920000u, custom["freqHz"]);
    TEST_ASSERT_EQUAL_UINT32(4, custom["bytes"]);
    TEST_ASSERT_EQUAL_UINT32(1000, custom["bitPeriodUs"]);
    TEST_ASSERT_EQUAL_UINT32(2, custom["repeat"]);
}

// ---------------------------------------------------------------------------
// Portable RMT transmit conversion
// ---------------------------------------------------------------------------

void test_replay_items_preserve_alternating_levels() {
    Record record;
    const uint16_t pulses[] = {400, 800, 400, 1200};
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Ready),
                      static_cast<int>(rfRecord::makeRecord(433920000u, true, pulses, 4, record)));

    rfRecord::TxItem items[8] = {};
    const size_t count = rfRecord::replayItems(record, items, 8);
    TEST_ASSERT_EQUAL_UINT32(2, count);
    TEST_ASSERT_EQUAL_UINT16(400, items[0].duration0);
    TEST_ASSERT_EQUAL_UINT8(1, items[0].level0);
    TEST_ASSERT_EQUAL_UINT16(800, items[0].duration1);
    TEST_ASSERT_EQUAL_UINT8(0, items[0].level1);
    TEST_ASSERT_EQUAL_UINT16(400, items[1].duration0);
    TEST_ASSERT_EQUAL_UINT8(1, items[1].level0);
    TEST_ASSERT_EQUAL_UINT16(1200, items[1].duration1);
    TEST_ASSERT_EQUAL_UINT8(0, items[1].level1);

    // An odd count leaves the last segment pending; it is packed alone and the
    // unused half is zeroed so the hardware stops after that duration.
    const uint16_t odd[] = {300, 500, 700};
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Ready),
                      static_cast<int>(rfRecord::makeRecord(433920000u, false, odd, 3, record)));
    TEST_ASSERT_EQUAL_UINT32(2, rfRecord::replayItems(record, items, 8));
    TEST_ASSERT_EQUAL_UINT16(300, items[0].duration0);
    TEST_ASSERT_EQUAL_UINT8(0, items[0].level0); // firstLevelHigh == false
    TEST_ASSERT_EQUAL_UINT16(500, items[0].duration1);
    TEST_ASSERT_EQUAL_UINT8(1, items[0].level1);
    TEST_ASSERT_EQUAL_UINT16(700, items[1].duration0);
    TEST_ASSERT_EQUAL_UINT8(0, items[1].level0);
    TEST_ASSERT_EQUAL_UINT16(0, items[1].duration1);
    TEST_ASSERT_EQUAL_UINT8(0, items[1].level1);
}

void test_replay_items_split_durations_at_15_bits() {
    Record record;
    record.freqHz = 433920000u;
    record.firstLevelHigh = true;

    // Exactly at the 15-bit field limit: one chunk.
    record.count = 1;
    record.durationsUs[0] = 32767;
    rfRecord::TxItem items[8] = {};
    TEST_ASSERT_EQUAL_UINT32(1, rfRecord::replayItems(record, items, 8));
    TEST_ASSERT_EQUAL_UINT16(32767, items[0].duration0);
    TEST_ASSERT_EQUAL_UINT16(0, items[0].duration1);

    // One over: two chunks, both keeping the segment level.
    record.durationsUs[0] = 32768;
    TEST_ASSERT_EQUAL_UINT32(1, rfRecord::replayItems(record, items, 8));
    TEST_ASSERT_EQUAL_UINT16(32767, items[0].duration0);
    TEST_ASSERT_EQUAL_UINT8(1, items[0].level0);
    TEST_ASSERT_EQUAL_UINT16(1, items[0].duration1);
    TEST_ASSERT_EQUAL_UINT8(1, items[0].level1);

    // kMaxDurationUs needs three chunks; the sum is preserved exactly.
    record.durationsUs[0] = static_cast<uint16_t>(rfRecord::kMaxDurationUs);
    TEST_ASSERT_EQUAL_UINT32(2, rfRecord::replayItems(record, items, 8));
    uint32_t total = 0;
    total += items[0].duration0 + items[0].duration1 + items[1].duration0 + items[1].duration1;
    TEST_ASSERT_EQUAL_UINT32(rfRecord::kMaxDurationUs, total);
    TEST_ASSERT_EQUAL_UINT16(1, items[1].duration0);
    TEST_ASSERT_EQUAL_UINT16(0, items[1].duration1);
}

void test_tx_items_fail_closed_and_fit_budget() {
    rfRecord::TxItem items[rfRecord::kMaxTxItems] = {};
    Record record;
    record.freqHz = 433920000u;
    record.firstLevelHigh = true;

    // Empty / null / zero-duration inputs are rejected, never truncated.
    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::replayItems(record, items, rfRecord::kMaxTxItems));
    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::replayItems(record, nullptr, 8));
    record.count = 1;
    record.durationsUs[0] = 0;
    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::replayItems(record, items, rfRecord::kMaxTxItems));

    // A stream that does not fit the caller's capacity fails closed.
    record.durationsUs[0] = 32767;
    record.count = 3;
    record.durationsUs[1] = 32767;
    record.durationsUs[2] = 32767;
    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::replayItems(record, items, 1));

    // A full-capacity record of 1 us pulses fits the advertised item budget.
    Record big;
    big.freqHz = 433920000u;
    big.firstLevelHigh = true;
    big.count = static_cast<uint16_t>(rfRecord::kMaxDurations);
    for (size_t i = 0; i < rfRecord::kMaxDurations; ++i) big.durationsUs[i] = 1;
    TEST_ASSERT_EQUAL_UINT32(rfRecord::kMaxDurations / 2, rfRecord::replayItems(big, items, rfRecord::kMaxTxItems));
}

void test_custom_items_bits_msb_first() {
    const uint8_t payload = 0xA5; // 1010 0101
    rfRecord::TxItem items[8] = {};
    const size_t count = rfRecord::customItems(&payload, 1, 1000, items, 8);
    TEST_ASSERT_EQUAL_UINT32(4, count);
    for (size_t i = 0; i < 8; ++i) {
        const uint32_t duration = (i % 2 == 0) ? items[i / 2].duration0 : items[i / 2].duration1;
        const uint8_t level = (i % 2 == 0) ? items[i / 2].level0 : items[i / 2].level1;
        TEST_ASSERT_EQUAL_UINT32(1000, duration);
        TEST_ASSERT_EQUAL_UINT8((payload >> (7 - i)) & 0x01, level);
    }

    // A bit period above the 15-bit field splits without dropping a bit.
    rfRecord::TxItem split[16] = {};
    TEST_ASSERT_EQUAL_UINT32(8, rfRecord::customItems(&payload, 1, 40000, split, 16));
    TEST_ASSERT_EQUAL_UINT16(32767, split[0].duration0);
    TEST_ASSERT_EQUAL_UINT16(7233, split[0].duration1);

    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::customItems(nullptr, 1, 1000, items, 8));
    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::customItems(&payload, 0, 1000, items, 8));
    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::customItems(&payload, 1, 0, items, 8));
    TEST_ASSERT_EQUAL_UINT32(0, rfRecord::customItems(&payload, 1, 1000, items, 1));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_make_record_empty_rejected);
    RUN_TEST(test_make_record_null_and_invalid_fields);
    RUN_TEST(test_make_record_too_long_count_and_total);
    RUN_TEST(test_make_record_ready_copies_exactly);
    RUN_TEST(test_encode_decode_round_trip_is_lossless);
    RUN_TEST(test_encode_rejects_undersized_buffer_and_bad_frequency);
    RUN_TEST(test_decode_rejects_corruption_and_truncation);
    RUN_TEST(test_decode_rejects_over_cap_count);
    RUN_TEST(test_json_summary_is_bounded_and_complete_on_totals);
    RUN_TEST(test_frequency_bands_are_inclusive_at_edges);
    RUN_TEST(test_frequency_mhz_rounds_to_nearest_hz);
    RUN_TEST(test_supported_band_requires_single_span);
    RUN_TEST(test_sweep_point_count_and_positions);
    RUN_TEST(test_hex_decode_strict);
    RUN_TEST(test_rssi_conversion_matches_driver);
    RUN_TEST(test_sweep_json_reports_peak_and_bounded_body);
    RUN_TEST(test_sweep_json_rejects_bad_input);
    RUN_TEST(test_replay_and_custom_summaries);
    RUN_TEST(test_replay_items_preserve_alternating_levels);
    RUN_TEST(test_replay_items_split_durations_at_15_bits);
    RUN_TEST(test_tx_items_fail_closed_and_fit_budget);
    RUN_TEST(test_custom_items_bits_msb_first);
    return UNITY_END();
}
