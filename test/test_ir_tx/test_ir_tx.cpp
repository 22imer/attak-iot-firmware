// Behavioral tests for the portable IR transmit planner (T40 replay, T41
// tvbgone, T42 custom_tx): parameter/raw validation, protocol bit-size rules
// and exact synthesized bursts. Assertions check observable structure and
// invariants, not incidental byte snapshots.
#include <unity.h>

#include <string>

#include "core/ir_record.h"
#include "core/ir_tx.h"

void setUp() {}
void tearDown() {}

namespace {

uint32_t burstTotal(const irTx::Burst &burst) {
    uint32_t total = 0;
    for (uint16_t i = 0; i < burst.count; ++i) total += burst.timings[i];
    return total;
}

// No entry may be zero: RMT treats a zero duration as an end marker, and a zero
// timing would also make the burst collapse.
bool burstHasNoZeroDurations(const irTx::Burst &burst) {
    if (burst.count == 0) return false;
    for (uint16_t i = 0; i < burst.count; ++i) {
        if (burst.timings[i] == 0) return false;
    }
    return true;
}

} // namespace

// --- protocol identity / carrier ------------------------------------------

void test_protocol_parse_and_names() {
    irTx::Protocol protocol = irTx::Protocol::Count;
    TEST_ASSERT_TRUE(irTx::parseProtocol("nec", protocol));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Protocol::Nec), static_cast<int>(protocol));
    TEST_ASSERT_TRUE(irTx::parseProtocol("Panasonic", protocol));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Protocol::Panasonic), static_cast<int>(protocol));
    TEST_ASSERT_FALSE(irTx::parseProtocol("RAW", protocol)); // raw is a transport, not a codec
    TEST_ASSERT_FALSE(irTx::parseProtocol("RC6", protocol));
    TEST_ASSERT_EQUAL_STRING("NEC", irTx::protocolName(irTx::Protocol::Nec));
    TEST_ASSERT_EQUAL_STRING("RC5", irTx::protocolName(irTx::Protocol::Rc5));
}

void test_carrier_and_bit_sizes_are_exact() {
    TEST_ASSERT_EQUAL_UINT32(38000, irTx::protocolCarrier(irTx::Protocol::Nec).frequencyHz);
    TEST_ASSERT_EQUAL_UINT32(40000, irTx::protocolCarrier(irTx::Protocol::Sony).frequencyHz);
    TEST_ASSERT_EQUAL_UINT32(36000, irTx::protocolCarrier(irTx::Protocol::Rc5).frequencyHz);
    TEST_ASSERT_EQUAL_UINT8(25, irTx::protocolCarrier(irTx::Protocol::Rc5).dutyPercent);
    TEST_ASSERT_EQUAL_UINT32(36700, irTx::protocolCarrier(irTx::Protocol::Panasonic).frequencyHz);
    TEST_ASSERT_EQUAL_UINT8(50, irTx::protocolCarrier(irTx::Protocol::Panasonic).dutyPercent);

    TEST_ASSERT_TRUE(irTx::protocolSupportsBits(irTx::Protocol::Nec, 32));
    TEST_ASSERT_FALSE(irTx::protocolSupportsBits(irTx::Protocol::Nec, 16));
    TEST_ASSERT_TRUE(irTx::protocolSupportsBits(irTx::Protocol::Sony, 12));
    TEST_ASSERT_TRUE(irTx::protocolSupportsBits(irTx::Protocol::Sony, 15));
    TEST_ASSERT_TRUE(irTx::protocolSupportsBits(irTx::Protocol::Sony, 20));
    TEST_ASSERT_FALSE(irTx::protocolSupportsBits(irTx::Protocol::Sony, 32));
    TEST_ASSERT_TRUE(irTx::protocolSupportsBits(irTx::Protocol::Rc5, 12));
    TEST_ASSERT_FALSE(irTx::protocolSupportsBits(irTx::Protocol::Rc5, 13)); // RC5X not offered
    TEST_ASSERT_TRUE(irTx::protocolSupportsBits(irTx::Protocol::Samsung, 32));
    TEST_ASSERT_FALSE(irTx::protocolSupportsBits(irTx::Protocol::Samsung, 48));
    TEST_ASSERT_TRUE(irTx::protocolSupportsBits(irTx::Protocol::Panasonic, 48));
    TEST_ASSERT_FALSE(irTx::protocolSupportsBits(irTx::Protocol::Panasonic, 32));
}

