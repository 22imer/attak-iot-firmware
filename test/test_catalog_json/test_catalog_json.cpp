// Smoke for the catalog -> dashboard contract: the exact `catalog` frame the
// firmware broadcasts must carry the disruptive descriptors (with tier/kind the
// dashboard validates) when built with -DENABLE_DISRUPTIVE.
#include <ArduinoJson.h>
#include <unity.h>

#include <string>

#include "core/action_catalog.h"

void setUp() {}
void tearDown() {}

#ifdef ENABLE_DISRUPTIVE
namespace {

// Returns the action object for `id` inside `module`, or a null JsonObject.
JsonObjectConst findAction(const JsonDocument &doc, const char *module, const char *id) {
    for (JsonObjectConst entry : doc["modules"].as<JsonArrayConst>()) {
        if (entry["module"].as<const char *>() == nullptr) continue;
        if (std::string(entry["module"].as<const char *>()) != module) continue;
        for (JsonObjectConst action : entry["actions"].as<JsonArrayConst>()) {
            if (action["id"].as<const char *>() != nullptr && std::string(action["id"].as<const char *>()) == id) {
                return action;
            }
        }
    }
    return JsonObjectConst();
}

} // namespace

void test_catalog_frame_carries_disruptive_actions() {
    const std::string json = catalogToJson();
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("catalog", doc["type"].as<const char *>());

    struct Case {
        const char *module;
        const char *id;
        size_t params;
    };
    const Case cases[] = {
        {"cc1101", "rf_jammer", 4}, {"nrf24", "nrf_jammer", 3},   {"wifi", "wifi_beacon", 2},
        {"wifi", "wifi_deauth", 5}, {"wifi", "wifi_evil_portal", 0},
    };
    for (const Case &c : cases) {
        const JsonObjectConst action = findAction(doc, c.module, c.id);
        TEST_ASSERT_FALSE(action.isNull());
        TEST_ASSERT_EQUAL_STRING("disruptive", action["tier"].as<const char *>());
        TEST_ASSERT_EQUAL_STRING("continuous", action["kind"].as<const char *>());
        TEST_ASSERT_FALSE(action["radioExclusive"].as<bool>());
        TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(c.params), action["params"].as<JsonArrayConst>().size());
    }
}

void test_catalog_disruptive_param_types_are_dashboard_legal() {
    const std::string json = catalogToJson();
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);

    // The dashboard rejects a String spec whose maxLength is outside 0..64 and a
    // numeric spec without a finite min<=max; assert every emitted spec passes.
    for (JsonObjectConst entry : doc["modules"].as<JsonArrayConst>()) {
        for (JsonObjectConst action : entry["actions"].as<JsonArrayConst>()) {
            for (JsonObjectConst param : action["params"].as<JsonArrayConst>()) {
                const char *type = param["type"].as<const char *>();
                TEST_ASSERT_NOT_NULL(type);
                if (std::string(type) == "string") {
                    const int maxLength = param["maxLength"].as<int>();
                    TEST_ASSERT_TRUE(maxLength >= 0 && maxLength <= 64);
                } else if (std::string(type) == "integer" || std::string(type) == "number") {
                    const double min = param["min"].as<double>();
                    const double max = param["max"].as<double>();
                    TEST_ASSERT_TRUE(min <= max);
                }
            }
        }
    }
}
#endif

int main(int, char **) {
    UNITY_BEGIN();
#ifdef ENABLE_DISRUPTIVE
    RUN_TEST(test_catalog_frame_carries_disruptive_actions);
    RUN_TEST(test_catalog_disruptive_param_types_are_dashboard_legal);
#endif
    return UNITY_END();
}
