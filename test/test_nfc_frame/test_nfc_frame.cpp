#include <unity.h>

#include "core/nfc_frame.h"

void setUp() {}
void tearDown() {}

namespace {

// Real InListPassiveTarget responses: 4-byte (19B) and 10-byte (25B) UIDs.
const uint8_t kFrame4[] = {0x00, 0x00, 0xFF, 0x0C, 0xF4, 0xD5, 0x4B, 0x01, 0x01, 0x00,
                           0x04, 0x08, 0x04, 0x04, 0xAB, 0x01, 0x02, 0x1C, 0x00};
const uint8_t kFrame10[] = {0x00, 0x00, 0xFF, 0x12, 0xEE, 0xD5, 0x4B, 0x01, 0x01, 0x00,
                            0x04, 0x08, 0x0A, 0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                            0x77, 0x88, 0x99, 0xC7, 0x00};
const uint8_t kNoTarget[] = {0x00, 0x00, 0xFF, 0x03, 0xFD, 0xD5, 0x4B, 0x00, 0xE0, 0x00};
const uint8_t kAck[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

} // namespace

void test_valid_4byte_uid_frame() {
    std::array<uint8_t, nfcFrame::kMaxUidLength> uid{};
    uint8_t len = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(nfcFrame::ParseStatus::Uid),
                      static_cast<int>(nfcFrame::parseUidFrame(kFrame4, sizeof(kFrame4), uid, len)));
    TEST_ASSERT_EQUAL_UINT8(4, len);
    const uint8_t expected[4] = {0x04, 0xAB, 0x01, 0x02};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, uid.data(), 4);
}

void test_valid_10byte_uid_frame() {
    std::array<uint8_t, nfcFrame::kMaxUidLength> uid{};
    uint8_t len = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(nfcFrame::ParseStatus::Uid),
                      static_cast<int>(nfcFrame::parseUidFrame(kFrame10, sizeof(kFrame10), uid, len)));
    TEST_ASSERT_EQUAL_UINT8(10, len);
    const uint8_t expected[10] = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, uid.data(), 10);
}

void test_short_frame_is_malformed() {
    std::array<uint8_t, nfcFrame::kMaxUidLength> uid{};
    uint8_t len = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(nfcFrame::ParseStatus::Malformed),
                      static_cast<int>(nfcFrame::parseUidFrame(kFrame10, 5, uid, len)));
}

void test_bad_checksum_is_malformed() {
    uint8_t frame[sizeof(kFrame10)];
    for (size_t i = 0; i < sizeof(frame); ++i) frame[i] = kFrame10[i];
    frame[23] ^= 0xFF; // corrupt DCS
    std::array<uint8_t, nfcFrame::kMaxUidLength> uid{};
    uint8_t len = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(nfcFrame::ParseStatus::Malformed),
                      static_cast<int>(nfcFrame::parseUidFrame(frame, sizeof(frame), uid, len)));
}

void test_unexpected_uid_length_is_malformed() {
    uint8_t frame[sizeof(kFrame10)];
    for (size_t i = 0; i < sizeof(frame); ++i) frame[i] = kFrame10[i];
    frame[12] = 5;    // UID length 5 is not 4/7/10
    frame[23] = 0xCC; // recompute DCS so the failure is isolated to the length check
    std::array<uint8_t, nfcFrame::kMaxUidLength> uid{};
    uint8_t len = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(nfcFrame::ParseStatus::Malformed),
                      static_cast<int>(nfcFrame::parseUidFrame(frame, sizeof(frame), uid, len)));
}

void test_no_target_frame() {
    std::array<uint8_t, nfcFrame::kMaxUidLength> uid{};
    uint8_t len = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(nfcFrame::ParseStatus::NoTarget),
                      static_cast<int>(nfcFrame::parseUidFrame(kNoTarget, sizeof(kNoTarget), uid, len)));
}

void test_header_reports_total() {
    uint16_t total = 0;
    TEST_ASSERT_TRUE(nfcFrame::parseResponseHeader(kFrame10, 6, total));
    TEST_ASSERT_EQUAL_UINT16(25, total);
    TEST_ASSERT_TRUE(nfcFrame::parseResponseHeader(kFrame10, sizeof(kFrame10), total));
    TEST_ASSERT_EQUAL_UINT16(25, total);
}

void test_ack_frame() {
    TEST_ASSERT_TRUE(nfcFrame::isAckFrame(kAck, sizeof(kAck)));
    const uint8_t bad[6] = {0x00, 0x00, 0xFF, 0x01, 0xFF, 0x00};
    TEST_ASSERT_FALSE(nfcFrame::isAckFrame(bad, sizeof(bad)));
    TEST_ASSERT_FALSE(nfcFrame::isAckFrame(kAck, 5));
}

void test_command_frame_bytes() {
    uint8_t command[11];
    const size_t n = nfcFrame::buildReadUidCommand(command, sizeof(command));
    TEST_ASSERT_EQUAL_UINT8(11, n);
    const uint8_t expected[11] = {0x00, 0x00, 0xFF, 0x04, 0xFC, 0xD4, 0x4A, 0x01, 0x00, 0xE1, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, command, 11);
}

void test_command_frame_capacity_guard() {
    uint8_t command[10];
    TEST_ASSERT_EQUAL_UINT8(0, nfcFrame::buildReadUidCommand(command, sizeof(command)));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_4byte_uid_frame);
    RUN_TEST(test_valid_10byte_uid_frame);
    RUN_TEST(test_short_frame_is_malformed);
    RUN_TEST(test_bad_checksum_is_malformed);
    RUN_TEST(test_unexpected_uid_length_is_malformed);
    RUN_TEST(test_no_target_frame);
    RUN_TEST(test_header_reports_total);
    RUN_TEST(test_ack_frame);
    RUN_TEST(test_command_frame_bytes);
    RUN_TEST(test_command_frame_capacity_guard);
    return UNITY_END();
}