// --- hex code parsing ------------------------------------------------------

void test_parse_hex_code_boundaries() {
    uint64_t value = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None), static_cast<int>(irTx::parseHexCode("20DF10EF", value)));
    TEST_ASSERT_EQUAL_UINT64(0x20DF10EFULL, value);
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None), static_cast<int>(irTx::parseHexCode("0x20df10ef", value)));
    TEST_ASSERT_EQUAL_UINT64(0x20DF10EFULL, value);

    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::parseHexCode("FFFFFFFFFFFFFFFF", value)));
    TEST_ASSERT_EQUAL_UINT64(0xFFFFFFFFFFFFFFFFULL, value);

    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::InvalidCode),
                      static_cast<int>(irTx::parseHexCode("FFFFFFFFFFFFFFFFF", value))); // 17 digits
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::InvalidCode), static_cast<int>(irTx::parseHexCode("0x", value)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::InvalidCode), static_cast<int>(irTx::parseHexCode("", value)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::InvalidCode), static_cast<int>(irTx::parseHexCode("12G4", value)));
}

// --- raw waveform parsing --------------------------------------------------

void test_parse_raw_waveform_accepts_and_normalizes() {
    irTx::Waveform waveform;
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::parseRawWaveform("9000,4500,560,560", waveform)));
    TEST_ASSERT_EQUAL_UINT16(4, waveform.count);
    TEST_ASSERT_EQUAL_UINT32(9000, waveform.timings[0]);
    TEST_ASSERT_EQUAL_UINT32(14620, waveform.totalUs);

    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::parseRawWaveform(" 9000 , 4500 ,560", waveform)));
    TEST_ASSERT_EQUAL_UINT16(3, waveform.count);

    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::parseRawWaveform("1", waveform)));
    TEST_ASSERT_EQUAL_UINT16(1, waveform.count);
}

void test_parse_raw_waveform_rejects_malformed_without_touching_out() {
    irTx::Waveform waveform;
    waveform.count = 7;
    waveform.timings[0] = 123;
    waveform.totalUs = 456;

    const char *malformed[] = {
        "",            // Empty (covered separately below)
        ",9000",       // leading comma
        "9000,",       // trailing comma
        "9000,,4500",  // empty field
        "0,9000",      // zero timing
        "9000,45x",    // non-digit
        "9000;4500",   // wrong separator
    };
    for (const char *text : malformed) {
        const irTx::Error error = irTx::parseRawWaveform(text, waveform);
        TEST_ASSERT_TRUE(error != irTx::Error::None);
        TEST_ASSERT_EQUAL_UINT16(7, waveform.count);
        TEST_ASSERT_EQUAL_UINT32(123, waveform.timings[0]);
    }
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::Empty), static_cast<int>(irTx::parseRawWaveform("", waveform)));
}

