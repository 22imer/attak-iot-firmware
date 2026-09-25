#include <unity.h>

#include "core/storage.h"

void setUp() {}
void tearDown() {}

void test_round_trip_preserves_values() {
    DeviceConfig original;
    original.apSsid = "MyNet";
    original.apPassword = "hunter2";
    original.deviceName = "unit-01";

    DeviceConfig decoded = storage::fromJson(storage::toJson(original));

    TEST_ASSERT_EQUAL_STRING(original.apSsid.c_str(), decoded.apSsid.c_str());
    TEST_ASSERT_EQUAL_STRING(original.apPassword.c_str(), decoded.apPassword.c_str());
    TEST_ASSERT_EQUAL_STRING(original.deviceName.c_str(), decoded.deviceName.c_str());
}

void test_missing_json_falls_back_to_defaults() {
    DeviceConfig defaults;
    DeviceConfig decoded = storage::fromJson("");

    TEST_ASSERT_EQUAL_STRING(defaults.apSsid.c_str(), decoded.apSsid.c_str());
    TEST_ASSERT_EQUAL_STRING(defaults.apPassword.c_str(), decoded.apPassword.c_str());
    TEST_ASSERT_EQUAL_STRING(defaults.deviceName.c_str(), decoded.deviceName.c_str());
}

void test_partial_json_falls_back_for_missing_fields_only() {
    DeviceConfig defaults;
    DeviceConfig decoded = storage::fromJson(R"({"apSsid":"OnlySsid"})");

    TEST_ASSERT_EQUAL_STRING("OnlySsid", decoded.apSsid.c_str());
    TEST_ASSERT_EQUAL_STRING(defaults.apPassword.c_str(), decoded.apPassword.c_str());
    TEST_ASSERT_EQUAL_STRING(defaults.deviceName.c_str(), decoded.deviceName.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_preserves_values);
    RUN_TEST(test_missing_json_falls_back_to_defaults);
    RUN_TEST(test_partial_json_falls_back_for_missing_fields_only);
    return UNITY_END();
}
