#include <unity.h>

#include "core/command_queue.h"

void setUp() {}
void tearDown() {}

namespace {

EnqueuedCommand makeRequest(uint32_t id, ModuleId module, CommandKind kind) {
    EnqueuedCommand request;
    request.clientId = 7;
    request.sessionToken = 1;
    request.command.id = id;
    request.command.module = module;
    request.command.cmd = kind;
    request.command.error = CommandError::None;
    request.command.correlationValid = true;
    return request;
}

} // namespace

// 8 ordinary slots must not block the 5 reserved Stops, and everything pops in
// one shared FIFO order.
void test_normal_saturation_preserves_all_five_stops_in_fifo() {
    CommandQueue queue;

    for (uint32_t id = 1; id <= 8; ++id) {
        TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                          static_cast<int>(queue.push(makeRequest(id, ModuleId::Wifi, CommandKind::Enable))));
    }
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::QueueFull),
                      static_cast<int>(queue.push(makeRequest(99, ModuleId::Wifi, CommandKind::Enable))));

    for (uint8_t module = 0; module < 5; ++module) {
        TEST_ASSERT_EQUAL(
            static_cast<int>(CommandError::None),
            static_cast<int>(queue.push(makeRequest(9 + module, static_cast<ModuleId>(module), CommandKind::Disable))));
    }

    EnqueuedCommand out;
    for (uint32_t id = 1; id <= 13; ++id) {
        TEST_ASSERT_TRUE(queue.pop(out));
        TEST_ASSERT_EQUAL_UINT32(id, out.command.id);
    }
    TEST_ASSERT_FALSE(queue.pop(out));
}

// A duplicate queued Stop for one module is rejected and must not consume
// another module's reserved slot.
void test_duplicate_stop_does_not_take_another_category_slot() {
    CommandQueue queue;

    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(queue.push(makeRequest(1, ModuleId::Ir, CommandKind::Disable))));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::QueueFull),
                      static_cast<int>(queue.push(makeRequest(2, ModuleId::Ir, CommandKind::Disable))));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(queue.push(makeRequest(3, ModuleId::Wifi, CommandKind::Disable))));

    EnqueuedCommand out;
    TEST_ASSERT_TRUE(queue.pop(out));
    TEST_ASSERT_EQUAL_UINT32(1, out.command.id);

    // Ir's slot is free again after pop; Wifi's reserved slot was never stolen.
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
                      static_cast<int>(queue.push(makeRequest(4, ModuleId::Ir, CommandKind::Disable))));
    TEST_ASSERT_TRUE(queue.pop(out));
    TEST_ASSERT_EQUAL_UINT32(3, out.command.id);
    TEST_ASSERT_TRUE(queue.pop(out));
    TEST_ASSERT_EQUAL_UINT32(4, out.command.id);
}

void test_invalid_and_uncorrelated_commands_are_rejected() {
    CommandQueue queue;

    EnqueuedCommand uncorrelated = makeRequest(1, ModuleId::Wifi, CommandKind::Enable);
    uncorrelated.command.correlationValid = false;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidCommand),
                      static_cast<int>(queue.push(uncorrelated)));

    EnqueuedCommand alreadyInvalid = makeRequest(2, ModuleId::Wifi, CommandKind::Enable);
    alreadyInvalid.command.error = CommandError::UnsupportedAction;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::UnsupportedAction),
                      static_cast<int>(queue.push(alreadyInvalid)));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_normal_saturation_preserves_all_five_stops_in_fifo);
    RUN_TEST(test_duplicate_stop_does_not_take_another_category_slot);
    RUN_TEST(test_invalid_and_uncorrelated_commands_are_rejected);
    return UNITY_END();
}
