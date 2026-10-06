#include <unity.h>

#include "core/action_catalog.h"
#include "core/module_runtime.h"

void setUp() {}
void tearDown() {}

namespace {

// Minimal descriptor for a runtime test: only kind/needsBuffer/id drive the
// state machine. The real catalog supplies the rest in production.
ActionDescriptor descriptor(const char *id, ActionKind kind, bool needsBuffer = false) {
    return ActionDescriptor{id, id, ActionId::None, kind, LegalTier::Observe, false, needsBuffer};
}

} // namespace

// Timeout must keep the previous successful payload and its metadata, and only
// change the action outcome.
void test_timeout_preserves_previous_result() {
    ModuleRuntime runtime("pn532");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);

    uint32_t first = 0, second = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(10, 5000, first, descriptor("read_uid", ActionKind::OneShot))));
    TEST_ASSERT_TRUE(runtime.completeAction(first, R"({"kind":"nfc_uid","uid":"04:AB:01:02"})", 20));

    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(30, 5000, second, descriptor("read_uid", ActionKind::OneShot))));
    TEST_ASSERT_FALSE(runtime.expire(5029, ActionError::ReadTimeout, false));
    TEST_ASSERT_TRUE(runtime.expire(5030, ActionError::ReadTimeout, false));

    TEST_ASSERT_EQUAL_STRING(R"({"kind":"nfc_uid","uid":"04:AB:01:02"})", runtime.status().output.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, runtime.status().resultSequence);
    TEST_ASSERT_EQUAL_UINT32(20, runtime.status().resultUpdateMs);
    TEST_ASSERT_EQUAL(static_cast<int>(ActionState::Timeout), static_cast<int>(runtime.status().actionState));
    TEST_ASSERT_EQUAL(static_cast<int>(ActionError::ReadTimeout),
                      static_cast<int>(runtime.status().actionError));
    // A finished generation is no longer active and can stream nothing.
    TEST_ASSERT_EQUAL_STRING("", runtime.status().activeAction);
    TEST_ASSERT_EQUAL_UINT32(0, runtime.status().actionTicket);
}

// Disable invalidates the ticket and keeps cleanup; re-enable must not clear
// the cleanup flag, and the old ticket's completion must never land.
void test_cancel_reenable_rejects_old_completion_and_keeps_cleanup() {
    ModuleRuntime runtime("wifi");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);

    uint32_t oldTicket = 0, newTicket = 0;
    runtime.beginAction(3, 15000, oldTicket, descriptor("scan", ActionKind::OneShot));

    runtime.setEnabled(false, 4, true);
    TEST_ASSERT_FALSE(runtime.status().enabled);
    TEST_ASSERT_FALSE(runtime.status().connected);
    TEST_ASSERT_EQUAL_STRING("off", runtime.status().detail.c_str());
    TEST_ASSERT_TRUE(runtime.status().cleanupPending);
    TEST_ASSERT_EQUAL(static_cast<int>(ActionState::Idle), static_cast<int>(runtime.status().actionState));
    TEST_ASSERT_EQUAL_STRING("", runtime.status().activeAction);
    TEST_ASSERT_EQUAL_UINT32(0, runtime.status().actionTicket);
    runtime.setEnabled(true, 5);
    runtime.setHealth(true, "ready", 6);

    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::Busy),
                      static_cast<int>(runtime.beginAction(7, 15000, newTicket, descriptor("scan", ActionKind::OneShot))));
    TEST_ASSERT_FALSE(runtime.completeAction(oldTicket, "old", 8));
    TEST_ASSERT_TRUE(runtime.status().output.empty());

    runtime.finishCleanup(9);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(10, 15000, newTicket, descriptor("scan", ActionKind::OneShot))));
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
    runtime.beginAction(0xfffffff0u, 32, ticket, descriptor("capture", ActionKind::Record));
    TEST_ASSERT_FALSE(runtime.expire(0x0fu, ActionError::CaptureTimeout, false));
    TEST_ASSERT_TRUE(runtime.expire(0x10u, ActionError::CaptureTimeout, false));

    runtime.beginAction(20, 100, ticket, descriptor("capture", ActionKind::Record));
    runtime.completeAction(ticket, "same", 21);
    runtime.beginAction(22, 100, ticket, descriptor("capture", ActionKind::Record));
    runtime.completeAction(ticket, "same", 23);

    TEST_ASSERT_EQUAL_UINT32(2, runtime.status().resultSequence);
    TEST_ASSERT_EQUAL_UINT32(23, runtime.status().resultUpdateMs);
}

