#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "core/module_status.h"
#include "core/module_runtime.h"

void setUp() {}
void tearDown() {}

namespace {

ActionOutput sample(const char *payload) {
    ActionOutput output;
    output.module = "wifi";
    output.action = "monitor";
    output.ticket = 4;
    output.sequence = 2;
    output.uptimeMs = 1234;
    output.payload = payload;
    return output;
}

} // namespace

// The payload is an embedded JSON value, not an escaped opaque string: a client
// can read fields directly, and the raw frame never shows the nested quotes as
// a string literal.
void test_payload_is_embedded_json_value() {
    const std::string json = actionOutputToJson(sample(R"({"rssi":-42,"ssid":"lab"})"));
    TEST_ASSERT_FALSE(json.empty());

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("action_output", doc["type"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("wifi", doc["module"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("monitor", doc["action"].as<const char *>());
    TEST_ASSERT_EQUAL_UINT32(4, doc["ticket"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(2, doc["sequence"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(1234, doc["uptimeMs"].as<uint32_t>());
    TEST_ASSERT_TRUE(doc["payload"].is<JsonObject>());
    TEST_ASSERT_EQUAL_INT(-42, doc["payload"]["rssi"].as<int>());
    TEST_ASSERT_EQUAL_STRING("lab", doc["payload"]["ssid"].as<const char *>());

    TEST_ASSERT_TRUE(json.find(R"("payload":{)") != std::string::npos);
    TEST_ASSERT_TRUE(json.find(R"("payload":"{\")") == std::string::npos);
}

// Scalar and empty-object payloads are valid JSON values and must survive.
void test_scalar_and_empty_object_payloads() {
    JsonDocument scalarDoc;
    TEST_ASSERT_TRUE(deserializeJson(scalarDoc, actionOutputToJson(sample("17"))) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_INT(17, scalarDoc["payload"].as<int>());

    JsonDocument emptyDoc;
    TEST_ASSERT_TRUE(deserializeJson(emptyDoc, actionOutputToJson(sample("{}"))) == DeserializationError::Ok);
    TEST_ASSERT_TRUE(emptyDoc["payload"].is<JsonObject>());
    TEST_ASSERT_EQUAL_UINT32(0, emptyDoc["payload"].as<JsonObjectConst>().size());
}

// A malformed producer payload is dropped, never broadcast as opaque text.
void test_malformed_payload_is_rejected() {
    TEST_ASSERT_TRUE(actionOutputToJson(sample("not json")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample("")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"unterminated":)")).empty());
}

// Null module/action pointers must not reach the client as a crash or `null`
// string; the frame stays valid.
void test_null_names_are_tolerated() {
    ActionOutput output = sample("{}");
    output.module = nullptr;
    output.action = nullptr;
    const std::string json = actionOutputToJson(output);
    TEST_ASSERT_FALSE(json.empty());

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("", doc["module"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("", doc["action"].as<const char *>());
}

// A complete value followed by any extra bytes is not one frame: the whole
// buffer must be consumed. ArduinoJson's deserializeJson() alone would accept
// the valid prefix and silently drop the tail.
void test_trailing_content_is_rejected() {
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"a":1}x)")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"a":1} {"b":2})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"a":1} x)")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample("[1,2]]")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample("17 17")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"("a"b)")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample("truefalse")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample("null null")).empty());
}

// Non-RFC extensions that ArduinoJson accepts by default: comments, single
// quotes (keys and values), unquoted keys, trailing commas, missing commas.
void test_non_rfc_extensions_are_rejected() {
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"a":1}//tail)")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"(/*head*/{"a":1})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({/*mid*/"a":1})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample("[1,/*mid*/2]")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample("{'a':1}")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"a":'b'})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample("{a:1}")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"a":1,})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample("[1,]")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample("[1 2]")).empty());
}

// Only the RFC 8259 number grammar and the seven string escapes are allowed.
void test_malformed_numbers_and_escapes_are_rejected() {
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"n":01})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"n":.5})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"n":1.})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"n":+1})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"n":1e})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"n":0x10})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"n":NaN})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"n":Infinity})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"n":-})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"s":"\x"})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"s":"\u12"})")).empty());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(R"({"s":"\uZZZZ"})")).empty());
    const std::string rawNewline = "{\"s\":\"a\nb\"}"; // unescaped control byte
    TEST_ASSERT_TRUE(actionOutputToJson(sample(rawNewline.c_str())).empty());
}

