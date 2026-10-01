#include <unity.h>

#include "core/module_status.h"

void setUp() {}
void tearDown() {}

void test_connected_status_encodes_correctly() {
    ModuleStatus status{"cc1101", true, true, "chip id 0x14", "", 12345};
    std::string json = statusToJson(status);

    TEST_ASSERT_TRUE(json.find(R"("module":"cc1101")") != std::string::npos);
    TEST_ASSERT_TRUE(json.find(R"("enabled":true)") != std::string::npos);
    TEST_ASSERT_TRUE(json.find(R"("connected":true)") != std::string::npos);
    TEST_ASSERT_TRUE(json.find(R"("detail":"chip id 0x14")") != std::string::npos);
    TEST_ASSERT_TRUE(json.find(R"("lastUpdateMs":12345)") != std::string::npos);
}

void test_disabled_status_encodes_correctly() {
    ModuleStatus status{"nrf24", false, false, "off", "", 0};
    std::string json = statusToJson(status);

    TEST_ASSERT_TRUE(json.find(R"("enabled":false)") != std::string::npos);
    TEST_ASSERT_TRUE(json.find(R"("connected":false)") != std::string::npos);
}

void test_output_field_carries_payload_result() {
    ModuleStatus status{"wifi", true, true, "ok", "SSID1,SSID2", 0};
    std::string json = statusToJson(status);

    TEST_ASSERT_TRUE(json.find(R"("output":"SSID1,SSID2")") != std::string::npos);
}

void test_detail_with_special_characters_is_escaped() {
    ModuleStatus status{"pn532", true, false, "error: \"timeout\" after 5s", "", 0};
    std::string json = statusToJson(status);

    // The literal, unescaped quote pair must not survive JSON encoding —
    // it must come back as an escaped \" so the payload stays valid JSON.
    TEST_ASSERT_TRUE(json.find(R"(\"timeout\")") != std::string::npos);
    TEST_ASSERT_FALSE(json.find(R"("timeout")") != std::string::npos);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_connected_status_encodes_correctly);
    RUN_TEST(test_disabled_status_encodes_correctly);
    RUN_TEST(test_output_field_carries_payload_result);
    RUN_TEST(test_detail_with_special_characters_is_escaped);
    return UNITY_END();
}
