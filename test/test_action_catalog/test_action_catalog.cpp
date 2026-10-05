#include <unity.h>

#include <ArduinoJson.h>

#include "core/action_catalog.h"

void setUp() {}
void tearDown() {}

void test_migrated_actions_resolve() {
    const ActionDescriptor *scan = catalog::findAction(ModuleId::Wifi, "scan");
    TEST_ASSERT_NOT_NULL(scan);
    TEST_ASSERT_EQUAL(static_cast<int>(ActionId::Scan), static_cast<int>(scan->action));

    const ActionDescriptor *readUid = catalog::findAction(ModuleId::Pn532, "read_uid");
    TEST_ASSERT_NOT_NULL(readUid);
    TEST_ASSERT_EQUAL(static_cast<int>(ActionId::ReadUid), static_cast<int>(readUid->action));

    const ActionDescriptor *capture = catalog::findAction(ModuleId::Ir, "capture");
    TEST_ASSERT_NOT_NULL(capture);
    TEST_ASSERT_EQUAL(static_cast<int>(ActionId::Capture), static_cast<int>(capture->action));
    TEST_ASSERT_EQUAL(static_cast<int>(ActionKind::Record), static_cast<int>(capture->kind));
}

void test_unknown_action_is_null() {
    TEST_ASSERT_NULL(catalog::findAction(ModuleId::Wifi, "deauth"));
    TEST_ASSERT_NULL(catalog::findAction(ModuleId::Ir, "replay"));
    TEST_ASSERT_NULL(catalog::findAction(ModuleId::Pn532, ""));
}

void test_modules_without_actions_are_empty() {
    size_t count = 123;
    TEST_ASSERT_NULL(catalog::moduleActions(ModuleId::Cc1101, count));
    TEST_ASSERT_EQUAL_UINT32(0, count);

    count = 123;
    TEST_ASSERT_NULL(catalog::moduleActions(ModuleId::Nrf24, count));
    TEST_ASSERT_EQUAL_UINT32(0, count);

    // CC1101 declaring no actions means any action for it is rejected.
    TEST_ASSERT_NULL(catalog::findAction(ModuleId::Cc1101, "scan"));
}

void test_module_action_counts() {
    size_t count = 0;
    TEST_ASSERT_NOT_NULL(catalog::moduleActions(ModuleId::Wifi, count));
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_NOT_NULL(catalog::moduleActions(ModuleId::Ir, count));
    TEST_ASSERT_EQUAL_UINT32(1, count);
}

void test_enum_names() {
    TEST_ASSERT_EQUAL_STRING("oneshot", catalog::actionKindName(ActionKind::OneShot));
    TEST_ASSERT_EQUAL_STRING("record", catalog::actionKindName(ActionKind::Record));
    TEST_ASSERT_EQUAL_STRING("observe", catalog::legalTierName(LegalTier::Observe));
    TEST_ASSERT_EQUAL_STRING("disruptive", catalog::legalTierName(LegalTier::Disruptive));
}

void test_catalog_json_shape() {
    std::string json = catalogToJson();
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("catalog", doc["type"].as<const char *>());

    JsonArray modules = doc["modules"].as<JsonArray>();
    TEST_ASSERT_EQUAL_UINT32(5, modules.size()); // all selectable modules present

    // Find the wifi module entry and check its single action.
    bool sawWifi = false, sawCc1101 = false;
    for (JsonObject module : modules) {
        const std::string name = module["module"].as<const char *>();
        JsonArray actions = module["actions"].as<JsonArray>();
        if (name == "wifi") {
            sawWifi = true;
            TEST_ASSERT_EQUAL_UINT32(1, actions.size());
            TEST_ASSERT_EQUAL_STRING("scan", actions[0]["id"].as<const char *>());
            TEST_ASSERT_EQUAL_STRING("oneshot", actions[0]["kind"].as<const char *>());
            TEST_ASSERT_EQUAL_STRING("observe", actions[0]["tier"].as<const char *>());
        } else if (name == "cc1101") {
            sawCc1101 = true;
            TEST_ASSERT_EQUAL_UINT32(0, actions.size()); // present but no actions
        }
    }
    TEST_ASSERT_TRUE(sawWifi);
    TEST_ASSERT_TRUE(sawCc1101);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_migrated_actions_resolve);
    RUN_TEST(test_unknown_action_is_null);
    RUN_TEST(test_modules_without_actions_are_empty);
    RUN_TEST(test_module_action_counts);
    RUN_TEST(test_enum_names);
    RUN_TEST(test_catalog_json_shape);
    return UNITY_END();
}