void test_parse_raw_waveform_bounds() {
    irTx::Waveform waveform;

    // 65536 is the first value the 16-bit RMT timing cannot carry.
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::ValueOutOfRange),
                      static_cast<int>(irTx::parseRawWaveform("65536", waveform)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::parseRawWaveform("65535", waveform)));
    TEST_ASSERT_EQUAL_UINT32(65535, waveform.timings[0]);

    // Duration cap: exactly kMaxRawTxDurationUs (500000 us) is accepted and one
    // microsecond more is too long. Eight 62500 us runs land on the boundary.
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::parseRawWaveform("62500,62500,62500,62500,"
                                                              "62500,62500,62500,62500",
                                                              waveform)));
    TEST_ASSERT_EQUAL_UINT16(8, waveform.count);
    TEST_ASSERT_EQUAL_UINT32(irTx::kMaxRawTxDurationUs, waveform.totalUs);

    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::TooLong),
                      static_cast<int>(irTx::parseRawWaveform("62500,62500,62500,62500,"
                                                              "62500,62500,62500,62501",
                                                              waveform)));
    TEST_ASSERT_EQUAL_UINT32(500000, waveform.totalUs); // unchanged on failure

    // A raw param is also bounded by the WebSocket string cap (64 bytes), which
    // bounds the reachable timing count well below kMaxTimings.
    std::string maxValues;
    for (size_t i = 0; i < 32; ++i) { // 32 one-digit runs = 63 bytes
        if (i != 0) maxValues += ',';
        maxValues += '1';
    }
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::parseRawWaveform(maxValues, waveform)));
    TEST_ASSERT_EQUAL_UINT16(32, waveform.count);
    maxValues += ",1"; // 65 bytes exceeds kMaxRawParamBytes
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::TooLong),
                      static_cast<int>(irTx::parseRawWaveform(maxValues, waveform)));
    TEST_ASSERT_EQUAL_UINT16(32, waveform.count); // unchanged on failure
}

// --- replay conversion -----------------------------------------------------

void test_build_replay_waveform_converts_and_preserves_on_failure() {
    irRecord::Record record;
    record.count = 3;
    record.timingsUs[0] = 9000;
    record.timingsUs[1] = 4500;
    record.timingsUs[2] = 560;

    irTx::Waveform waveform;
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::buildReplayWaveform(record, waveform)));
    TEST_ASSERT_EQUAL_UINT16(3, waveform.count);
    TEST_ASSERT_EQUAL_UINT32(14060, waveform.totalUs);

    record.timingsUs[1] = 0; // 0 is the RMT end marker, never a duration
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::MalformedRaw),
                      static_cast<int>(irTx::buildReplayWaveform(record, waveform)));
    TEST_ASSERT_EQUAL_UINT16(3, waveform.count); // untouched

    record.timingsUs[1] = 400000;
    record.timingsUs[2] = 200000; // total > kMaxRawTxDurationUs
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::TooLong),
                      static_cast<int>(irTx::buildReplayWaveform(record, waveform)));
    TEST_ASSERT_EQUAL_UINT32(14060, waveform.totalUs);

    record.count = static_cast<uint16_t>(irTx::kMaxTimings + 1); // one past the timing cap
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::TooLong),
                      static_cast<int>(irTx::buildReplayWaveform(record, waveform)));
    TEST_ASSERT_EQUAL_UINT32(14060, waveform.totalUs); // untouched

    record.count = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::Empty),
                      static_cast<int>(irTx::buildReplayWaveform(record, waveform)));
}

// --- custom request validation --------------------------------------------

irTx::Error custom(const char *protocol, bool hasCode, const char *code, bool hasBits, int64_t bits, bool hasFreq,
                   int64_t freq, bool hasRaw, const char *raw, irTx::CustomRequest &out) {
    return irTx::buildCustom(std::string_view(protocol), hasCode, std::string_view(code ? code : ""), hasBits, bits,
                             hasFreq, freq, hasRaw, std::string_view(raw ? raw : ""), out);
}

