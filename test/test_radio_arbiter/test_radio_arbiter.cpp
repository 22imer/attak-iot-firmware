#include <unity.h>

#include "core/radio_arbiter.h"

void setUp() {}
void tearDown() {}

using radio::Owner;
using radio::RadioArbiter;
using radio::RadioResource;
using radio::WifiPhase;

namespace {

constexpr RadioResource SPI = RadioResource::SharedSpi;
constexpr RadioResource WIFI = RadioResource::WifiExclusive;

} // namespace

// --- contention ------------------------------------------------------------

void test_shared_spi_has_one_owner_at_a_time() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, SPI, 0));
    TEST_ASSERT_FALSE(arb.tryAcquire(2, SPI, 0));
    TEST_ASSERT_EQUAL_UINT32(1, arb.ownerOf(SPI));
    TEST_ASSERT_TRUE(arb.holds(1, SPI));
    TEST_ASSERT_FALSE(arb.holds(2, SPI));
}

void test_shared_spi_not_blocked_for_its_own_owner() {
    RadioArbiter arb;
    TEST_ASSERT_FALSE(arb.spiBlockedFor(1)); // nothing held
    arb.tryAcquire(1, SPI, 0);
    TEST_ASSERT_FALSE(arb.spiBlockedFor(1));
    TEST_ASSERT_TRUE(arb.spiBlockedFor(2));
}

void test_wifi_cannot_be_reacquired_while_awaiting_suspend() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, WIFI, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiPhase::AwaitSuspend), static_cast<int>(arb.wifiPhase()));
    TEST_ASSERT_FALSE(arb.canAcquireWifi());
    TEST_ASSERT_FALSE(arb.tryAcquire(2, WIFI, 100));
    TEST_ASSERT_EQUAL_UINT32(1, arb.ownerOf(WIFI));
}

// --- independent resources -------------------------------------------------

void test_independent_resources_run_concurrently() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, SPI, 0));
    TEST_ASSERT_TRUE(arb.tryAcquire(2, WIFI, 100));
    TEST_ASSERT_TRUE(arb.holds(1, SPI));
    TEST_ASSERT_TRUE(arb.holds(2, WIFI));
    TEST_ASSERT_TRUE(arb.spiBlockedFor(2)); // wifi owner must not touch the shared bus
    TEST_ASSERT_FALSE(arb.spiBlockedFor(1));
}

void test_same_owner_may_hold_spi_and_wifi() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(5, SPI, 0));
    TEST_ASSERT_TRUE(arb.tryAcquire(5, WIFI, 0));
    TEST_ASSERT_TRUE(arb.holds(5, SPI));
    TEST_ASSERT_TRUE(arb.holds(5, WIFI));
}

// --- wrong-owner release ---------------------------------------------------

void test_wrong_owner_release_changes_nothing() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, SPI, 0));
    TEST_ASSERT_FALSE(arb.release(2));
    TEST_ASSERT_EQUAL_UINT32(1, arb.ownerOf(SPI));
    TEST_ASSERT_TRUE(arb.release(1));
    TEST_ASSERT_EQUAL_UINT32(radio::kNoOwner, arb.ownerOf(SPI));
    TEST_ASSERT_FALSE(arb.release(1)); // nothing left to release
}

void test_release_with_no_lease_reports_false() {
    RadioArbiter arb;
    TEST_ASSERT_FALSE(arb.release(9));
    TEST_ASSERT_FALSE(arb.release(radio::kNoOwner));
}

// --- duplicate acquisition must not extend the deadline --------------------

void test_duplicate_wifi_acquire_does_not_extend_deadline() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(7, WIFI, 1000));
    const uint32_t deadline = arb.wifiDeadline();
    TEST_ASSERT_EQUAL_UINT32(1000 + radio::kMaxWifiExclusiveMs, deadline);

    TEST_ASSERT_FALSE(arb.tryAcquire(7, WIFI, 5000)); // same owner
    TEST_ASSERT_FALSE(arb.tryAcquire(8, WIFI, 5000)); // other owner
    TEST_ASSERT_EQUAL_UINT32(deadline, arb.wifiDeadline());
    TEST_ASSERT_TRUE(arb.suspendDue(1000 + radio::kWifiSuspendGraceMs));

    arb.markSuspended();
    TEST_ASSERT_FALSE(arb.tryAcquire(7, WIFI, 20000));
    TEST_ASSERT_EQUAL_UINT32(deadline, arb.wifiDeadline());
    TEST_ASSERT_EQUAL_UINT32(7, arb.expiredOwner(deadline)); // still expires exactly once, on time
}

