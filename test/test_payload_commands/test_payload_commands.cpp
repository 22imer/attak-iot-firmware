#include <unity.h>
#include <cstring>
#include "core/ws_command.h"

void setUp() {}
void tearDown() {}

void test_rf_required_frequency_and_numeric_boundaries() {
    const auto valid = parseWsCommand(R"({"id":1,"module":"cc1101","cmd":"action","action":"rf_scan","params":{"startMhz":433.05,"endMhz":434.79,"stepKhz":25}})");
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None), static_cast<int>(valid.error));
    const char *invalid[] = {
        R"({"startMhz":433.05,"stepKhz":25})",
        R"({"startMhz":"433.05","endMhz":434.79,"stepKhz":25})",
        R"({"startMhz":299.99,"endMhz":434.79,"stepKhz":25})",
        R"({"startMhz":433.05,"endMhz":928.01,"stepKhz":25})",
        R"({"startMhz":433.05,"endMhz":434.79,"stepKhz":25.5})",
        R"({"startMhz":433.05,"endMhz":434.79,"stepKhz":9})",
    };
    for (const char *params : invalid) {
        const std::string json = std::string(R"({"id":2,"module":"cc1101","cmd":"action","action":"rf_scan","params":)") + params + "}";
        const auto rejected = parseWsCommand(json);
        TEST_ASSERT_TRUE(rejected.correlationValid);
        TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidParams), static_cast<int>(rejected.error));
    }
}

void test_ndef_utf8_bytes_are_owned_and_bounded() {
    std::string text;
    for (int i = 0; i < 32; ++i) text += "é";
    std::string json = std::string(R"({"id":3,"module":"pn532","cmd":"action","action":"nfc_write_ndef","params":{"text":")") + text + R"("}})";
    const auto valid = parseWsCommand(json);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None), static_cast<int>(valid.error));
    json.assign(100, 'x'); // queued value must survive request storage reuse
    TEST_ASSERT_EQUAL_UINT32(64, valid.params.stringLength(0));
    TEST_ASSERT_EQUAL_MEMORY(text.data(), valid.params.string(0), text.size());
    text += "é";
    json = std::string(R"({"id":4,"module":"pn532","cmd":"action","action":"nfc_write_ndef","params":{"text":")") + text + R"("}})";
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidParams), static_cast<int>(parseWsCommand(json).error));
}

void test_radio_params_reject_wrong_types_and_settle_underflow() {
    const char *invalid[] = {
        R"({"id":5,"module":"nrf24","cmd":"action","action":"nrf_scan","params":{"dwellUs":149}})",
        R"({"id":6,"module":"nrf24","cmd":"action","action":"nrf_scan","params":{"endChannel":126}})",
        R"({"id":7,"module":"wifi","cmd":"action","action":"wifi_sniff","params":{"hop":1}})",
        R"({"id":8,"module":"wifi","cmd":"action","action":"wifi_sniff","params":{"channel":14}})",
    };
    for (const auto *json : invalid) {
        const auto cmd = parseWsCommand(json);
        TEST_ASSERT_TRUE(cmd.correlationValid);
        TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidParams), static_cast<int>(cmd.error));
    }
}

void test_ir_params_reject_wrong_types_and_out_of_bounds() {
    const char *invalid[] = {
        R"({"protocol":true})",
        R"({"protocol":"NEC","code":18446744073709551615,"bits":32})",
        R"({"protocol":"NEC","code":"FFFFFFFF","bits":65})",
        R"({"protocol":"RAW","raw":"9000,4500,560","frequency":29999})",
        R"({"protocol":"RAW","raw":null})",
        R"({"protocol":"NEC","code":"FFFFFFFF","unexpected":1})",
    };
    for (const char *params : invalid) {
        const std::string json = std::string(R"({"id":9,"module":"ir","cmd":"action","action":"ir_custom_tx","params":)") + params + "}";
        const auto rejected = parseWsCommand(json);
        TEST_ASSERT_TRUE(rejected.correlationValid);
        TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidParams), static_cast<int>(rejected.error));
    }
}

#ifdef ENABLE_DISRUPTIVE
void test_disruptive_params_validate() {
    // Schema-level rejections: wrong type, out-of-range, over-cap string and an
    // unknown key on a no-param action. Enum-valued strings (mode) are admitted
    // by the parser and rejected by the handler, so they are not tested here.
    const char *invalid[] = {
        R"({"id":20,"module":"cc1101","cmd":"action","action":"rf_jammer","params":{"mode":1}})",
        R"({"id":21,"module":"cc1101","cmd":"action","action":"rf_jammer","params":{"onMs":9}})",
        R"({"id":22,"module":"cc1101","cmd":"action","action":"rf_jammer","params":{"freqMhz":200}})",
        R"({"id":23,"module":"nrf24","cmd":"action","action":"nrf_jammer","params":{"endChannel":126}})",
        R"({"id":24,"module":"nrf24","cmd":"action","action":"nrf_jammer","params":{"dwellMs":19}})",
        R"({"id":25,"module":"wifi","cmd":"action","action":"wifi_beacon","params":{"intervalMs":10}})",
        R"({"id":27,"module":"wifi","cmd":"action","action":"wifi_deauth","params":{"reason":0}})",
        R"({"id":28,"module":"wifi","cmd":"action","action":"wifi_deauth","params":{"bssid":12345}})",
        R"({"id":29,"module":"wifi","cmd":"action","action":"wifi_evil_portal","params":{"x":1}})",
    };
    for (const auto *json : invalid) {
        const auto cmd = parseWsCommand(json);
        TEST_ASSERT_TRUE(cmd.correlationValid);
        TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidParams), static_cast<int>(cmd.error));
    }

    // Valid shapes: enumerated mode string, a well-formed target and no-param portal.
    const auto jammer = parseWsCommand(
        R"({"id":30,"module":"cc1101","cmd":"action","action":"rf_jammer","params":{"mode":"intermittent","onMs":250,"offMs":250}})");
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None), static_cast<int>(jammer.error));
    const auto deauth = parseWsCommand(
        R"({"id":31,"module":"wifi","cmd":"action","action":"wifi_deauth","params":{"mode":"target","bssid":"AA:BB:CC:DD:EE:FF","reason":7}})");
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None), static_cast<int>(deauth.error));
    const auto portal = parseWsCommand(R"({"id":32,"module":"wifi","cmd":"action","action":"wifi_evil_portal"})");
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None), static_cast<int>(portal.error));
}

void test_disruptive_params_over_cap_ssid_rejected() {
    std::string ssid(33, 'A');
    const std::string json = std::string(R"({"id":33,"module":"wifi","cmd":"action","action":"wifi_beacon","params":{"ssid":")") +
                             ssid + R"("}})";
    const auto rejected = parseWsCommand(json);
    TEST_ASSERT_TRUE(rejected.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidParams), static_cast<int>(rejected.error));
}
#endif

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_rf_required_frequency_and_numeric_boundaries);
    RUN_TEST(test_ndef_utf8_bytes_are_owned_and_bounded);
    RUN_TEST(test_radio_params_reject_wrong_types_and_settle_underflow);
    RUN_TEST(test_ir_params_reject_wrong_types_and_out_of_bounds);
#ifdef ENABLE_DISRUPTIVE
    RUN_TEST(test_disruptive_params_validate);
    RUN_TEST(test_disruptive_params_over_cap_ssid_rejected);
#endif
    return UNITY_END();
}
