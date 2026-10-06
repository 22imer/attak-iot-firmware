#include <unity.h>

#include <ArduinoJson.h>

#include <cstdint>
#include <string>

#include "core/nrf_spectrum.h"

void setUp() {}
void tearDown() {}

using nrfSpectrum::Sweeper;

// --- dwell bounds ----------------------------------------------------------

void test_clamp_dwell_boundaries() {
    TEST_ASSERT_EQUAL_UINT32(150, nrfSpectrum::clampDwellUs(0));
    TEST_ASSERT_EQUAL_UINT32(150, nrfSpectrum::clampDwellUs(149));
    TEST_ASSERT_EQUAL_UINT32(150, nrfSpectrum::clampDwellUs(150));
    TEST_ASSERT_EQUAL_UINT32(300, nrfSpectrum::clampDwellUs(300));
    TEST_ASSERT_EQUAL_UINT32(5000, nrfSpectrum::clampDwellUs(5000));
    TEST_ASSERT_EQUAL_UINT32(5000, nrfSpectrum::clampDwellUs(5001));
    TEST_ASSERT_EQUAL_UINT32(150, nrfSpectrum::clampDwellUs(-40));
}

// --- sweep stepping --------------------------------------------------------

void test_begin_normalizes_range_and_clamps_dwell() {
    Sweeper sweeper;
    sweeper.begin(0, 60, 30, 10); // swapped + below floor
    TEST_ASSERT_EQUAL_UINT32(30, sweeper.first());
    TEST_ASSERT_EQUAL_UINT32(60, sweeper.last());
    TEST_ASSERT_EQUAL_UINT32(150, sweeper.dwellUs());
    TEST_ASSERT_EQUAL_UINT32(30, sweeper.channel());

    Sweeper other;
    other.begin(0, 200, 250, 99999); // clamped to the 0..125 range + ceiling
    TEST_ASSERT_EQUAL_UINT32(125, other.first());
    TEST_ASSERT_EQUAL_UINT32(125, other.last());
    TEST_ASSERT_EQUAL_UINT32(5000, other.dwellUs());
}

void test_ready_gates_on_settle_window() {
    Sweeper sweeper;
    sweeper.begin(1000, 10, 80, 300);
    TEST_ASSERT_FALSE(sweeper.ready(1299));
    TEST_ASSERT_TRUE(sweeper.ready(1300)); // exactly now + dwell
}

void test_submit_records_hits_and_wraps_sweep() {
    Sweeper sweeper;
    sweeper.begin(0, 0, 3, 150);
    TEST_ASSERT_EQUAL_UINT32(0, sweeper.channel());

    TEST_ASSERT_FALSE(sweeper.submit(true, 100)); // ch0 hit
    TEST_ASSERT_EQUAL_UINT32(1, sweeper.channel());
    TEST_ASSERT_EQUAL_UINT16(1, sweeper.hits(0));

    TEST_ASSERT_FALSE(sweeper.submit(false, 200)); // ch1 miss
    TEST_ASSERT_EQUAL_UINT32(2, sweeper.channel());
    TEST_ASSERT_EQUAL_UINT16(0, sweeper.hits(1));
    TEST_ASSERT_EQUAL_UINT32(0, sweeper.sweeps());

    TEST_ASSERT_FALSE(sweeper.submit(true, 350)); // ch2 -> advances, no wrap yet
    TEST_ASSERT_EQUAL_UINT32(3, sweeper.channel());

    TEST_ASSERT_TRUE(sweeper.submit(true, 500)); // ch3 -> wraps to first, sweep done
    TEST_ASSERT_EQUAL_UINT32(0, sweeper.channel());
    TEST_ASSERT_EQUAL_UINT32(1, sweeper.sweeps());
    TEST_ASSERT_EQUAL_UINT16(1, sweeper.hits(2));
    TEST_ASSERT_EQUAL_UINT16(1, sweeper.hits(3));
    TEST_ASSERT_EQUAL_UINT16(0, sweeper.hits(120)); // out-of-range read is safe
}

