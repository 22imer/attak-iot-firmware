#include <unity.h>

#include "core/ws_command.h"

void setUp() {}
void tearDown() {}

void test_enable_command_parses() {
    WsCommand cmd = parseWsCommand(R"({"module":"cc1101","cmd":"enable"})");

    TEST_ASSERT_TRUE(cmd.valid);
    TEST_ASSERT_EQUAL_STRING("cc1101", cmd.module.c_str());
    TEST_ASSERT_EQUAL_STRING("enable", cmd.cmd.c_str());
}

void test_action_command_parses_with_action_name() {
    WsCommand cmd = parseWsCommand(R"({"module":"ir","cmd":"action","action":"replay"})");

    TEST_ASSERT_TRUE(cmd.valid);
    TEST_ASSERT_EQUAL_STRING("ir", cmd.module.c_str());
    TEST_ASSERT_EQUAL_STRING("action", cmd.cmd.c_str());
    TEST_ASSERT_EQUAL_STRING("replay", cmd.action.c_str());
}

void test_malformed_json_is_invalid() {
    WsCommand cmd = parseWsCommand("not json");
    TEST_ASSERT_FALSE(cmd.valid);
}

void test_missing_module_is_invalid() {
    WsCommand cmd = parseWsCommand(R"({"cmd":"enable"})");
    TEST_ASSERT_FALSE(cmd.valid);
}

void test_unrecognized_cmd_is_invalid() {
    WsCommand cmd = parseWsCommand(R"({"module":"cc1101","cmd":"self_destruct"})");
    TEST_ASSERT_FALSE(cmd.valid);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_enable_command_parses);
    RUN_TEST(test_action_command_parses_with_action_name);
    RUN_TEST(test_malformed_json_is_invalid);
    RUN_TEST(test_missing_module_is_invalid);
    RUN_TEST(test_unrecognized_cmd_is_invalid);
    return UNITY_END();
}
