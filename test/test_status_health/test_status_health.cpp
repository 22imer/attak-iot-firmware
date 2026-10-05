#include <unity.h>

#include "core/module_status.h"
#include "core/status_health.h"

void setUp() {}
void tearDown() {}

namespace {

ModuleStatus make(const char *name, bool enabled, bool connected, ActionState state = ActionState::Idle,
                  ActionError error = ActionError::None) {
    return ModuleStatus{name, enabled, connected, enabled ? "ready" : "off", "", 0, state, error, false, 0, 0};
}

} // namespace

void test_all_off_is_off_even_with_stale_connected_flag() {
    ModuleStatus a = make("cc1101", false, true); // stale connected on a disabled module
    ModuleStatus b = make("nrf24", false, false);
    const ModuleStatus *modules[] = {&a, &b};
    TEST_ASSERT_EQUAL(static_cast<int>(HealthLevel::Off),
                      static_cast<int>(aggregateHealth(modules, 2)));
}

void test_enabled_healthy_with_others_off_is_healthy() {
    ModuleStatus enabled = make("wifi", true, true);
    ModuleStatus off = make("ir", false, false);
    const ModuleStatus *modules[] = {&enabled, &off};
    TEST_ASSERT_EQUAL(static_cast<int>(HealthLevel::Healthy),
                      static_cast<int>(aggregateHealth(modules, 2)));
}

void test_enabled_disconnected_is_degraded() {
    ModuleStatus enabled = make("wifi", true, true);
    ModuleStatus broken = make("cc1101", true, false);
    const ModuleStatus *modules[] = {&enabled, &broken};
    TEST_ASSERT_EQUAL(static_cast<int>(HealthLevel::Degraded),
                      static_cast<int>(aggregateHealth(modules, 2)));
}

void test_action_timeout_does_not_change_health() {
    ModuleStatus timedOut = make("pn532", true, true, ActionState::Timeout, ActionError::ReadTimeout);
    const ModuleStatus *modules[] = {&timedOut};
    TEST_ASSERT_EQUAL(static_cast<int>(HealthLevel::Healthy),
                      static_cast<int>(aggregateHealth(modules, 1)));
}

void test_wifi_error_degrades() {
    ModuleStatus wifi = make("wifi", true, false);
    ModuleStatus nfc = make("pn532", true, true);
    const ModuleStatus *modules[] = {&wifi, &nfc};
    TEST_ASSERT_EQUAL(static_cast<int>(HealthLevel::Degraded),
                      static_cast<int>(aggregateHealth(modules, 2)));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_all_off_is_off_even_with_stale_connected_flag);
    RUN_TEST(test_enabled_healthy_with_others_off_is_healthy);
    RUN_TEST(test_enabled_disconnected_is_degraded);
    RUN_TEST(test_action_timeout_does_not_change_health);
    RUN_TEST(test_wifi_error_degrades);
    return UNITY_END();
}
