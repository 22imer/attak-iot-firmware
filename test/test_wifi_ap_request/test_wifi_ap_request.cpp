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

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_credential_changes_preserve_operator_ap_intent);
    return UNITY_END();
}