// Admission precedence is shared with main(): off before running/cleanup,
// before health, before the buffer requirement.
void test_action_admission_precedence() {
    ModuleRuntime runtime("wifi");

    // off beats everything, even a missing buffer.
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::ModuleOff),
                      static_cast<int>(actionAdmission(runtime.status(), true)));

    runtime.setEnabled(true, 1);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::HardwareError),
                      static_cast<int>(actionAdmission(runtime.status(), true)));
    runtime.setHealth(true, "ready", 2);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::BufferEmpty),
                      static_cast<int>(actionAdmission(runtime.status(), true)));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(actionAdmission(runtime.status(), false)));

    uint32_t ticket = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(3, 15000, ticket, descriptor("scan", ActionKind::OneShot))));
    // running/cleanup beats health.
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::Busy),
                      static_cast<int>(actionAdmission(runtime.status(), false)));
}

// A descriptor that needs a buffer is refused until a Record action filled it.
void test_buffer_admission_and_record_completion() {
    ModuleRuntime runtime("cc1101");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);
    TEST_ASSERT_FALSE(runtime.status().hasBuffer);
    TEST_ASSERT_TRUE(runtime.recordBuffer().empty());

    const uint8_t timings[] = {1, 2, 3, 4};
    uint32_t ticket = 0;
    // A OneShot action cannot persist a record.
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(3, 100, ticket, descriptor("capture", ActionKind::OneShot))));
    TEST_ASSERT_FALSE(runtime.completeRecord(ticket, R"({"kind":"ir"})", timings, sizeof(timings), 4));
    TEST_ASSERT_FALSE(runtime.status().hasBuffer);
    TEST_ASSERT_TRUE(runtime.recordBuffer().empty());
    TEST_ASSERT_TRUE(runtime.completeAction(ticket, "ok", 5));

    // A Record action persists bytes and marks the buffer available.
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(6, 100, ticket, descriptor("capture", ActionKind::Record))));
    TEST_ASSERT_TRUE(runtime.completeRecord(ticket, R"({"kind":"ir_capture"})", timings, sizeof(timings), 7));
    TEST_ASSERT_TRUE(runtime.status().hasBuffer);
    TEST_ASSERT_EQUAL_UINT32(sizeof(timings), runtime.recordBuffer().size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(timings, runtime.recordBuffer().data(), sizeof(timings));
    TEST_ASSERT_EQUAL_STRING(R"({"kind":"ir_capture"})", runtime.status().output.c_str());

    // Now a buffer-consuming action is admitted.
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(actionAdmission(runtime.status(), true)));

    // A stale ticket cannot overwrite the retained record.
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(8, 100, ticket, descriptor("capture", ActionKind::Record))));
    const uint32_t stale = ticket - 1;
    TEST_ASSERT_FALSE(runtime.completeRecord(stale, "late", timings, sizeof(timings), 9));
    TEST_ASSERT_EQUAL_UINT32(sizeof(timings), runtime.recordBuffer().size());
}

