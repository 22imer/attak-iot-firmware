#include <unity.h>

#include <cstring>

#include "core/wifi_attack.h"

void setUp() {}
void tearDown() {}

namespace {

const uint8_t kBssid[6] = {0x02, 0x1A, 0x11, 0x33, 0x44, 0x55};

} // namespace

void test_deauth_frame_layout() {
    uint8_t frame[wifiAttack::kDeauthBytes] = {};
    const uint8_t dest[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t src[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    const size_t length = wifiAttack::buildDeauth(dest, src, kBssid, 7, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT(wifiAttack::kDeauthBytes, length);
    TEST_ASSERT_EQUAL_HEX8(0xC0, frame[0]); // management / deauth
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[1]);
    TEST_ASSERT_EQUAL_MEMORY(dest, frame + 4, 6);  // DA
    TEST_ASSERT_EQUAL_MEMORY(src, frame + 10, 6);  // SA
    TEST_ASSERT_EQUAL_MEMORY(kBssid, frame + 16, 6); // BSSID
    TEST_ASSERT_EQUAL_HEX8(0x07, frame[24]); // reason LE low
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[25]);
}

void test_deauth_rejects_small_buffer() {
    uint8_t frame[wifiAttack::kDeauthBytes - 1] = {};
    const uint8_t mac[6] = {};
    TEST_ASSERT_EQUAL_UINT(0, wifiAttack::buildDeauth(mac, mac, mac, 1, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT(0, wifiAttack::buildDeauth(nullptr, mac, mac, 1, frame, wifiAttack::kDeauthBytes));
}

void test_beacon_frame_layout() {
    uint8_t frame[wifiAttack::kMaxBeaconBytes] = {};
    const size_t length = wifiAttack::buildBeacon(kBssid, 6, "Test", 4, 0, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT(24 + 12 + 2 + 4 + 6 + 3, length);
    TEST_ASSERT_EQUAL_HEX8(0x80, frame[0]); // beacon
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[1]);
    for (int i = 0; i < 6; ++i) TEST_ASSERT_EQUAL_HEX8(0xFF, frame[4 + i]); // broadcast DA
    TEST_ASSERT_EQUAL_MEMORY(kBssid, frame + 10, 6);  // SA
    TEST_ASSERT_EQUAL_MEMORY(kBssid, frame + 16, 6);  // BSSID
    TEST_ASSERT_EQUAL_HEX8(0x64, frame[32]); // beacon interval LE = 100
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[33]);
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[34]); // open ESS capability
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[35]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[36]); // SSID IE
    TEST_ASSERT_EQUAL_HEX8(0x04, frame[37]);
    TEST_ASSERT_EQUAL_MEMORY("Test", frame + 38, 4);
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[42]); // supported rates IE
    TEST_ASSERT_EQUAL_HEX8(0x04, frame[43]);
    TEST_ASSERT_EQUAL_HEX8(0x03, frame[48]); // DS parameter IE
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[49]);
    TEST_ASSERT_EQUAL_HEX8(6, frame[50]);
}

