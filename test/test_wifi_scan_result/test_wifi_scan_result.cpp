#include <unity.h>

#include "core/wifi_scan_result.h"

void setUp() {}
void tearDown() {}

namespace {

wifiScan::Candidate make(size_t index, int rssi, uint8_t lastBssid) {
    wifiScan::Candidate c;
    c.index = index;
    c.rssi = rssi;
    c.bssid = {0x02, 0x00, 0x00, 0x00, 0x00, lastBssid};
    return c;
}

} // namespace

void test_keeps_strongest_32_and_marks_truncated() {
    wifiScan::TopNetworks top;
    for (size_t i = 0; i < 33; ++i) top.consider(make(i, -90 + static_cast<int>(i), static_cast<uint8_t>(i)));

    TEST_ASSERT_EQUAL_UINT32(32, top.size());
    TEST_ASSERT_TRUE(top.truncated());
    TEST_ASSERT_EQUAL_UINT32(32, top.at(0).index); // strongest RSSI (-58)
    TEST_ASSERT_EQUAL_UINT32(1, top.at(31).index); // weakest kept; index 0 (-90) dropped
}

void test_equal_rssi_breaks_on_bssid_ascending() {
    wifiScan::TopNetworks top;
    top.consider(make(1, -50, 0x02));
    top.consider(make(2, -50, 0x01));

    TEST_ASSERT_EQUAL_UINT32(2, top.size());
    TEST_ASSERT_FALSE(top.truncated());
    TEST_ASSERT_EQUAL_UINT32(2, top.at(0).index);
    TEST_ASSERT_EQUAL_UINT32(1, top.at(1).index);
}

void test_empty_scan_is_empty_success_not_truncated() {
    wifiScan::TopNetworks top;
    TEST_ASSERT_EQUAL_UINT32(0, top.size());
    TEST_ASSERT_FALSE(top.truncated());
}

void test_exactly_32_is_not_truncated() {
    wifiScan::TopNetworks top;
    for (size_t i = 0; i < 32; ++i) top.consider(make(i, -40 - static_cast<int>(i), static_cast<uint8_t>(i)));
    TEST_ASSERT_EQUAL_UINT32(32, top.size());
    TEST_ASSERT_FALSE(top.truncated());
}

// The operator needs the actual class, not just a yes/no: "WPA2-PSK" and
// "WEP" are both `secure: true` but mean very different things on an audit.
void test_security_class_names_each_authmode() {
    TEST_ASSERT_EQUAL_STRING("OPEN", wifiScan::securityClass(wifiScan::kAuthOpen));
    TEST_ASSERT_EQUAL_STRING("WEP", wifiScan::securityClass(wifiScan::kAuthWep));
    TEST_ASSERT_EQUAL_STRING("WPA-PSK", wifiScan::securityClass(wifiScan::kAuthWpaPsk));
    TEST_ASSERT_EQUAL_STRING("WPA2-PSK", wifiScan::securityClass(wifiScan::kAuthWpa2Psk));
    TEST_ASSERT_EQUAL_STRING("WPA/WPA2-PSK", wifiScan::securityClass(wifiScan::kAuthWpaWpa2Psk));
    TEST_ASSERT_EQUAL_STRING("WPA2-ENTERPRISE", wifiScan::securityClass(wifiScan::kAuthWpa2Enterprise));
    TEST_ASSERT_EQUAL_STRING("WPA3-PSK", wifiScan::securityClass(wifiScan::kAuthWpa3Psk));
    TEST_ASSERT_EQUAL_STRING("WPA2/WPA3-PSK", wifiScan::securityClass(wifiScan::kAuthWpa2Wpa3Psk));
    TEST_ASSERT_EQUAL_STRING("WAPI-PSK", wifiScan::securityClass(wifiScan::kAuthWapiPsk));
    TEST_ASSERT_EQUAL_STRING("WPA3-ENTERPRISE-192BIT", wifiScan::securityClass(wifiScan::kAuthWpa3Ent192));
}

// A value outside the enum must never be reported as a known class or as
// protected: unknown means unknown, not "secure".
void test_unknown_authmode_is_unknown_and_not_secure() {
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", wifiScan::securityClass(200));
    TEST_ASSERT_FALSE(wifiScan::securityClassIsSecure(200));
}

void test_only_open_is_insecure() {
    TEST_ASSERT_FALSE(wifiScan::securityClassIsSecure(wifiScan::kAuthOpen));
    TEST_ASSERT_TRUE(wifiScan::securityClassIsSecure(wifiScan::kAuthWep));
    TEST_ASSERT_TRUE(wifiScan::securityClassIsSecure(wifiScan::kAuthWpa3Psk));
}


int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_keeps_strongest_32_and_marks_truncated);
    RUN_TEST(test_equal_rssi_breaks_on_bssid_ascending);
    RUN_TEST(test_security_class_names_each_authmode);
    RUN_TEST(test_unknown_authmode_is_unknown_and_not_secure);
    RUN_TEST(test_only_open_is_insecure);
    RUN_TEST(test_empty_scan_is_empty_success_not_truncated);
    RUN_TEST(test_exactly_32_is_not_truncated);
    return UNITY_END();
}