// Valid Unicode (escaped or raw UTF-8) and every valid JSON root survive.
void test_unicode_and_primitive_roots_survive() {
    JsonDocument escaped;
    TEST_ASSERT_TRUE(deserializeJson(escaped, actionOutputToJson(sample(R"({"t":"\u00e9"})"))) ==
                     DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("\xc3\xa9", escaped["payload"]["t"].as<const char *>());

    JsonDocument raw;
    TEST_ASSERT_TRUE(deserializeJson(raw, actionOutputToJson(sample(R"({"t":"é","e":"😀"})"))) ==
                     DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("é", raw["payload"]["t"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("😀", raw["payload"]["e"].as<const char *>());

    JsonDocument surrogate;
    TEST_ASSERT_TRUE(deserializeJson(surrogate, actionOutputToJson(sample(R"({"p":"\uD83D\uDE00"})"))) ==
                     DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("😀", surrogate["payload"]["p"].as<const char *>());

    // Contract says payload is a <validated JSON value>, primitives included.
    TEST_ASSERT_FALSE(actionOutputToJson(sample("17")).empty());
    TEST_ASSERT_FALSE(actionOutputToJson(sample("-12.5e+3")).empty());
    TEST_ASSERT_FALSE(actionOutputToJson(sample("true")).empty());
    TEST_ASSERT_FALSE(actionOutputToJson(sample("null")).empty());
    TEST_ASSERT_FALSE(actionOutputToJson(sample(R"("text")")).empty());

    // JSON whitespace around the value is legal; nothing else is.
    TEST_ASSERT_FALSE(actionOutputToJson(sample(" \t\r\n{\"a\":1} \n")).empty());
}

// The stream ceiling still applies: one byte over 1024 is dropped, exactly 1024
// is serialized.
void test_oversized_payload_is_rejected() {
    const std::string atLimit = R"({"d":")" + std::string(kMaxActionOutputBytes - 8, 'a') + R"("})";
    TEST_ASSERT_EQUAL_UINT32(kMaxActionOutputBytes, atLimit.size());
    TEST_ASSERT_FALSE(actionOutputToJson(sample(atLimit.c_str())).empty());

    const std::string overLimit = R"({"d":")" + std::string(kMaxActionOutputBytes - 7, 'a') + R"("})";
    TEST_ASSERT_EQUAL_UINT32(kMaxActionOutputBytes + 1, overLimit.size());
    TEST_ASSERT_TRUE(actionOutputToJson(sample(overLimit.c_str())).empty());
}

// End-to-end stream path: the runtime admits a sample by ticket/cadence/size,
// and the serializer is the strict gate that discards a malformed frame while a
// valid one keeps the stream metadata (ticket/action/sequence).
void test_stream_frame_discards_invalid_payload() {
    ModuleRuntime runtime("wifi");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);

    const ActionDescriptor descriptor{"monitor", "Monitor", ActionId::Scan, ActionKind::Continuous,
                                      LegalTier::Observe, false, false};
    uint32_t ticket = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(100, 0, ticket, descriptor)));

    TEST_ASSERT_TRUE(runtime.publishOutput(ticket, R"({"rssi":-40}garbage)", 100));
    ActionOutput bad;
    TEST_ASSERT_TRUE(runtime.takeActionOutput(bad));
    TEST_ASSERT_TRUE(actionOutputToJson(bad).empty()); // dropped, never broadcast

    TEST_ASSERT_TRUE(runtime.publishOutput(ticket, R"({"rssi":-41})", 200));
    ActionOutput good;
    TEST_ASSERT_TRUE(runtime.takeActionOutput(good));
    const std::string json = actionOutputToJson(good);
    TEST_ASSERT_FALSE(json.empty());

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("action_output", doc["type"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("wifi", doc["module"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("monitor", doc["action"].as<const char *>());
    TEST_ASSERT_EQUAL_UINT32(ticket, doc["ticket"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(2, doc["sequence"].as<uint32_t>());
    TEST_ASSERT_EQUAL_INT(-41, doc["payload"]["rssi"].as<int>());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_payload_is_embedded_json_value);
    RUN_TEST(test_scalar_and_empty_object_payloads);
    RUN_TEST(test_malformed_payload_is_rejected);
    RUN_TEST(test_null_names_are_tolerated);
    RUN_TEST(test_trailing_content_is_rejected);
    RUN_TEST(test_non_rfc_extensions_are_rejected);
    RUN_TEST(test_malformed_numbers_and_escapes_are_rejected);
    RUN_TEST(test_unicode_and_primitive_roots_survive);
    RUN_TEST(test_oversized_payload_is_rejected);
    RUN_TEST(test_stream_frame_discards_invalid_payload);
    return UNITY_END();
}