void test_beacon_bounds_and_hidden_ssid() {
    uint8_t frame[wifiAttack::kMaxBeaconBytes] = {};
    char longSsid[40];
    std::memset(longSsid, 'A', sizeof(longSsid));
    TEST_ASSERT_EQUAL_UINT(0, wifiAttack::buildBeacon(kBssid, 1, longSsid, 33, 0, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT(0, wifiAttack::buildBeacon(kBssid, 1, "abc", 3, 0, frame, 24 + 12 + 2 + 3 + 6 + 2));
    TEST_ASSERT_EQUAL_UINT(0, wifiAttack::buildBeacon(kBssid, 1, nullptr, 3, 0, frame, sizeof(frame)));
    // Hidden SSID (len 0) is allowed with a nullptr pointer.
    TEST_ASSERT_EQUAL_UINT(24 + 12 + 2 + 6 + 3, wifiAttack::buildBeacon(kBssid, 1, nullptr, 0, 0, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[37]); // SSID IE length 0
}

void test_sequence_control_is_little_endian() {
    uint8_t frame[wifiAttack::kMaxBeaconBytes] = {};
    wifiAttack::buildBeacon(kBssid, 1, "x", 1, 0x123, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_HEX8(0x30, frame[22]); // (0x123 << 4) & 0xFF
    TEST_ASSERT_EQUAL_HEX8(0x12, frame[23]);
}

void test_mac_parse_and_format() {
    uint8_t mac[6] = {};
    TEST_ASSERT_TRUE(wifiAttack::parseMac("AA:BB:CC:DD:EE:FF", mac));
    TEST_ASSERT_EQUAL_HEX8(0xAA, mac[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, mac[5]);
    TEST_ASSERT_TRUE(wifiAttack::parseMac("aa-bb-cc-dd-ee-ff", mac));
    TEST_ASSERT_EQUAL_HEX8(0xAA, mac[0]);

    TEST_ASSERT_FALSE(wifiAttack::parseMac("AA:BB:CC:DD:EE", mac));
    TEST_ASSERT_FALSE(wifiAttack::parseMac("AA:BB:CC:DD:EE:FFFF", mac));
    TEST_ASSERT_FALSE(wifiAttack::parseMac("GG:BB:CC:DD:EE:FF", mac));
    TEST_ASSERT_FALSE(wifiAttack::parseMac("AA:BB:CC:DD:EE:F", mac));

    char text[wifiAttack::kMacTextBytes] = {};
    TEST_ASSERT_TRUE(wifiAttack::formatMac(mac, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", text);
    TEST_ASSERT_FALSE(wifiAttack::formatMac(mac, text, 4));
}

void test_derive_bssid_is_unicast_local_and_stable() {
    uint8_t a[6] = {};
    uint8_t b[6] = {};
    wifiAttack::deriveBssid(5, a);
    wifiAttack::deriveBssid(5, b);
    TEST_ASSERT_EQUAL_MEMORY(a, b, 6);
    TEST_ASSERT_EQUAL_HEX8(0x00, a[0] & 0x01); // unicast
    TEST_ASSERT_EQUAL_HEX8(0x02, a[0] & 0x02); // locally administered
    uint8_t c[6] = {};
    wifiAttack::deriveBssid(6, c);
    TEST_ASSERT_TRUE(std::memcmp(a, c, 6) != 0);
}

void test_lure_ssid_bounds_and_variation() {
    char out[wifiAttack::kMaxSsidBytes + 1] = {};
    const size_t first = wifiAttack::lureSsid(0, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, first);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(static_cast<uint32_t>(wifiAttack::kMaxSsidBytes), first);
    TEST_ASSERT_EQUAL_STRING("Free WiFi", out);

    char large[8];
    for (uint32_t i = 0; i < 64; ++i) {
        const size_t length = wifiAttack::lureSsid(i, out, sizeof(out));
        TEST_ASSERT_GREATER_THAN(0, length);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(static_cast<uint32_t>(wifiAttack::kMaxSsidBytes), length);
    }
    // Too-small buffer must be reported, not truncated.
    TEST_ASSERT_EQUAL_UINT(0, wifiAttack::lureSsid(0, large, 2));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_deauth_frame_layout);
    RUN_TEST(test_deauth_rejects_small_buffer);
    RUN_TEST(test_beacon_frame_layout);
    RUN_TEST(test_beacon_bounds_and_hidden_ssid);
    RUN_TEST(test_sequence_control_is_little_endian);
    RUN_TEST(test_mac_parse_and_format);
    RUN_TEST(test_derive_bssid_is_unicast_local_and_stable);
    RUN_TEST(test_lure_ssid_bounds_and_variation);
    return UNITY_END();
}