void test_build_custom_protocol_requires_valid_bits_and_code() {
    irTx::CustomRequest request;
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(custom("NEC", true, "20DF10EF", true, 32, false, 0, false, nullptr, request)));
    TEST_ASSERT_FALSE(request.raw);
    TEST_ASSERT_EQUAL_UINT64(0x20DF10EFULL, request.code.value);
    TEST_ASSERT_EQUAL_UINT16(32, request.code.bits);
    TEST_ASSERT_EQUAL_UINT32(0, request.code.frequencyHz); // protocol carrier is fixed

    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::InvalidBits),
                      static_cast<int>(custom("NEC", true, "20DF10EF", true, 16, false, 0, false, nullptr, request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::InvalidBits),
                      static_cast<int>(custom("SAMSUNG", true, "E0E040BF", true, 48, false, 0, false, nullptr, request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::InvalidBits),
                      static_cast<int>(custom("RC5", true, "0C", true, 13, false, 0, false, nullptr, request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(custom("SONY", true, "A90", true, 12, false, 0, false, nullptr, request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(custom("SONY", true, "A90", true, 15, false, 0, false, nullptr, request)));

    // Code wider than the declared bits.
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::ValueOutOfRange),
                      static_cast<int>(custom("SONY", true, "1000", true, 12, false, 0, false, nullptr, request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::UnsupportedProtocol),
                      static_cast<int>(custom("RC6", true, "01", true, 12, false, 0, false, nullptr, request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::MissingRequired),
                      static_cast<int>(custom("NEC", false, nullptr, true, 32, false, 0, false, nullptr, request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::UnexpectedParam),
                      static_cast<int>(custom("NEC", true, "20DF10EF", true, 32, false, 0, true, "9000", request)));
}

void test_build_custom_raw_rules_and_frequency() {
    irTx::CustomRequest request;
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(custom("RAW", false, nullptr, false, 0, false, 0, true, "9000,4500", request)));
    TEST_ASSERT_TRUE(request.raw);
    TEST_ASSERT_EQUAL_UINT16(2, request.waveform.count);
    TEST_ASSERT_EQUAL_UINT32(irTx::kDefaultFrequencyHz, request.frequencyHz);

    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(custom("raw", false, nullptr, false, 0, true, 40000, true, "9000,4500", request)));
    TEST_ASSERT_EQUAL_UINT32(40000, request.frequencyHz);

    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::MissingRequired),
                      static_cast<int>(custom("RAW", false, nullptr, false, 0, false, 0, false, nullptr, request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::UnexpectedParam),
                      static_cast<int>(custom("RAW", true, "0C", false, 0, false, 0, true, "9000,4500", request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::UnexpectedParam),
                      static_cast<int>(custom("RAW", false, nullptr, true, 12, false, 0, true, "9000,4500", request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::InvalidFrequency),
                      static_cast<int>(custom("RAW", false, nullptr, false, 0, true, 29999, true, "9000", request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::InvalidFrequency),
                      static_cast<int>(custom("RAW", false, nullptr, false, 0, true, 60001, true, "9000", request)));
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::MalformedRaw),
                      static_cast<int>(custom("RAW", false, nullptr, false, 0, false, 0, true, "9000,", request)));
}

// --- protocol burst synthesis ---------------------------------------------

void test_nec_burst_exact_structure() {
    irTx::ProtocolCode code;
    code.protocol = irTx::Protocol::Nec;
    code.value = 0x20DF10EFULL;
    code.bits = 32;

    irTx::Burst burst;
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::buildProtocolBurst(code, burst)));
    TEST_ASSERT_EQUAL_UINT32(38000, burst.carrierHz);
    TEST_ASSERT_EQUAL_UINT8(33, burst.dutyPercent);
    TEST_ASSERT_TRUE(burstHasNoZeroDurations(burst));

    // header + 32 bits + footer + trailing pad
    TEST_ASSERT_EQUAL_UINT16(68, burst.count);
    TEST_ASSERT_EQUAL_UINT32(8960, burst.timings[0]);
    TEST_ASSERT_EQUAL_UINT32(4480, burst.timings[1]);
    // First four data bits of 0x20DF10EF (MSB first) are 0,0,1,0.
    TEST_ASSERT_EQUAL_UINT32(560, burst.timings[2]);
    TEST_ASSERT_EQUAL_UINT32(560, burst.timings[3]);
    TEST_ASSERT_EQUAL_UINT32(560, burst.timings[4]);
    TEST_ASSERT_EQUAL_UINT32(560, burst.timings[5]);
    TEST_ASSERT_EQUAL_UINT32(560, burst.timings[6]);
    TEST_ASSERT_EQUAL_UINT32(1680, burst.timings[7]);
    TEST_ASSERT_EQUAL_UINT32(560, burst.timings[8]);
    TEST_ASSERT_EQUAL_UINT32(560, burst.timings[9]);
    // The burst is padded to the protocol minimum command length.
    TEST_ASSERT_EQUAL_UINT32(108080, burstTotal(burst));
}

void test_sony_burst_repeats_three_frames() {
    irTx::ProtocolCode code;
    code.protocol = irTx::Protocol::Sony;
    code.value = 0xA90ULL; // Sony TV power (command 21, address 1)
    code.bits = 12;

    irTx::Burst burst;
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::buildProtocolBurst(code, burst)));
    TEST_ASSERT_EQUAL_UINT32(40000, burst.carrierHz);
    TEST_ASSERT_TRUE(burstHasNoZeroDurations(burst));
    // Three frames, each padded to the 45 ms Sony repeat length; adjacent
    // trailing/last-bit spaces merge, so 26 entries per frame.
    TEST_ASSERT_EQUAL_UINT16(78, burst.count);
    TEST_ASSERT_EQUAL_UINT32(135000, burstTotal(burst));
}

