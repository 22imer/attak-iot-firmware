#include <unity.h>

#include <ArduinoJson.h>

#include "core/module_status.h"

void setUp() {}
void tearDown() {}

// Escaping regression: device text must survive as data, not raw JSON syntax.
// Deserialize and compare the original string so the test does not pin
// incidental formatting.
void test_detail_with_special_characters_is_escaped() {
    ModuleStatus status{"pn532", true, false, "error: \"timeout\"\\path\nline", "", 0,
                        ActionState::Error, ActionError::HardwareError, false, 0, 0};
    std::string json = statusToJson(status);

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("error: \"timeout\"\\path\nline", doc["detail"].as<const char *>());
    const std::string encoded = statusToJson(status);
    TEST_ASSERT_TRUE(encoded.find(R"(\"timeout\")") != std::string::npos);
}

// Consumer-visible wire contract: enums become spec strings, never numbers.
void test_enum_mapping_is_stable() {
    ModuleStatus ok{"wifi", true, true, "ready", "", 0, ActionState::Succeeded, ActionError::None, false, 3, 9};
    ModuleStatus timedOut{"wifi", true, true, "ready", "", 0, ActionState::Timeout, ActionError::ScanTimeout, true, 3, 9};

    JsonDocument okDoc;
    TEST_ASSERT_TRUE(deserializeJson(okDoc, statusToJson(ok)) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("succeeded", okDoc["actionState"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("", okDoc["actionError"].as<const char *>());
    TEST_ASSERT_EQUAL_UINT32(3, okDoc["resultSequence"].as<uint32_t>());

    JsonDocument timeoutDoc;
    TEST_ASSERT_TRUE(deserializeJson(timeoutDoc, statusToJson(timedOut)) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("timeout", timeoutDoc["actionState"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("scan_timeout", timeoutDoc["actionError"].as<const char *>());
    TEST_ASSERT_TRUE(timeoutDoc["cleanupPending"].as<bool>());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_detail_with_special_characters_is_escaped);
    RUN_TEST(test_enum_mapping_is_stable);
    return UNITY_END();
}
