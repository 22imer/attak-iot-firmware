#include <unity.h>

#include <ArduinoJson.h>

#include "core/ws_command.h"

void setUp() {}
void tearDown() {}

void test_enable_command_parses() {
    WsCommand cmd = parseWsCommand(R"({"id":1,"module":"cc1101","cmd":"enable"})");

    TEST_ASSERT_TRUE(cmd.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None), static_cast<int>(cmd.error));
    TEST_ASSERT_EQUAL_UINT32(1, cmd.id);
    TEST_ASSERT_EQUAL(static_cast<int>(ModuleId::Cc1101), static_cast<int>(cmd.module));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::Enable), static_cast<int>(cmd.cmd));
}

void test_allowlisted_action_parses() {
    WsCommand cmd = parseWsCommand(R"({"id":17,"module":"wifi","cmd":"action","action":"scan"})");

    TEST_ASSERT_TRUE(cmd.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None), static_cast<int>(cmd.error));
    TEST_ASSERT_EQUAL(static_cast<int>(ModuleId::Wifi), static_cast<int>(cmd.module));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::Action), static_cast<int>(cmd.cmd));
    TEST_ASSERT_EQUAL(static_cast<int>(ActionId::Scan), static_cast<int>(cmd.action));
}

// A well-typed action outside the allowlist is unsupported, not malformed.
void test_unknown_action_is_not_malformed_command() {
    WsCommand unsupported = parseWsCommand(R"({"id":1,"module":"ir","cmd":"action","action":"replay"})");
    WsCommand malformed = parseWsCommand(R"({"id":2,"module":"ir","cmd":"action","action":""})");

    TEST_ASSERT_TRUE(unsupported.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::UnsupportedAction),
                      static_cast<int>(unsupported.error));
    // Empty action is a schema error, but id+module still correlate.
    TEST_ASSERT_TRUE(malformed.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidCommand), static_cast<int>(malformed.error));
}

void test_cc1101_action_is_unsupported() {
    WsCommand cmd = parseWsCommand(R"({"id":3,"module":"cc1101","cmd":"action","action":"record"})");
    TEST_ASSERT_TRUE(cmd.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::UnsupportedAction), static_cast<int>(cmd.error));
}

void test_malformed_json_is_invalid() {
    WsCommand cmd = parseWsCommand("not json");
    TEST_ASSERT_FALSE(cmd.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidCommand), static_cast<int>(cmd.error));
    TEST_ASSERT_EQUAL_UINT32(0, cmd.id);
}

void test_missing_module_is_invalid() {
    WsCommand cmd = parseWsCommand(R"({"id":1,"cmd":"enable"})");
    TEST_ASSERT_FALSE(cmd.correlationValid);
}

void test_unrecognized_cmd_is_invalid() {
    WsCommand cmd = parseWsCommand(R"({"id":1,"module":"cc1101","cmd":"self_destruct"})");
    TEST_ASSERT_TRUE(cmd.correlationValid); // id+module ok
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidCommand), static_cast<int>(cmd.error));
}

void test_bad_id_is_invalid() {
    TEST_ASSERT_FALSE(parseWsCommand(R"({"id":0,"module":"ir","cmd":"enable"})").correlationValid);
    TEST_ASSERT_FALSE(parseWsCommand(R"({"id":"7","module":"ir","cmd":"enable"})").correlationValid);
    TEST_ASSERT_FALSE(parseWsCommand(R"({"id":1.5,"module":"ir","cmd":"enable"})").correlationValid);
    TEST_ASSERT_FALSE(parseWsCommand(R"({"id":4294967296,"module":"ir","cmd":"enable"})").correlationValid);
    TEST_ASSERT_FALSE(parseWsCommand(R"({"module":"ir","cmd":"enable"})").correlationValid);
}

void test_unknown_module_is_invalid() {
    TEST_ASSERT_FALSE(parseWsCommand(R"({"id":1,"module":"ble","cmd":"enable"})").correlationValid);
}

void test_result_ack_shape() {
    std::string ok = commandResultToJson(17, ModuleId::Wifi, CommandError::None);
    JsonDocument okDoc;
    TEST_ASSERT_TRUE(deserializeJson(okDoc, ok) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("command_result", okDoc["type"].as<const char *>());
    TEST_ASSERT_EQUAL_UINT32(17, okDoc["id"].as<uint32_t>());
    TEST_ASSERT_EQUAL_STRING("wifi", okDoc["module"].as<const char *>());
    TEST_ASSERT_TRUE(okDoc["ok"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("", okDoc["error"].as<const char *>());

    std::string busy = commandResultToJson(19, ModuleId::Wifi, CommandError::Busy);
    JsonDocument busyDoc;
    TEST_ASSERT_TRUE(deserializeJson(busyDoc, busy) == DeserializationError::Ok);
    TEST_ASSERT_FALSE(busyDoc["ok"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("busy", busyDoc["error"].as<const char *>());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_enable_command_parses);
    RUN_TEST(test_allowlisted_action_parses);
    RUN_TEST(test_unknown_action_is_not_malformed_command);
    RUN_TEST(test_cc1101_action_is_unsupported);
    RUN_TEST(test_malformed_json_is_invalid);
    RUN_TEST(test_missing_module_is_invalid);
    RUN_TEST(test_unrecognized_cmd_is_invalid);
    RUN_TEST(test_bad_id_is_invalid);
    RUN_TEST(test_unknown_module_is_invalid);
    RUN_TEST(test_result_ack_shape);
    return UNITY_END();
}