void test_panasonic_burst_exact_structure() {
    irTx::ProtocolCode code;
    code.protocol = irTx::Protocol::Panasonic;
    code.value = 0x40040100BCBDULL;
    code.bits = 48;

    irTx::Burst burst;
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::buildProtocolBurst(code, burst)));
    TEST_ASSERT_EQUAL_UINT32(36700, burst.carrierHz);
    TEST_ASSERT_EQUAL_UINT8(50, burst.dutyPercent);
    TEST_ASSERT_TRUE(burstHasNoZeroDurations(burst));
    TEST_ASSERT_EQUAL_UINT16(100, burst.count);
    TEST_ASSERT_EQUAL_UINT32(3456, burst.timings[0]);
    TEST_ASSERT_EQUAL_UINT32(1728, burst.timings[1]);
    TEST_ASSERT_EQUAL_UINT32(163296, burstTotal(burst));
}

void test_rc5_burst_structure() {
    irTx::ProtocolCode code;
    code.protocol = irTx::Protocol::Rc5;
    code.value = 0x0CULL; // power
    code.bits = 12;

    irTx::Burst burst;
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                      static_cast<int>(irTx::buildProtocolBurst(code, burst)));
    TEST_ASSERT_EQUAL_UINT32(36000, burst.carrierHz);
    TEST_ASSERT_EQUAL_UINT8(25, burst.dutyPercent);
    TEST_ASSERT_TRUE(burstHasNoZeroDurations(burst));
    TEST_ASSERT_EQUAL_UINT32(889, burst.timings[0]);
    TEST_ASSERT_EQUAL_UINT32(113778, burstTotal(burst));
}

void test_build_protocol_burst_rejects_bad_input_without_touching_out() {
    irTx::Burst burst;
    burst.count = 3;
    burst.timings[0] = 42;

    irTx::ProtocolCode code;
    code.protocol = irTx::Protocol::Nec;
    code.value = 0x20DF10EFULL;
    code.bits = 16;
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::InvalidBits),
                      static_cast<int>(irTx::buildProtocolBurst(code, burst)));
    TEST_ASSERT_EQUAL_UINT16(3, burst.count);

    code.bits = 32;
    code.value = 0x100000000ULL; // wider than 32 bits
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::ValueOutOfRange),
                      static_cast<int>(irTx::buildProtocolBurst(code, burst)));
    TEST_ASSERT_EQUAL_UINT16(3, burst.count);
}

// --- step sequencing -------------------------------------------------------

