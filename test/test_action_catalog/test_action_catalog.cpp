#include <unity.h>

#include "core/action_catalog.h"

void setUp() {}
void tearDown() {}

void test_unknown_action_is_null() {
    TEST_ASSERT_NULL(catalog::findAction(ModuleId::Wifi, "deauth"));
    TEST_ASSERT_NULL(catalog::findAction(ModuleId::Ir, "replay"));
    TEST_ASSERT_NULL(catalog::findAction(ModuleId::Pn532, ""));
}

void test_cross_module_dispatch_is_rejected() {
    TEST_ASSERT_NULL(catalog::findAction(ModuleId::Cc1101, ActionId::Scan));
    TEST_ASSERT_NULL(catalog::findAction(ModuleId::Wifi, ActionId::None));
    TEST_ASSERT_NULL(catalog::findAction(ModuleId::Wifi, ActionId::Capture));
}

#ifdef ENABLE_DISRUPTIVE
// Disruptive payloads (PLAN §2.4): registered only with -DENABLE_DISRUPTIVE,
// tier Disruptive, Continuous, and NOT radioExclusive (they use the device's
// own AP for raw TX / the captive portal).
void test_disruptive_descriptors_are_registered() {
    struct Case {
        ModuleId module;
        const char *id;
        ActionId action;
        size_t paramCount;
    };
    const Case cases[] = {
        {ModuleId::Cc1101, "rf_jammer", ActionId::RfJammer, 4},
        {ModuleId::Nrf24, "nrf_jammer", ActionId::NrfJammer, 3},
        {ModuleId::Wifi, "wifi_beacon", ActionId::WifiBeacon, 2},
        {ModuleId::Wifi, "wifi_deauth", ActionId::WifiDeauth, 5},
        {ModuleId::Wifi, "wifi_evil_portal", ActionId::WifiEvilPortal, 0},
    };
    for (const Case &c : cases) {
        const ActionDescriptor *byId = catalog::findAction(c.module, c.id);
        TEST_ASSERT_NOT_NULL(byId);
        TEST_ASSERT_EQUAL(static_cast<int>(c.action), static_cast<int>(byId->action));
        TEST_ASSERT_EQUAL(static_cast<int>(LegalTier::Disruptive), static_cast<int>(byId->tier));
        TEST_ASSERT_EQUAL(static_cast<int>(ActionKind::Continuous), static_cast<int>(byId->kind));
        TEST_ASSERT_FALSE(byId->radioExclusive);
        TEST_ASSERT_FALSE(byId->needsBuffer);
        TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(c.paramCount), static_cast<uint32_t>(byId->paramCount));
        // Dispatch resolves the same descriptor back from the ActionId.
        TEST_ASSERT_EQUAL_PTR(byId, catalog::findAction(c.module, c.action));
    }
}

void test_portal_has_no_params_and_disruptive_tier_name() {
    const ActionDescriptor *portal = catalog::findAction(ModuleId::Wifi, ActionId::WifiEvilPortal);
    TEST_ASSERT_NOT_NULL(portal);
    TEST_ASSERT_NULL(portal->params);
    TEST_ASSERT_EQUAL_STRING("disruptive", catalog::legalTierName(portal->tier));
    TEST_ASSERT_EQUAL_STRING("continuous", catalog::actionKindName(portal->kind));
}
#endif

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_unknown_action_is_null);
    RUN_TEST(test_cross_module_dispatch_is_rejected);
#ifdef ENABLE_DISRUPTIVE
    RUN_TEST(test_disruptive_descriptors_are_registered);
    RUN_TEST(test_portal_has_no_params_and_disruptive_tier_name);
#endif
    return UNITY_END();
}
