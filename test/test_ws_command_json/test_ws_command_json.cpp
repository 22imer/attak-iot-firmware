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

// A real no-param action accepts omitted params and {}, but rejects an explicit
// null, a scalar or an unknown key — all still correlated to the client.
void test_no_param_action_params_member_rules() {
    WsCommand omitted = parseWsCommand(R"({"id":1,"module":"wifi","cmd":"action","action":"scan"})");
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None), static_cast<int>(omitted.error));
    TEST_ASSERT_EQUAL_UINT32(0, omitted.params.size());

    WsCommand empty = parseWsCommand(R"({"id":2,"module":"wifi","cmd":"action","action":"scan","params":{}})");
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None), static_cast<int>(empty.error));

    WsCommand nullParams =
        parseWsCommand(R"({"id":3,"module":"wifi","cmd":"action","action":"scan","params":null})");
    TEST_ASSERT_TRUE(nullParams.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidParams), static_cast<int>(nullParams.error));

    WsCommand scalar =
        parseWsCommand(R"({"id":4,"module":"wifi","cmd":"action","action":"scan","params":5})");
    TEST_ASSERT_TRUE(scalar.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidParams), static_cast<int>(scalar.error));

    WsCommand unknownKey =
        parseWsCommand(R"({"id":5,"module":"wifi","cmd":"action","action":"scan","params":{"x":1}})");
    TEST_ASSERT_TRUE(unknownKey.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidParams), static_cast<int>(unknownKey.error));
}

// An unsupported action keeps its precedence over params validation, and an
// empty action name stays a schema (invalid_command) error before both.
void test_unsupported_action_precedence_over_params() {
    WsCommand unsupported =
        parseWsCommand(R"({"id":1,"module":"wifi","cmd":"action","action":"deauth","params":null})");
    TEST_ASSERT_TRUE(unsupported.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::UnsupportedAction), static_cast<int>(unsupported.error));

    WsCommand badAction =
        parseWsCommand(R"({"id":2,"module":"wifi","cmd":"action","action":"","params":null})");
    TEST_ASSERT_TRUE(badAction.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidCommand), static_cast<int>(badAction.error));
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

// A command with trailing bytes or JSON5 extensions must not correlate: the
// whole frame has to be one strict JSON value.
void test_trailing_garbage_and_extensions_are_invalid() {
    TEST_ASSERT_FALSE(parseWsCommand(R"({"id":1,"module":"cc1101","cmd":"enable"}x)").correlationValid);
    TEST_ASSERT_FALSE(
        parseWsCommand(R"({"id":1,"module":"cc1101","cmd":"enable"} {"id":2})").correlationValid);
    TEST_ASSERT_FALSE(
        parseWsCommand(R"({"id":1,"module":"cc1101","cmd":"enable"} //tail)").correlationValid);
    TEST_ASSERT_FALSE(parseWsCommand(R"({/*c*/"id":1,"module":"cc1101","cmd":"enable"})").correlationValid);
    TEST_ASSERT_FALSE(parseWsCommand(R"({'id':1,'module':'cc1101','cmd':'enable'})").correlationValid);
    TEST_ASSERT_FALSE(parseWsCommand(R"({"id":1,"module":"cc1101","cmd":"enable",})").correlationValid);
    TEST_ASSERT_FALSE(parseWsCommand(R"({"id":01,"module":"cc1101","cmd":"enable"})").correlationValid);

    // A strictly valid frame keeps correlation and error precedence unchanged.
    WsCommand unsupported = parseWsCommand(R"({"id":9,"module":"cc1101","cmd":"action","action":"scan"})");
    TEST_ASSERT_TRUE(unsupported.correlationValid);
    TEST_ASSERT_EQUAL_UINT32(9, unsupported.id);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::UnsupportedAction), static_cast<int>(unsupported.error));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_enable_command_parses);
    RUN_TEST(test_allowlisted_action_parses);
    RUN_TEST(test_unknown_action_is_not_malformed_command);
    RUN_TEST(test_cc1101_action_is_unsupported);
    RUN_TEST(test_no_param_action_params_member_rules);
    RUN_TEST(test_unsupported_action_precedence_over_params);
    RUN_TEST(test_malformed_json_is_invalid);
    RUN_TEST(test_missing_module_is_invalid);
    RUN_TEST(test_unrecognized_cmd_is_invalid);
    RUN_TEST(test_bad_id_is_invalid);
    RUN_TEST(test_unknown_module_is_invalid);
    RUN_TEST(test_trailing_garbage_and_extensions_are_invalid);
    RUN_TEST(test_result_ack_shape);
    return UNITY_END();
}
