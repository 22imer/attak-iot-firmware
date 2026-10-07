// Native tests for evilTwin::Plan — the wifi_evil_twin scenario orchestrator
// (PLAN §13). Separate suite from test_evil_twin (which covers the portal
// helpers parseFormCredentials/buildDeauthPlan). Channel bounds follow
// evilTwin::channelUsable (1..13).
#include <unity.h>

#include <cstring>

#include "core/evil_twin.h"

void setUp() {}
void tearDown() {}

namespace {

evilTwin::Config makeConfig() {
    evilTwin::Config cfg{};
    const char *ssid = "VictimNet";
    cfg.ssidLen = std::strlen(ssid);
    std::memcpy(cfg.ssid, ssid, cfg.ssidLen + 1);
    for (int i = 0; i < 6; ++i) cfg.bssid[i] = static_cast<uint8_t>(0x10 + i);
    cfg.channel = 6;
    cfg.deauthReason = 7;
    cfg.deauthIntervalMs = 100;
    return cfg;
}

} // namespace

// --- Validate bounds -------------------------------------------------------

void test_begin_rejects_empty_ssid() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    cfg.ssidLen = 0;
    TEST_ASSERT_FALSE(plan.begin(cfg, 1000));
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::Idle);
}

void test_begin_rejects_oversized_ssid() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    cfg.ssidLen = 33; // > 32
    TEST_ASSERT_FALSE(plan.begin(cfg, 1000));
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::Idle);
}

void test_begin_accepts_max_ssid() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    for (int i = 0; i < 32; ++i) cfg.ssid[i] = 'A';
    cfg.ssid[32] = '\0';
    cfg.ssidLen = 32;
    TEST_ASSERT_TRUE(plan.begin(cfg, 1000));
    TEST_ASSERT_EQUAL_size_t(32, plan.config().ssidLen);
    TEST_ASSERT_EQUAL_CHAR('\0', plan.config().ssid[32]);
}

void test_begin_rejects_channel_zero() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    cfg.channel = 0;
    TEST_ASSERT_FALSE(plan.begin(cfg, 0));
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::Idle);
}

void test_begin_rejects_channel_fourteen() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    cfg.channel = 14; // channelUsable caps at 13
    TEST_ASSERT_FALSE(plan.begin(cfg, 0));
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::Idle);
}

void test_begin_accepts_channel_bounds() {
    evilTwin::Plan planLow;
    evilTwin::Config low = makeConfig();
    low.channel = 1;
    TEST_ASSERT_TRUE(planLow.begin(low, 0));

    evilTwin::Plan planHigh;
    evilTwin::Config high = makeConfig();
    high.channel = 13;
    TEST_ASSERT_TRUE(planHigh.begin(high, 0));
}

void test_begin_clamps_reason_below_min() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    cfg.deauthReason = 0; // < 1 -> 1
    TEST_ASSERT_TRUE(plan.begin(cfg, 0));
    TEST_ASSERT_EQUAL_UINT16(1, plan.config().deauthReason);
}

void test_begin_preserves_valid_reason() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    cfg.deauthReason = 60000;
    TEST_ASSERT_TRUE(plan.begin(cfg, 0));
    TEST_ASSERT_EQUAL_UINT16(60000, plan.config().deauthReason);
}

void test_begin_clamps_interval_below_min() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    cfg.deauthIntervalMs = 10; // < 20 -> 20
    TEST_ASSERT_TRUE(plan.begin(cfg, 0));
    TEST_ASSERT_EQUAL_UINT32(20, plan.config().deauthIntervalMs);
}

void test_begin_clamps_interval_above_max() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    cfg.deauthIntervalMs = 6000; // > 5000 -> 5000
    TEST_ASSERT_TRUE(plan.begin(cfg, 0));
    TEST_ASSERT_EQUAL_UINT32(5000, plan.config().deauthIntervalMs);
}

void test_begin_preserves_config_fields() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    TEST_ASSERT_TRUE(plan.begin(cfg, 0));
    TEST_ASSERT_EQUAL_UINT8(6, plan.config().channel);
    TEST_ASSERT_EQUAL_MEMORY(cfg.bssid, plan.config().bssid, 6);
    TEST_ASSERT_EQUAL_STRING("VictimNet", plan.config().ssid);
}

// --- Phase transitions -----------------------------------------------------

void test_phase_flow_idle_to_running() {
    evilTwin::Plan plan;
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::Idle);
    TEST_ASSERT_TRUE(plan.begin(makeConfig(), 1000));
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::CloningAp);
    plan.apReady(1500);
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::Running);
    plan.stop(2000);
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::Stopping);
}

void test_apready_ignored_when_not_cloning() {
    evilTwin::Plan plan;
    plan.apReady(1000); // Idle -> no-op
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::Idle);
    TEST_ASSERT_TRUE(plan.begin(makeConfig(), 0));
    plan.stop(10);
    plan.apReady(20); // Stopping -> no-op
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::Stopping);
}

void test_stop_is_idempotent() {
    evilTwin::Plan plan;
    TEST_ASSERT_TRUE(plan.begin(makeConfig(), 0));
    plan.stop(100);
    plan.stop(200);
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::Stopping);
}