void test_single_channel_range_counts_every_probe_as_a_sweep() {
    Sweeper sweeper;
    sweeper.begin(0, 5, 5, 150);
    // first == last: every probe visits the whole range, so each one wraps.
    TEST_ASSERT_TRUE(sweeper.submit(true, 10));
    TEST_ASSERT_TRUE(sweeper.submit(true, 20));
    TEST_ASSERT_EQUAL_UINT32(5, sweeper.channel());
    TEST_ASSERT_EQUAL_UINT32(2, sweeper.sweeps());
    TEST_ASSERT_EQUAL_UINT16(2, sweeper.hits(5));
}

void test_hits_saturate() {
    Sweeper sweeper;
    sweeper.begin(0, 5, 5, 150);
    for (uint32_t i = 0; i < 70000; ++i) sweeper.submit(true, i);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, sweeper.hits(5)); // never wraps to zero
    TEST_ASSERT_EQUAL_UINT32(70000, sweeper.sweeps());
}

void test_micros_wrap_is_wrap_safe() {
    Sweeper sweeper;
    // 0xFFFFFF00 + 0x12C wraps past UINT32_MAX.
    sweeper.begin(0xFFFFFF00u, 0, 125, 300);
    TEST_ASSERT_FALSE(sweeper.ready(0x0000002Bu));
    TEST_ASSERT_TRUE(sweeper.ready(0x0000002Cu));
}

// --- bounded JSON encoding -------------------------------------------------

void test_spectrum_json_is_bounded_and_complete() {
    Sweeper sweeper;
    sweeper.begin(0, 0, 125, 300);
    for (int i = 0; i < 3; ++i) {
        for (uint8_t channel = 0; channel <= 125; ++channel) {
            sweeper.submit(true, static_cast<uint32_t>(i * 1000 + channel));
        }
    }

    std::string output;
    TEST_ASSERT_TRUE(nrfSpectrum::formatSpectrumJson(sweeper, output));
    TEST_ASSERT_TRUE(output.size() <= 1024);

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, output) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("nrf_spectrum", doc["kind"].as<const char *>());
    TEST_ASSERT_EQUAL_UINT32(3, doc["sweeps"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(0, doc["first"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(125, doc["last"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(300, doc["dwellUs"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(nrfSpectrum::kChannelCount, static_cast<uint32_t>(doc["hits"].size()));
    for (uint8_t channel = 0; channel <= 125; ++channel) {
        TEST_ASSERT_EQUAL_UINT32(3, doc["hits"][channel].as<uint32_t>());
    }
}

void test_spectrum_json_rejects_budget_too_small_without_touching_out() {
    Sweeper sweeper;
    sweeper.begin(0, 0, 125, 300);

    std::string output = "sentinel";
    TEST_ASSERT_FALSE(nrfSpectrum::formatSpectrumJson(sweeper, output, 32));
    TEST_ASSERT_EQUAL_STRING("sentinel", output.c_str());

    TEST_ASSERT_FALSE(nrfSpectrum::formatSpectrumJson(sweeper, output, 100));
    TEST_ASSERT_EQUAL_STRING("sentinel", output.c_str());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_clamp_dwell_boundaries);
    RUN_TEST(test_begin_normalizes_range_and_clamps_dwell);
    RUN_TEST(test_ready_gates_on_settle_window);
    RUN_TEST(test_submit_records_hits_and_wraps_sweep);
    RUN_TEST(test_single_channel_range_counts_every_probe_as_a_sweep);
    RUN_TEST(test_hits_saturate);
    RUN_TEST(test_micros_wrap_is_wrap_safe);
    RUN_TEST(test_spectrum_json_is_bounded_and_complete);
    RUN_TEST(test_spectrum_json_rejects_budget_too_small_without_touching_out);
    return UNITY_END();
}