// Disable is the reserved cancel path: it drops the record, hasBuffer and any
// acting generation.
void test_disable_clears_record_and_generation() {
    ModuleRuntime runtime("ir");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);

    uint32_t ticket = 0;
    runtime.beginAction(3, 100, ticket, descriptor("capture", ActionKind::Record));
    const uint8_t timings[] = {9, 8, 7};
    TEST_ASSERT_TRUE(runtime.completeRecord(ticket, "rec", timings, sizeof(timings), 4));
    TEST_ASSERT_TRUE(runtime.status().hasBuffer);

    runtime.setEnabled(false, 5);
    TEST_ASSERT_FALSE(runtime.status().hasBuffer);
    TEST_ASSERT_TRUE(runtime.recordBuffer().empty());
    TEST_ASSERT_EQUAL_STRING("", runtime.status().activeAction);
    TEST_ASSERT_EQUAL_UINT32(0, runtime.status().actionTicket);
}

// Streaming: Continuous + Running + current ticket only, first sample free,
// 100 ms cadence after that, retained result untouched.
void test_stream_cadence_gate_and_retained_frame() {
    ModuleRuntime runtime("wifi");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);

    uint32_t ticket = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(100, 0, ticket, descriptor("monitor", ActionKind::Continuous))));
    TEST_ASSERT_EQUAL_STRING("monitor", runtime.status().activeAction);
    TEST_ASSERT_EQUAL_UINT32(ticket, runtime.status().actionTicket);

    TEST_ASSERT_TRUE(runtime.publishOutput(ticket, R"({"rssi":-40})", 100)); // first sample allowed
    TEST_ASSERT_FALSE(runtime.publishOutput(ticket, R"({"rssi":-41})", 150)); // too soon
    TEST_ASSERT_TRUE(runtime.publishOutput(ticket, R"({"rssi":-42})", 200));  // exactly 100 ms
    TEST_ASSERT_TRUE(runtime.publishOutput(ticket, R"({"rssi":-43})", 300));

    // The next frame replaces the retained one: only the latest is pending.
    ActionOutput out;
    TEST_ASSERT_TRUE(runtime.takeActionOutput(out));
    TEST_ASSERT_EQUAL_STRING("wifi", out.module);
    TEST_ASSERT_EQUAL_STRING("monitor", out.action);
    TEST_ASSERT_EQUAL_UINT32(ticket, out.ticket);
    TEST_ASSERT_EQUAL_UINT32(3, out.sequence); // monotonic per ticket
    TEST_ASSERT_EQUAL_UINT32(300, out.uptimeMs);
    TEST_ASSERT_EQUAL_STRING(R"({"rssi":-43})", out.payload.c_str());
    TEST_ASSERT_FALSE(runtime.takeActionOutput(out)); // take moves it out

    // Streaming never completed the action nor replaced the retained result.
    TEST_ASSERT_EQUAL(static_cast<int>(ActionState::Running), static_cast<int>(runtime.status().actionState));
    TEST_ASSERT_TRUE(runtime.status().output.empty());
    TEST_ASSERT_EQUAL_UINT32(0, runtime.status().resultSequence);
}

// Oversize and non-Continuous streams are rejected.
void test_stream_rejects_oversize_and_wrong_kind() {
    ModuleRuntime runtime("wifi");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);

    uint32_t oneshot = 0;
    runtime.beginAction(10, 100, oneshot, descriptor("scan", ActionKind::OneShot));
    TEST_ASSERT_FALSE(runtime.publishOutput(oneshot, "{}", 10));

    runtime.completeAction(oneshot, "{}", 11);
    uint32_t continuous = 0;
    runtime.beginAction(20, 0, continuous, descriptor("monitor", ActionKind::Continuous));
    const std::string tooLong(kMaxActionOutputBytes + 1, 'x');
    TEST_ASSERT_FALSE(runtime.publishOutput(continuous, tooLong, 20));
    TEST_ASSERT_TRUE(runtime.publishOutput(continuous, std::string(kMaxActionOutputBytes, 'x'), 20));
}