void test_fail_sets_failed_phase() {
    evilTwin::Plan plan;
    TEST_ASSERT_TRUE(plan.begin(makeConfig(), 0));
    plan.apReady(10);
    plan.fail(50);
    TEST_ASSERT_TRUE(plan.phase() == evilTwin::Phase::Failed);
    TEST_ASSERT_TRUE(plan.step(1000) == evilTwin::Step::None);
}

// --- StartTwinAp cadence ---------------------------------------------------

void test_start_twin_ap_emitted_once() {
    evilTwin::Plan plan;
    TEST_ASSERT_TRUE(plan.begin(makeConfig(), 1000));
    TEST_ASSERT_TRUE(plan.step(1000) == evilTwin::Step::StartTwinAp);
    TEST_ASSERT_TRUE(plan.step(1001) == evilTwin::Step::None);
    TEST_ASSERT_TRUE(plan.step(2000) == evilTwin::Step::None);
    TEST_ASSERT_EQUAL_UINT32(0, plan.deauthBursts());
}

// --- SendDeauth cadence ----------------------------------------------------

void test_send_deauth_cadence_and_bursts() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    cfg.deauthIntervalMs = 100;
    TEST_ASSERT_TRUE(plan.begin(cfg, 1000));
    TEST_ASSERT_TRUE(plan.step(1000) == evilTwin::Step::StartTwinAp);
    plan.apReady(1000);
    TEST_ASSERT_TRUE(plan.step(1000) == evilTwin::Step::SendDeauth);
    TEST_ASSERT_EQUAL_UINT32(1, plan.deauthBursts());
    TEST_ASSERT_TRUE(plan.step(1099) == evilTwin::Step::None);
    TEST_ASSERT_EQUAL_UINT32(1, plan.deauthBursts());
    TEST_ASSERT_TRUE(plan.step(1100) == evilTwin::Step::SendDeauth);
    TEST_ASSERT_EQUAL_UINT32(2, plan.deauthBursts());
    TEST_ASSERT_TRUE(plan.step(1150) == evilTwin::Step::None);
    TEST_ASSERT_TRUE(plan.step(1200) == evilTwin::Step::SendDeauth);
    TEST_ASSERT_EQUAL_UINT32(3, plan.deauthBursts());
}

void test_send_deauth_wrap_safe_around_overflow() {
    evilTwin::Plan plan;
    evilTwin::Config cfg = makeConfig();
    cfg.deauthIntervalMs = 100;
    TEST_ASSERT_TRUE(plan.begin(cfg, 0xFFFFFFF0u));
    plan.step(0xFFFFFFF0u); // StartTwinAp
    plan.apReady(0xFFFFFFF0u);
    TEST_ASSERT_TRUE(plan.step(0xFFFFFFF0u) == evilTwin::Step::SendDeauth);
    TEST_ASSERT_EQUAL_UINT32(1, plan.deauthBursts());
    TEST_ASSERT_TRUE(plan.step(0x00000040u) == evilTwin::Step::None); // 0x40 < 0x54 deadline
    TEST_ASSERT_EQUAL_UINT32(1, plan.deauthBursts());
    TEST_ASSERT_TRUE(plan.step(0x00000060u) == evilTwin::Step::SendDeauth); // past wrapped deadline
    TEST_ASSERT_EQUAL_UINT32(2, plan.deauthBursts());
}

void test_no_deauth_before_ap_ready() {
    evilTwin::Plan plan;
    TEST_ASSERT_TRUE(plan.begin(makeConfig(), 0));
    plan.step(0);
    for (uint32_t t = 100; t <= 5000; t += 500) {
        TEST_ASSERT_TRUE(plan.step(t) == evilTwin::Step::None);
    }
    TEST_ASSERT_EQUAL_UINT32(0, plan.deauthBursts());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_rejects_empty_ssid);
    RUN_TEST(test_begin_rejects_oversized_ssid);
    RUN_TEST(test_begin_accepts_max_ssid);
    RUN_TEST(test_begin_rejects_channel_zero);
    RUN_TEST(test_begin_rejects_channel_fourteen);
    RUN_TEST(test_begin_accepts_channel_bounds);
    RUN_TEST(test_begin_clamps_reason_below_min);
    RUN_TEST(test_begin_preserves_valid_reason);
    RUN_TEST(test_begin_clamps_interval_below_min);
    RUN_TEST(test_begin_clamps_interval_above_max);
    RUN_TEST(test_begin_preserves_config_fields);
    RUN_TEST(test_phase_flow_idle_to_running);
    RUN_TEST(test_apready_ignored_when_not_cloning);
    RUN_TEST(test_stop_is_idempotent);
    RUN_TEST(test_fail_sets_failed_phase);
    RUN_TEST(test_start_twin_ap_emitted_once);
    RUN_TEST(test_send_deauth_cadence_and_bursts);
    RUN_TEST(test_send_deauth_wrap_safe_around_overflow);
    RUN_TEST(test_no_deauth_before_ap_ready);
    return UNITY_END();
}
