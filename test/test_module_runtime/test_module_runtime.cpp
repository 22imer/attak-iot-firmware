#include <unity.h>

#include "core/module_runtime.h"

void setUp() {}
void tearDown() {}

// Timeout must keep the previous successful payload and its metadata, and only
// change the action outcome.
void test_timeout_preserves_previous_result() {
    ModuleRuntime runtime("pn532");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);

    uint32_t first = 0, second = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(10, 5000, first)));
    TEST_ASSERT_TRUE(runtime.completeAction(first, R"({"kind":"nfc_uid","uid":"04:AB:01:02"})", 20));

    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(30, 5000, second)));
    TEST_ASSERT_FALSE(runtime.expire(5029, ActionError::ReadTimeout, false));
    TEST_ASSERT_TRUE(runtime.expire(5030, ActionError::ReadTimeout, false));

    TEST_ASSERT_EQUAL_STRING(R"({"kind":"nfc_uid","uid":"04:AB:01:02"})", runtime.status().output.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, runtime.status().resultSequence);
    TEST_ASSERT_EQUAL_UINT32(20, runtime.status().resultUpdateMs);
    TEST_ASSERT_EQUAL(static_cast<int>(ActionState::Timeout), static_cast<int>(runtime.status().actionState));
    TEST_ASSERT_EQUAL(static_cast<int>(ActionError::ReadTimeout),
                      static_cast<int>(runtime.status().actionError));
}

// Disable invalidates the ticket and keeps cleanup; re-enable must not clear
// the cleanup flag, and the old ticket's completion must never land.
void test_cancel_reenable_rejects_old_completion_and_keeps_cleanup() {
    ModuleRuntime runtime("wifi");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);

    uint32_t oldTicket = 0, newTicket = 0;
    runtime.beginAction(3, 15000, oldTicket);

    runtime.setEnabled(false, 4, true);
    runtime.setEnabled(true, 5);
    runtime.setHealth(true, "ready", 6);

    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::Busy),
                      static_cast<int>(runtime.beginAction(7, 15000, newTicket)));
    TEST_ASSERT_FALSE(runtime.completeAction(oldTicket, "old", 8));
    TEST_ASSERT_TRUE(runtime.status().output.empty());

    runtime.finishCleanup(9);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(10, 15000, newTicket)));
    TEST_ASSERT_FALSE(runtime.completeAction(oldTicket, "late", 11));
    TEST_ASSERT_TRUE(runtime.completeAction(newTicket, "new", 12));
    TEST_ASSERT_EQUAL_STRING("new", runtime.status().output.c_str());
}

// Deadlines are wrap-safe durations; identical payloads still advance the
// sequence and result time.
void test_deadline_wrap_and_identical_results() {
    ModuleRuntime runtime("ir");
    runtime.setEnabled(true, 0);
    runtime.setHealth(true, "ready", 0);

    uint32_t ticket = 0;
    runtime.beginAction(0xfffffff0u, 32, ticket);
    TEST_ASSERT_FALSE(runtime.expire(0x0fu, ActionError::CaptureTimeout, false));
    TEST_ASSERT_TRUE(runtime.expire(0x10u, ActionError::CaptureTimeout, false));

    runtime.beginAction(20, 100, ticket);
    runtime.completeAction(ticket, "same", 21);
    runtime.beginAction(22, 100, ticket);
    runtime.completeAction(ticket, "same", 23);

    TEST_ASSERT_EQUAL_UINT32(2, runtime.status().resultSequence);
    TEST_ASSERT_EQUAL_UINT32(23, runtime.status().resultUpdateMs);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_timeout_preserves_previous_result);
    RUN_TEST(test_cancel_reenable_rejects_old_completion_and_keeps_cleanup);
    RUN_TEST(test_deadline_wrap_and_identical_results);
    return UNITY_END();
}