// Old-generation frames are discarded on stop/fail/expire and never re-enter a
// later generation.
void test_late_stream_frames_discarded() {
    ModuleRuntime runtime("wifi");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);

    uint32_t oldTicket = 0;
    runtime.beginAction(10, 0, oldTicket, descriptor("monitor", ActionKind::Continuous));
    TEST_ASSERT_TRUE(runtime.publishOutput(oldTicket, R"({"n":1})", 10));

    // Reserved Stop path (disable) invalidates the generation and the frame.
    runtime.setEnabled(false, 20);
    TEST_ASSERT_FALSE(runtime.publishOutput(oldTicket, R"({"n":2})", 20));
    ActionOutput out;
    TEST_ASSERT_FALSE(runtime.takeActionOutput(out));

    runtime.setEnabled(true, 30);
    runtime.setHealth(true, "ready", 31);
    uint32_t newTicket = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(runtime.beginAction(40, 0, newTicket, descriptor("monitor", ActionKind::Continuous))));
    TEST_ASSERT_NOT_EQUAL(oldTicket, newTicket);
    TEST_ASSERT_FALSE(runtime.publishOutput(oldTicket, R"({"n":3})", 45));
    TEST_ASSERT_TRUE(runtime.publishOutput(newTicket, R"({"n":4})", 45));
    // A new start also dropped the previous pending frame.
    TEST_ASSERT_TRUE(runtime.takeActionOutput(out));
    TEST_ASSERT_EQUAL_STRING(R"({"n":4})", out.payload.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, out.sequence);

    // Failure ends the generation too.
    TEST_ASSERT_TRUE(runtime.failAction(newTicket, ActionError::HardwareError, 50, false));
    TEST_ASSERT_FALSE(runtime.publishOutput(newTicket, R"({"n":5})", 60));
    TEST_ASSERT_FALSE(runtime.takeActionOutput(out));
}

// A Continuous action started with deadline 0 has no deadline: expire never
// fires, even when the uptime counter wraps past any arbitrary bound.
void test_continuous_zero_deadline_never_expires() {
    ModuleRuntime runtime("wifi");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);

    uint32_t ticket = 0;
    runtime.beginAction(0xfffffff0u, 0, ticket, descriptor("monitor", ActionKind::Continuous));
    TEST_ASSERT_FALSE(runtime.expire(0x0fu, ActionError::ScanTimeout, false));
    TEST_ASSERT_FALSE(runtime.expire(0xfffffff0u, ActionError::ScanTimeout, false));
    TEST_ASSERT_FALSE(runtime.expire(0xffffffffu, ActionError::ScanTimeout, false));
    TEST_ASSERT_EQUAL(static_cast<int>(ActionState::Running), static_cast<int>(runtime.status().actionState));

    // A finite Continuous deadline still expires, wrap-safe.
    uint32_t finite = 0;
    runtime.failAction(ticket, ActionError::HardwareError, 5, false);
    runtime.beginAction(0xfffffff0u, 32, finite, descriptor("monitor", ActionKind::Continuous));
    TEST_ASSERT_FALSE(runtime.expire(0x0fu, ActionError::ScanTimeout, false));
    TEST_ASSERT_TRUE(runtime.expire(0x10u, ActionError::ScanTimeout, false));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_timeout_preserves_previous_result);
    RUN_TEST(test_cancel_reenable_rejects_old_completion_and_keeps_cleanup);
    RUN_TEST(test_deadline_wrap_and_identical_results);
    RUN_TEST(test_action_admission_precedence);
    RUN_TEST(test_buffer_admission_and_record_completion);
    RUN_TEST(test_disable_clears_record_and_generation);
    RUN_TEST(test_stream_cadence_gate_and_retained_frame);
    RUN_TEST(test_stream_rejects_oversize_and_wrong_kind);
    RUN_TEST(test_late_stream_frames_discarded);
    RUN_TEST(test_continuous_zero_deadline_never_expires);
    return UNITY_END();
}
