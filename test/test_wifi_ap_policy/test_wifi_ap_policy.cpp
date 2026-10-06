#include <unity.h>

#include "core/wifi_ap.h"

void setUp() {}
void tearDown() {}

using wifiAp::ApCredentials;

namespace {

// Compiled-in fallback credentials ship with DeviceConfig's defaults.
DeviceConfig defaults() { return DeviceConfig{}; }

} // namespace

// --- password policy -------------------------------------------------------

void test_password_usable_boundaries() {
    TEST_ASSERT_TRUE(wifiAp::passwordUsable("")); // empty => open AP
    TEST_ASSERT_FALSE(wifiAp::passwordUsable("1234567"));   // 7 chars
    TEST_ASSERT_TRUE(wifiAp::passwordUsable("12345678"));   // 8 chars, WPA2 minimum
    TEST_ASSERT_TRUE(wifiAp::passwordUsable(std::string(63, 'x'))); // 63 chars, maximum
    TEST_ASSERT_FALSE(wifiAp::passwordUsable(std::string(64, 'x')));
}

// --- effective credential resolution ---------------------------------------

void test_usable_configured_credentials_are_used() {
    DeviceConfig cfg;
    cfg.apSsid = "FieldAP";
    cfg.apPassword = "fieldpass1";

    const ApCredentials eff = wifiAp::resolveCredentials(cfg);
    TEST_ASSERT_EQUAL_STRING("FieldAP", eff.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("fieldpass1", eff.password.c_str());
    TEST_ASSERT_FALSE(eff.open());
}

void test_invalid_configured_password_falls_back_to_default() {
    DeviceConfig cfg;
    cfg.apSsid = "FieldAP";
    cfg.apPassword = "short"; // hand-edited config

    const ApCredentials eff = wifiAp::resolveCredentials(cfg);
    TEST_ASSERT_EQUAL_STRING("FieldAP", eff.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(defaults().apPassword.c_str(), eff.password.c_str());
    TEST_ASSERT_FALSE(eff.open());
}

void test_empty_configured_ssid_falls_back_to_default() {
    DeviceConfig cfg;
    cfg.apSsid = "";
    cfg.apPassword = "fieldpass1";

    const ApCredentials eff = wifiAp::resolveCredentials(cfg);
    TEST_ASSERT_EQUAL_STRING(defaults().apSsid.c_str(), eff.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("fieldpass1", eff.password.c_str());
}

void test_empty_configured_password_means_open_ap() {
    DeviceConfig cfg;
    cfg.apSsid = "FieldAP";
    cfg.apPassword = "";

    const ApCredentials eff = wifiAp::resolveCredentials(cfg);
    TEST_ASSERT_EQUAL_STRING("FieldAP", eff.ssid.c_str());
    TEST_ASSERT_TRUE(eff.open());
}

void test_unusable_default_password_ends_as_open_ap() {
    DeviceConfig cfg;
    cfg.apSsid = "FieldAP";
    cfg.apPassword = "bad"; // unusable configured password

    DeviceConfig fallback;
    fallback.apPassword = "tiny"; // unusable compiled-in default

    const ApCredentials eff = wifiAp::resolveCredentials(cfg, fallback);
    TEST_ASSERT_EQUAL_STRING("FieldAP", eff.ssid.c_str());
    TEST_ASSERT_TRUE(eff.open()); // last resort: never leave the device with no AP
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_password_usable_boundaries);
    RUN_TEST(test_usable_configured_credentials_are_used);
    RUN_TEST(test_invalid_configured_password_falls_back_to_default);
    RUN_TEST(test_empty_configured_ssid_falls_back_to_default);
    RUN_TEST(test_empty_configured_password_means_open_ap);
    RUN_TEST(test_unusable_default_password_ends_as_open_ap);
    return UNITY_END();
}
