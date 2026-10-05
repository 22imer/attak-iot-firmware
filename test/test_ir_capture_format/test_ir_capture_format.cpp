#include <unity.h>

#include <string>

#include "core/ir_capture_format.h"

void setUp() {}
void tearDown() {}

void test_unknown_keeps_raw_and_nulls_value() {
    // raw[0] is the leading idle gap (dropped); tick is the real 2us factor.
    const uint16_t raw[] = {100, 4500, 2250, 280};
    std::string json;
    TEST_ASSERT_EQUAL(static_cast<int>(irCapture::FormatResult::Ready),
                      static_cast<int>(irCapture::format(false, false, raw, 4, 2, "UNKNOWN", false, 0, json)));
    TEST_ASSERT_EQUAL_STRING(R"({"kind":"ir_capture","protocol":"UNKNOWN","value":null,"rawTimingsUs":[9000,4500,560]})",
                             json.c_str());
}

void test_repeat_is_ignored_and_keeps_payload() {
    const uint16_t raw[] = {100, 4500, 2250};
    std::string json = "previous";
    TEST_ASSERT_EQUAL(static_cast<int>(irCapture::FormatResult::Ignored),
                      static_cast<int>(irCapture::format(true, false, raw, 3, 2, "NEC", true, 0xAB, json)));
    TEST_ASSERT_EQUAL_STRING("previous", json.c_str());
}

void test_no_message_is_ignored() {
    std::string json = "previous";
    TEST_ASSERT_EQUAL(static_cast<int>(irCapture::FormatResult::Ignored),
                      static_cast<int>(irCapture::format(false, false, nullptr, 0, 2, "UNKNOWN", false, 0, json)));
    TEST_ASSERT_EQUAL_STRING("previous", json.c_str());
}

void test_512_timings_accepted_513_rejected() {
    uint16_t raw[514];
    for (size_t i = 0; i < 514; ++i) raw[i] = 1;

    std::string json;
    TEST_ASSERT_EQUAL(static_cast<int>(irCapture::FormatResult::Ready),
                      static_cast<int>(irCapture::format(false, false, raw, 513, 2, "UNKNOWN", false, 0, json)));

    std::string kept = "previous";
    TEST_ASSERT_EQUAL(static_cast<int>(irCapture::FormatResult::TooLong),
                      static_cast<int>(irCapture::format(false, false, raw, 514, 2, "UNKNOWN", false, 0, kept)));
    TEST_ASSERT_EQUAL_STRING("previous", kept.c_str());
}

void test_overflow_is_too_long() {
    const uint16_t raw[] = {1, 2, 3};
    std::string kept = "previous";
    TEST_ASSERT_EQUAL(static_cast<int>(irCapture::FormatResult::TooLong),
                      static_cast<int>(irCapture::format(false, true, raw, 3, 2, "UNKNOWN", false, 0, kept)));
    TEST_ASSERT_EQUAL_STRING("previous", kept.c_str());
}

void test_known_64bit_value_is_hex() {
    const uint16_t raw[] = {100, 20};
    std::string json;
    TEST_ASSERT_EQUAL(static_cast<int>(irCapture::FormatResult::Ready),
                      static_cast<int>(irCapture::format(false, false, raw, 2, 2, "SAMSUNG", true, 0xFEDCBA9876543210ULL,
                                                         json)));
    TEST_ASSERT_TRUE(json.find(R"("value":"0xFEDCBA9876543210")") != std::string::npos);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_unknown_keeps_raw_and_nulls_value);
    RUN_TEST(test_repeat_is_ignored_and_keeps_payload);
    RUN_TEST(test_no_message_is_ignored);
    RUN_TEST(test_512_timings_accepted_513_rejected);
    RUN_TEST(test_overflow_is_too_long);
    RUN_TEST(test_known_64bit_value_is_hex);
    return UNITY_END();
}
