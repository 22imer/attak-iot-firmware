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
        {ModuleId::Wifi, "wifi_evil_portal", ActionId::WifiEvilPortal, 7},
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

// The evil twin takes optional parameters only: `ssid` clones the AP name,
// `channel` moves it, and the companion deauth (`deauth`, `bssid`, `client`,
// `reason`, `intervalMs`) stays off unless the operator asks for it. Nothing may
// become required, or a plain portal run would need parameters it never had.
void test_portal_clone_params_are_optional_and_bounded() {
    const ActionDescriptor *portal = catalog::findAction(ModuleId::Wifi, ActionId::WifiEvilPortal);
    TEST_ASSERT_NOT_NULL(portal);
    TEST_ASSERT_EQUAL_STRING("disruptive", catalog::legalTierName(portal->tier));
    TEST_ASSERT_EQUAL_STRING("continuous", catalog::actionKindName(portal->kind));
    TEST_ASSERT_EQUAL_UINT32(7, static_cast<uint32_t>(portal->paramCount));

    TEST_ASSERT_EQUAL_STRING("ssid", portal->params[0].name);
    TEST_ASSERT_EQUAL_STRING("string", catalog::paramTypeName(portal->params[0].type));
    TEST_ASSERT_FALSE(portal->params[0].required);
    TEST_ASSERT_EQUAL_UINT32(32, static_cast<uint32_t>(portal->params[0].maxLength));

    TEST_ASSERT_EQUAL_STRING("channel", portal->params[1].name);
    TEST_ASSERT_EQUAL_STRING("integer", catalog::paramTypeName(portal->params[1].type));
    TEST_ASSERT_FALSE(portal->params[1].required);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(portal->params[1].minimum));
    TEST_ASSERT_EQUAL_INT(13, static_cast<int>(portal->params[1].maximum));

    // The deauth companion is a boolean switch; leaving it out (or false) must
    // keep the portal a pure captive portal.
    TEST_ASSERT_EQUAL_STRING("deauth", portal->params[2].name);
    TEST_ASSERT_EQUAL_STRING("boolean", catalog::paramTypeName(portal->params[2].type));
    TEST_ASSERT_FALSE(portal->params[2].required);

    // `bssid` is the target AP to push clients off; it stays optional in the
    // schema because the portal runs fine without deauth, and the module
    // rejects it as invalid_params when deauth is on without a target.
    TEST_ASSERT_EQUAL_STRING("bssid", portal->params[3].name);
    TEST_ASSERT_EQUAL_STRING("string", catalog::paramTypeName(portal->params[3].type));
    TEST_ASSERT_FALSE(portal->params[3].required);
    TEST_ASSERT_EQUAL_UINT32(17, static_cast<uint32_t>(portal->params[3].maxLength));

    TEST_ASSERT_EQUAL_STRING("client", portal->params[4].name);
    TEST_ASSERT_EQUAL_STRING("string", catalog::paramTypeName(portal->params[4].type));
    TEST_ASSERT_FALSE(portal->params[4].required);
    TEST_ASSERT_EQUAL_UINT32(17, static_cast<uint32_t>(portal->params[4].maxLength));

    TEST_ASSERT_EQUAL_STRING("reason", portal->params[5].name);
    TEST_ASSERT_EQUAL_STRING("integer", catalog::paramTypeName(portal->params[5].type));
    TEST_ASSERT_FALSE(portal->params[5].required);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(portal->params[5].minimum));
    TEST_ASSERT_EQUAL_INT(65535, static_cast<int>(portal->params[5].maximum));

    TEST_ASSERT_EQUAL_STRING("intervalMs", portal->params[6].name);
    TEST_ASSERT_EQUAL_STRING("integer", catalog::paramTypeName(portal->params[6].type));
    TEST_ASSERT_FALSE(portal->params[6].required);
    TEST_ASSERT_EQUAL_INT(20, static_cast<int>(portal->params[6].minimum));
    TEST_ASSERT_EQUAL_INT(5000, static_cast<int>(portal->params[6].maximum));
}
#endif

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_unknown_action_is_null);
    RUN_TEST(test_cross_module_dispatch_is_rejected);
#ifdef ENABLE_DISRUPTIVE
    RUN_TEST(test_disruptive_descriptors_are_registered);
    RUN_TEST(test_portal_clone_params_are_optional_and_bounded);
#endif
    return UNITY_END();
}
