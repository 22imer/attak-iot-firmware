#include <unity.h>

#include "core/module_status.h"

void setUp() {}
void tearDown() {}

void test_connected_status_encodes_correctly() {
    ModuleStatus status{"cc1101", true, "chip id 0x14", 12345};
    std::string json = statusToJson(status);

    TEST_ASSERT_TRUE(json.find(R"("module":"cc1101")") != std::string::npos);
    TEST_ASSERT_TRUE(json.find(R"("connected":true)") != std::string::npos);
    TEST_ASSERT_TRUE(json.find(R"("detail":"chip id 0x14")") != std::string::npos);
    TEST_ASSERT_TRUE(json.find(R"("lastUpdateMs":12345)") != std::string::npos);
}

void test_disconnected_status_encodes_correctly() {
    ModuleStatus status{"nrf24", false, "no response", 0};
    std::string json = statusToJson(status);

    TEST_ASSERT_TRUE(json.find(R"("connected":false)") != std::string::npos);
}

void test_detail_with_special_characters_is_escaped() {
    ModuleStatus status{"pn532", false, "error: \"timeout\" after 5s", 0};
    std::string json = statusToJson(status);

    // The literal, unescaped quote pair must not survive JSON encoding —
    // it must come back as an escaped \" so the payload stays valid JSON.
    TEST_ASSERT_TRUE(json.find(R"(\"timeout\")") != std::string::npos);
    TEST_ASSERT_FALSE(json.find(R"("timeout")") != std::string::npos);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_connected_status_encodes_correctly);
    RUN_TEST(test_disconnected_status_encodes_correctly);
    RUN_TEST(test_detail_with_special_characters_is_escaped);
    return UNITY_END();
}
