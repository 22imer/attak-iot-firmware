#include <unity.h>

#include "core/record_buffer.h"

void setUp() {}
void tearDown() {}

// A fresh buffer is unavailable, stays unavailable after clear, and clear must
// not leave the old bytes reachable.
void test_empty_initially_and_after_clear() {
    RecordBuffer buffer;
    TEST_ASSERT_TRUE(buffer.empty());
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(buffer.size()));
    TEST_ASSERT_NULL(buffer.data());

    const uint8_t data[] = {1, 2, 3};
    TEST_ASSERT_TRUE(buffer.replace(data, sizeof(data)));
    TEST_ASSERT_FALSE(buffer.empty());
    TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(buffer.size()));
    TEST_ASSERT_NOT_NULL(buffer.data());

    buffer.clear();
    TEST_ASSERT_TRUE(buffer.empty());
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(buffer.size()));
    TEST_ASSERT_NULL(buffer.data());
}

// replace() stores exactly the given bytes and a later replace fully supersedes
// the previous record.
void test_replace_reads_back_and_supersedes() {
    RecordBuffer buffer;
    const uint8_t first[] = {0x11, 0x22, 0x33};
    const uint8_t second[] = {0xAA, 0xBB};

    TEST_ASSERT_TRUE(buffer.replace(first, sizeof(first)));
    TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(buffer.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first, buffer.data(), 3);

    TEST_ASSERT_TRUE(buffer.replace(second, sizeof(second)));
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(buffer.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second, buffer.data(), 2);
}

// Exactly capacity is accepted; one byte more is not.
void test_capacity_boundary() {
    static uint8_t full[RecordBuffer::capacity];
    for (size_t i = 0; i < sizeof(full); ++i) full[i] = static_cast<uint8_t>(i);

    RecordBuffer buffer;
    TEST_ASSERT_TRUE(buffer.replace(full, sizeof(full)));
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(RecordBuffer::capacity),
                             static_cast<uint32_t>(buffer.size()));
    TEST_ASSERT_EQUAL_UINT8(full[0], buffer.data()[0]);
    TEST_ASSERT_EQUAL_UINT8(full[RecordBuffer::capacity - 1], buffer.data()[RecordBuffer::capacity - 1]);
}

// null / zero / over-capacity replacements are rejected and the previous record
// is preserved byte-for-byte.
void test_invalid_rejects_preserve_previous_record() {
    const uint8_t kept[] = {0xDE, 0xAD, 0xBE, 0xEF};
    static uint8_t over[RecordBuffer::capacity + 1];

    RecordBuffer buffer;
    TEST_ASSERT_TRUE(buffer.replace(kept, sizeof(kept)));

    TEST_ASSERT_FALSE(buffer.replace(nullptr, 4));
    TEST_ASSERT_FALSE(buffer.replace(kept, 0));
    TEST_ASSERT_FALSE(buffer.replace(over, sizeof(over)));

    TEST_ASSERT_FALSE(buffer.empty());
    TEST_ASSERT_EQUAL_UINT32(sizeof(kept), static_cast<uint32_t>(buffer.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kept, buffer.data(), sizeof(kept));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_initially_and_after_clear);
    RUN_TEST(test_replace_reads_back_and_supersedes);
    RUN_TEST(test_capacity_boundary);
    RUN_TEST(test_invalid_rejects_preserve_previous_record);
    return UNITY_END();
}
