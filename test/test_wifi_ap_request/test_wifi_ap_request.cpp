#include <unity.h>

#include "core/wifi_ap.h"

void setUp() {}
void tearDown() {}

// Replacing credentials must not enable an unrequested AP or cancel an
// explicit request preserved across a radio-exclusive action.
void test_credential_changes_preserve_operator_ap_intent() {
    wifiAp::ConfigCache cache;
    cache.remember({"InitialAP", "initialpass"});
    TEST_ASSERT_FALSE(cache.requested());

    cache.setRequested(true);
    cache.remember({"ReplacementAP", "replacementpass"});
    TEST_ASSERT_TRUE(cache.requested());
    TEST_ASSERT_EQUAL_STRING("ReplacementAP", cache.get()->ssid.c_str());

    cache.setRequested(false);
    cache.remember({"NextAP", "nextpassword"});
    TEST_ASSERT_FALSE(cache.requested());
    TEST_ASSERT_EQUAL_STRING("NextAP", cache.get()->ssid.c_str());
}

#ifdef ENABLE_DISRUPTIVE
// The evil twin is a lure: it must be joinable by anyone nearby, so the portal
// AP never carries the management AP's password — neither when cloning a name
// nor when keeping our own.
void test_portal_ap_is_always_open() {
    const wifiAp::ApCredentials managed{"AttakIoT", "supersecret"};

    const wifiAp::ApCredentials kept = wifiAp::portalCredentials(managed, "");
    TEST_ASSERT_EQUAL_STRING("AttakIoT", kept.ssid.c_str());
    TEST_ASSERT_TRUE(kept.open());

    const wifiAp::ApCredentials cloned = wifiAp::portalCredentials(managed, "VanTot");
    TEST_ASSERT_EQUAL_STRING("VanTot", cloned.ssid.c_str());
    TEST_ASSERT_TRUE(cloned.open()); // clone must not leak the management key

    // An open management AP stays open, and a cloned name still wins.
    const wifiAp::ApCredentials open{"AttakIoT", ""};
    TEST_ASSERT_TRUE(wifiAp::portalCredentials(open, "").open());
}

// Restart policy: a password-protected live AP must be restarted (its key
// cannot serve the portal), while an already-open AP with the right name is
// reused so no client is dropped by a needless restart.
void test_portal_restart_policy() {
    const wifiAp::ApCredentials managed{"AttakIoT", "supersecret"};
    const wifiAp::ApCredentials open{"AttakIoT", ""};

    TEST_ASSERT_TRUE(wifiAp::portalNeedsRestart(&managed, "", true));   // protected AP
    TEST_ASSERT_FALSE(wifiAp::portalNeedsRestart(&open, "", true));     // reusable as-is
    TEST_ASSERT_TRUE(wifiAp::portalNeedsRestart(&open, "VanTot", true));// renamed
    TEST_ASSERT_TRUE(wifiAp::portalNeedsRestart(&open, "", false));     // AP was down
    TEST_ASSERT_TRUE(wifiAp::portalNeedsRestart(&managed, "", false));   // down + protected
    // Never reused without a cache to take the SSID from.
    TEST_ASSERT_TRUE(wifiAp::portalNeedsRestart(nullptr, "VanTot", true));
}
#endif

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_credential_changes_preserve_operator_ap_intent);
#ifdef ENABLE_DISRUPTIVE
    RUN_TEST(test_portal_ap_is_always_open);
    RUN_TEST(test_portal_restart_policy);
#endif
    return UNITY_END();
}
