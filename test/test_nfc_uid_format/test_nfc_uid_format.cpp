#include <unity.h>

#include "core/nfc_uid_format.h"

void setUp() {}
void tearDown() {}

void test_two_digit_padding_and_uppercase() {
    const uint8_t uid[4] = {0x04, 0xAB, 0x01, 0x02};
    TEST_ASSERT_EQUAL_STRING("04:AB:01:02", nfcUid::formatHex(uid, 4).c_str());
}

// A real display bug class: a nibble must not be dropped for values < 0x10.
void test_nibble_padding_not_dropped() {
    const uint8_t uid[4] = {0x0A, 0x00, 0xFF, 0x0B};
    TEST_ASSERT_EQUAL_STRING("0A:00:FF:0B", nfcUid::formatHex(uid, 4).c_str());
}

void test_ten_byte_uid() {
    const uint8_t uid[10] = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
    TEST_ASSERT_EQUAL_STRING("04:11:22:33:44:55:66:77:88:99", nfcUid::formatHex(uid, 10).c_str());
}

void test_payload_shape() {
    const uint8_t uid[4] = {0x04, 0xAB, 0x01, 0x02};
    TEST_ASSERT_EQUAL_STRING(R"({"kind":"nfc_uid","uid":"04:AB:01:02"})", nfcUid::toJson(uid, 4).c_str());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_two_digit_padding_and_uppercase);
    RUN_TEST(test_nibble_padding_not_dropped);
    RUN_TEST(test_ten_byte_uid);
    RUN_TEST(test_payload_shape);
    return UNITY_END();
}