void test_duplicate_spi_acquire_is_rejected() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(4, SPI, 0));
    TEST_ASSERT_FALSE(arb.tryAcquire(4, SPI, 999));
    TEST_ASSERT_EQUAL_UINT32(4, arb.ownerOf(SPI));
}

// --- shutdown grace --------------------------------------------------------

void test_suspend_follows_grace_window() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, WIFI, 0));
    TEST_ASSERT_FALSE(arb.suspendDue(radio::kWifiSuspendGraceMs - 1));
    TEST_ASSERT_TRUE(arb.suspendDue(radio::kWifiSuspendGraceMs));
    TEST_ASSERT_TRUE(arb.markSuspended());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiPhase::Active), static_cast<int>(arb.wifiPhase()));
    TEST_ASSERT_FALSE(arb.suspendDue(100000)); // already suspended, grace is done
    TEST_ASSERT_FALSE(arb.markSuspended());    // not in AwaitSuspend
}

void test_suspend_signals_rejected_from_idle() {
    RadioArbiter arb;
    TEST_ASSERT_FALSE(arb.suspendDue(1000));
    TEST_ASSERT_FALSE(arb.markSuspended());
    TEST_ASSERT_FALSE(arb.markRestoring());
}

// --- hard deadline and wrap ------------------------------------------------

void test_hard_deadline_expires_at_exactly_the_limit() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, WIFI, 0));
    TEST_ASSERT_EQUAL_UINT32(radio::kNoOwner, arb.expiredOwner(radio::kMaxWifiExclusiveMs - 1));
    TEST_ASSERT_EQUAL_UINT32(1, arb.expiredOwner(radio::kMaxWifiExclusiveMs));
}

void test_hard_deadline_is_wrap_safe() {
    RadioArbiter arb;
    const uint32_t t0 = 0xFFFFFF00u; // 256 ms before the uint32_t clock wraps
    TEST_ASSERT_TRUE(arb.tryAcquire(3, WIFI, t0));

    const uint32_t graceAt = t0 + radio::kWifiSuspendGraceMs;
    TEST_ASSERT_FALSE(arb.suspendDue(graceAt - 1));
    TEST_ASSERT_TRUE(arb.suspendDue(graceAt));
    TEST_ASSERT_TRUE(arb.markSuspended());

    TEST_ASSERT_EQUAL_UINT32(radio::kNoOwner, arb.expiredOwner(t0));       // start
    TEST_ASSERT_EQUAL_UINT32(radio::kNoOwner, arb.expiredOwner(0x00000100u)); // after wrap, only +512 ms
    const uint32_t deadline = t0 + radio::kMaxWifiExclusiveMs;             // wrapped deadline
    TEST_ASSERT_TRUE(deadline < t0);
    TEST_ASSERT_EQUAL_UINT32(radio::kNoOwner, arb.expiredOwner(deadline - 1));
    TEST_ASSERT_EQUAL_UINT32(3, arb.expiredOwner(deadline));
}

void test_spi_lease_has_no_deadline() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, SPI, 0));
    TEST_ASSERT_EQUAL_UINT32(radio::kNoOwner, arb.expiredOwner(0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_UINT32(1, arb.ownerOf(SPI));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiPhase::Idle), static_cast<int>(arb.wifiPhase()));
}

// --- restoration locking and failure ---------------------------------------

void test_release_enters_restoring_and_locks_wifi() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, WIFI, 0));
    arb.markSuspended();

    TEST_ASSERT_TRUE(arb.release(1)); // cleanup done
    TEST_ASSERT_TRUE(arb.restorePending());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiPhase::Restoring), static_cast<int>(arb.wifiPhase()));
    TEST_ASSERT_FALSE(arb.canAcquireWifi());
    TEST_ASSERT_FALSE(arb.tryAcquire(2, WIFI, 100)); // no new WiFi while restoring
    TEST_ASSERT_EQUAL_UINT32(radio::kNoOwner, arb.expiredOwner(1000000));

    // The shared SPI bus stays independent of the WiFi restoration.
    TEST_ASSERT_TRUE(arb.tryAcquire(2, SPI, 100));
}