void test_plan_sequencing_protocol_raw_and_done() {
    irTx::Plan plan;
    plan.kind = irTx::PlanKind::Protocol;
    plan.code.protocol = irTx::Protocol::Nec;
    plan.code.value = 0x20DF10EFULL;
    plan.code.bits = 32;

    irTx::Step step = irTx::nextStep(plan);
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::StepKind::Protocol), static_cast<int>(step.kind));
    step = irTx::nextStep(plan);
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::StepKind::Done), static_cast<int>(step.kind));

    irTx::Plan raw;
    raw.kind = irTx::PlanKind::Raw;
    raw.waveform.count = 2;
    raw.waveform.timings[0] = 9000;
    raw.waveform.timings[1] = 4500;
    raw.frequencyHz = 38000;
    step = irTx::nextStep(raw);
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::StepKind::Raw), static_cast<int>(step.kind));
    TEST_ASSERT_EQUAL_UINT16(2, step.count);
    TEST_ASSERT_EQUAL_UINT32(9000, step.timings[0]);
    TEST_ASSERT_EQUAL_UINT32(38000, step.frequencyHz);
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::StepKind::Done), static_cast<int>(irTx::nextStep(raw).kind));
}

void test_tvbgone_yields_every_code_once_then_done() {
    irTx::Plan plan;
    plan.kind = irTx::PlanKind::Tvbgone;
    for (size_t i = 0; i < irTx::kTvbgoneCount; ++i) {
        const irTx::Step step = irTx::nextStep(plan);
        TEST_ASSERT_EQUAL(static_cast<int>(irTx::StepKind::Protocol), static_cast<int>(step.kind));
        const irTx::ProtocolCode &expected = irTx::tvbgoneCode(i);
        TEST_ASSERT_EQUAL(static_cast<int>(expected.protocol), static_cast<int>(step.code.protocol));
        TEST_ASSERT_EQUAL_UINT64(expected.value, step.code.value);
        TEST_ASSERT_EQUAL_UINT16(expected.bits, step.code.bits);
        TEST_ASSERT_TRUE(irTx::protocolSupportsBits(step.code.protocol, step.code.bits));
        TEST_ASSERT_NOT_NULL(irTx::tvbgoneModel(i));
        // Each table code must actually synthesize.
        irTx::Burst burst;
        TEST_ASSERT_EQUAL(static_cast<int>(irTx::Error::None),
                          static_cast<int>(irTx::buildProtocolBurst(step.code, burst)));
    }
    TEST_ASSERT_EQUAL(static_cast<int>(irTx::StepKind::Done), static_cast<int>(irTx::nextStep(plan).kind));
    // Out-of-range lookups clamp to the last entry instead of reading past it.
    TEST_ASSERT_EQUAL_UINT64(irTx::tvbgoneCode(irTx::kTvbgoneCount).value,
                             irTx::tvbgoneCode(irTx::kTvbgoneCount * 4).value);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_protocol_parse_and_names);
    RUN_TEST(test_carrier_and_bit_sizes_are_exact);
    RUN_TEST(test_parse_hex_code_boundaries);
    RUN_TEST(test_parse_raw_waveform_accepts_and_normalizes);
    RUN_TEST(test_parse_raw_waveform_rejects_malformed_without_touching_out);
    RUN_TEST(test_parse_raw_waveform_bounds);
    RUN_TEST(test_build_replay_waveform_converts_and_preserves_on_failure);
    RUN_TEST(test_build_custom_protocol_requires_valid_bits_and_code);
    RUN_TEST(test_build_custom_raw_rules_and_frequency);
    RUN_TEST(test_nec_burst_exact_structure);
    RUN_TEST(test_sony_burst_repeats_three_frames);
    RUN_TEST(test_panasonic_burst_exact_structure);
    RUN_TEST(test_rc5_burst_structure);
    RUN_TEST(test_build_protocol_burst_rejects_bad_input_without_touching_out);
    RUN_TEST(test_plan_sequencing_protocol_raw_and_done);
    RUN_TEST(test_tvbgone_yields_every_code_once_then_done);
    return UNITY_END();
}
