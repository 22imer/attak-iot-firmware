#include <unity.h>

#include "core/jam_plan.h"

void setUp() {}
void tearDown() {}

void test_hopper_single_channel_never_moves() {
    jamPlan::ChannelHopper hopper;
    hopper.begin(7, 7, 100, 1000);
    TEST_ASSERT_FALSE(hopper.spansMultiple());
    TEST_ASSERT_EQUAL_UINT8(7, hopper.channel());
    TEST_ASSERT_FALSE(hopper.due(1099));
    TEST_ASSERT_TRUE(hopper.due(1100));
    hopper.advance(1100);
    TEST_ASSERT_EQUAL_UINT8(7, hopper.channel());
    TEST_ASSERT_EQUAL_UINT32(0, hopper.hops());
    TEST_ASSERT_FALSE(hopper.due(1199)); // re-armed dwell
    TEST_ASSERT_TRUE(hopper.due(1200));
}

void test_hopper_wraps_and_counts_hops() {
    jamPlan::ChannelHopper hopper;
    hopper.begin(1, 3, 50, 0);
    TEST_ASSERT_TRUE(hopper.spansMultiple());
    TEST_ASSERT_EQUAL_UINT8(1, hopper.channel());
    hopper.advance(50);
    TEST_ASSERT_EQUAL_UINT8(2, hopper.channel());
    TEST_ASSERT_EQUAL_UINT32(1, hopper.hops());
    hopper.advance(100);
    TEST_ASSERT_EQUAL_UINT8(3, hopper.channel());
    hopper.advance(150);
    TEST_ASSERT_EQUAL_UINT8(1, hopper.channel()); // wrap
    TEST_ASSERT_EQUAL_UINT32(3, hopper.hops());
}

void test_hopper_normalizes_reversed_range() {
    jamPlan::ChannelHopper hopper;
    hopper.begin(5, 2, 10, 0);
    TEST_ASSERT_EQUAL_UINT8(2, hopper.first());
    TEST_ASSERT_EQUAL_UINT8(5, hopper.last());
    TEST_ASSERT_EQUAL_UINT8(2, hopper.channel());
}

void test_hopper_due_is_wrap_safe() {
    jamPlan::ChannelHopper hopper;
    hopper.begin(0, 1, 100, 0xFFFFFFF0u); // deadline wraps around to 0x00000054
    TEST_ASSERT_FALSE(hopper.due(0x00000040u)); // after the wrap but before the deadline
    TEST_ASSERT_TRUE(hopper.due(0x00000060u));  // past the wrapped deadline
    hopper.advance(0x00000060u);
    TEST_ASSERT_FALSE(hopper.due(0x00000070u));
}

void test_duty_gate_toggles_on_schedule() {
    jamPlan::DutyGate gate;
    gate.begin(200, 100, 1000);
    TEST_ASSERT_TRUE(gate.carrierOn());
    TEST_ASSERT_EQUAL_UINT32(0, gate.toggles());
    TEST_ASSERT_FALSE(gate.update(1199));
    TEST_ASSERT_TRUE(gate.update(1200));
    TEST_ASSERT_FALSE(gate.carrierOn());
    TEST_ASSERT_EQUAL_UINT32(1, gate.toggles());
    TEST_ASSERT_FALSE(gate.update(1299));
    TEST_ASSERT_TRUE(gate.update(1300));
    TEST_ASSERT_TRUE(gate.carrierOn());
    TEST_ASSERT_EQUAL_UINT32(2, gate.toggles());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_hopper_single_channel_never_moves);
    RUN_TEST(test_hopper_wraps_and_counts_hops);
    RUN_TEST(test_hopper_normalizes_reversed_range);
    RUN_TEST(test_hopper_due_is_wrap_safe);
    RUN_TEST(test_duty_gate_toggles_on_schedule);
    return UNITY_END();
}