void test_restoration_errors_accumulate_and_keep_locking() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, WIFI, 0));
    arb.markSuspended();
    arb.release(1);

    TEST_ASSERT_TRUE(arb.restorationError());
    TEST_ASSERT_TRUE(arb.restorationError());
    TEST_ASSERT_EQUAL_UINT32(2, arb.restoreAttempts());
    TEST_ASSERT_TRUE(arb.restorePending());
    TEST_ASSERT_FALSE(arb.tryAcquire(2, WIFI, 10));
}

void test_restoration_complete_returns_wifi_to_idle() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, WIFI, 0));
    arb.markSuspended();
    arb.release(1);
    arb.restorationError();

    TEST_ASSERT_TRUE(arb.restorationComplete());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiPhase::Idle), static_cast<int>(arb.wifiPhase()));
    TEST_ASSERT_EQUAL_UINT32(0, arb.restoreAttempts());
    TEST_ASSERT_FALSE(arb.restorePending());
    TEST_ASSERT_TRUE(arb.canAcquireWifi());
    TEST_ASSERT_TRUE(arb.tryAcquire(2, WIFI, 500));
}

void test_mark_restoring_is_idempotent_and_clears_owner() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, WIFI, 0));
    TEST_ASSERT_TRUE(arb.markRestoring()); // cleanup signalled without release()
    TEST_ASSERT_EQUAL_UINT32(radio::kNoOwner, arb.ownerOf(WIFI));
    TEST_ASSERT_EQUAL_UINT32(0, arb.wifiDeadline());
    TEST_ASSERT_TRUE(arb.markRestoring()); // remains Restoring
    TEST_ASSERT_TRUE(arb.restorePending());
}

void test_restoration_signals_rejected_when_idle() {
    RadioArbiter arb;
    TEST_ASSERT_FALSE(arb.restorationComplete());
    TEST_ASSERT_FALSE(arb.restorationError());
    TEST_ASSERT_EQUAL_UINT32(0, arb.restoreAttempts());
    TEST_ASSERT_FALSE(arb.restorePending());
}

// --- misc admission --------------------------------------------------------

void test_invalid_owner_is_rejected() {
    RadioArbiter arb;
    TEST_ASSERT_FALSE(arb.tryAcquire(radio::kNoOwner, SPI, 0));
    TEST_ASSERT_FALSE(arb.tryAcquire(radio::kNoOwner, WIFI, 0));
    TEST_ASSERT_EQUAL_UINT32(radio::kNoOwner, arb.ownerOf(SPI));
}

void test_none_resource_needs_no_lease() {
    RadioArbiter arb;
    TEST_ASSERT_TRUE(arb.tryAcquire(1, RadioResource::None, 0));
    TEST_ASSERT_EQUAL_UINT32(radio::kNoOwner, arb.ownerOf(RadioResource::None));
    TEST_ASSERT_FALSE(arb.release(1));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_shared_spi_has_one_owner_at_a_time);
    RUN_TEST(test_shared_spi_not_blocked_for_its_own_owner);
    RUN_TEST(test_wifi_cannot_be_reacquired_while_awaiting_suspend);
    RUN_TEST(test_independent_resources_run_concurrently);
    RUN_TEST(test_same_owner_may_hold_spi_and_wifi);
    RUN_TEST(test_wrong_owner_release_changes_nothing);
    RUN_TEST(test_release_with_no_lease_reports_false);
    RUN_TEST(test_duplicate_wifi_acquire_does_not_extend_deadline);
    RUN_TEST(test_duplicate_spi_acquire_is_rejected);
    RUN_TEST(test_suspend_follows_grace_window);
    RUN_TEST(test_suspend_signals_rejected_from_idle);
    RUN_TEST(test_hard_deadline_expires_at_exactly_the_limit);
    RUN_TEST(test_hard_deadline_is_wrap_safe);
    RUN_TEST(test_spi_lease_has_no_deadline);
    RUN_TEST(test_release_enters_restoring_and_locks_wifi);
    RUN_TEST(test_restoration_errors_accumulate_and_keep_locking);
    RUN_TEST(test_restoration_complete_returns_wifi_to_idle);
    RUN_TEST(test_mark_restoring_is_idempotent_and_clears_owner);
    RUN_TEST(test_restoration_signals_rejected_when_idle);
    RUN_TEST(test_invalid_owner_is_rejected);
    RUN_TEST(test_none_resource_needs_no_lease);
    return UNITY_END();
}
