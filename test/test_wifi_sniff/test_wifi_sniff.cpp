#include <unity.h>

#include <ArduinoJson.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "core/wifi_sniff.h"

void setUp() {}
void tearDown() {}

namespace {

wifiSniff::Frame makeFrame(uint16_t sigLen, uint8_t storedLen, uint8_t first) {
    wifiSniff::Frame frame;
    frame.sigLen = sigLen;
    frame.storedLen = storedLen;
    frame.rssi = -42;
    frame.channel = 6;
    frame.type = wifiSniff::kFrameTypeData;
    frame.subtype = 0;
    for (uint8_t i = 0; i < storedLen && i < wifiSniff::kMaxFrameBytes; ++i) frame.data[i] = first + i;
    return frame;
}

} // namespace

// --- 802.11 frame-control classification ----------------------------------

void test_classify_frame_control_bits() {
    uint8_t type = 0xFF, subtype = 0xFF;
    wifiSniff::classifyFrameControl(0x80, type, subtype); // beacon
    TEST_ASSERT_EQUAL_UINT8(wifiSniff::kFrameTypeManagement, type);
    TEST_ASSERT_EQUAL_UINT8(8, subtype);

    wifiSniff::classifyFrameControl(0x40, type, subtype); // probe request
    TEST_ASSERT_EQUAL_UINT8(wifiSniff::kFrameTypeManagement, type);
    TEST_ASSERT_EQUAL_UINT8(4, subtype);

    wifiSniff::classifyFrameControl(0xB4, type, subtype); // RTS is a control frame
    TEST_ASSERT_EQUAL_UINT8(wifiSniff::kFrameTypeControl, type);
    TEST_ASSERT_EQUAL_UINT8(11, subtype);

    wifiSniff::classifyFrameControl(0x08, type, subtype); // plain data
    TEST_ASSERT_EQUAL_UINT8(wifiSniff::kFrameTypeData, type);
    TEST_ASSERT_EQUAL_UINT8(0, subtype);

    wifiSniff::classifyFrameControl(0x0C, type, subtype); // extension type bits == 3
    TEST_ASSERT_EQUAL_UINT8(wifiSniff::kFrameTypeExtension, type);
}

void test_frame_type_names() {
    TEST_ASSERT_EQUAL_STRING("mgmt", wifiSniff::frameTypeName(wifiSniff::kFrameTypeManagement));
    TEST_ASSERT_EQUAL_STRING("ctrl", wifiSniff::frameTypeName(wifiSniff::kFrameTypeControl));
    TEST_ASSERT_EQUAL_STRING("data", wifiSniff::frameTypeName(wifiSniff::kFrameTypeData));
    TEST_ASSERT_EQUAL_STRING("ext", wifiSniff::frameTypeName(wifiSniff::kFrameTypeExtension));
    TEST_ASSERT_EQUAL_STRING("ext", wifiSniff::frameTypeName(99)); // unknown is safe
}

// --- SPSC ring -------------------------------------------------------------

void test_ring_is_fifo_and_preserves_frame_fields() {
    wifiSniff::FrameRing ring;
    ring.reset();
    for (uint8_t i = 0; i < 5; ++i) TEST_ASSERT_TRUE(ring.push(makeFrame(100 + i, 4, i)));

    for (uint8_t i = 0; i < 5; ++i) {
        wifiSniff::Frame out;
        TEST_ASSERT_TRUE(ring.pop(out));
        TEST_ASSERT_EQUAL_UINT16(100 + i, out.sigLen);
        TEST_ASSERT_EQUAL_UINT8(4, out.storedLen);
        TEST_ASSERT_EQUAL_UINT8(i, out.data[0]);
    }
    wifiSniff::Frame out;
    TEST_ASSERT_FALSE(ring.pop(out));
}

void test_ring_keeps_one_slot_free_and_counts_drops() {
    wifiSniff::FrameRing ring;
    ring.reset();
    for (uint8_t i = 0; i < wifiSniff::kRingCapacity - 1; ++i) {
        TEST_ASSERT_TRUE(ring.push(makeFrame(i, 0, 0)));
    }
    TEST_ASSERT_EQUAL_UINT32(0, ring.dropped());

    TEST_ASSERT_FALSE(ring.push(makeFrame(999, 0, 0))); // ring full
    TEST_ASSERT_EQUAL_UINT32(1, ring.dropped());
    TEST_ASSERT_FALSE(ring.push(makeFrame(999, 0, 0)));
    TEST_ASSERT_EQUAL_UINT32(2, ring.dropped());
}

void test_ring_wraps_and_reset_clears() {
    wifiSniff::FrameRing ring;
    ring.reset();
    for (uint8_t i = 0; i < 10; ++i) ring.push(makeFrame(i, 0, 0));
    for (uint8_t i = 0; i < 10; ++i) {
        wifiSniff::Frame out;
        TEST_ASSERT_TRUE(ring.pop(out));
    }
    ring.push(makeFrame(777, 0, 0)); // wraps the indices
    wifiSniff::Frame out;
    TEST_ASSERT_TRUE(ring.pop(out));
    TEST_ASSERT_EQUAL_UINT16(777, out.sigLen);

    ring.reset();
    TEST_ASSERT_FALSE(ring.pop(out));
    TEST_ASSERT_EQUAL_UINT32(0, ring.dropped());
}

// --- batch + bounded JSON --------------------------------------------------

void test_batch_stops_at_capacity() {
    wifiSniff::Batch batch;
    for (uint8_t i = 0; i < wifiSniff::kMaxBatchFrames; ++i) {
        TEST_ASSERT_TRUE(batch.add(makeFrame(i, 0, 0)));
    }
    TEST_ASSERT_TRUE(batch.full());
    TEST_ASSERT_FALSE(batch.add(makeFrame(99, 0, 0)));
    TEST_ASSERT_EQUAL_UINT8(wifiSniff::kMaxBatchFrames, batch.count);
    batch.clear();
    TEST_ASSERT_FALSE(batch.full());
}

void test_batch_json_empty_batch() {
    wifiSniff::Batch batch;
    std::string output;
    uint8_t emitted = 7;
    TEST_ASSERT_TRUE(wifiSniff::formatBatchJson(batch, 6, false, 0, 0, output, 1024, &emitted));
    TEST_ASSERT_EQUAL_UINT8(0, emitted);
    TEST_ASSERT_TRUE(output.size() <= 1024);

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, output) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("wifi_sniff", doc["kind"].as<const char *>());
    TEST_ASSERT_EQUAL_UINT32(6, doc["channel"].as<uint32_t>());
    TEST_ASSERT_FALSE(doc["hop"].as<bool>());
    TEST_ASSERT_EQUAL_UINT32(0, doc["total"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(0, doc["dropped"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(doc["frames"].size()));
}

void test_batch_json_emits_all_frames_within_bound() {
    wifiSniff::Batch batch;
    for (uint8_t i = 0; i < wifiSniff::kMaxBatchFrames; ++i) {
        wifiSniff::Frame frame = makeFrame(65535, wifiSniff::kMaxFrameBytes, 0xDE);
        frame.rssi = -128;
        frame.channel = 13;
        frame.subtype = 15;
        batch.add(frame);
    }

    std::string output;
    uint8_t emitted = 0;
    TEST_ASSERT_TRUE(wifiSniff::formatBatchJson(batch, 13, true, 4294967295u, 4294967295u, output, 1024, &emitted));
    TEST_ASSERT_EQUAL_UINT8(wifiSniff::kMaxBatchFrames, emitted);
    TEST_ASSERT_TRUE(output.size() <= 1024);

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, output) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("wifi_sniff", doc["kind"].as<const char *>());
    TEST_ASSERT_EQUAL_UINT32(13, doc["channel"].as<uint32_t>());
    TEST_ASSERT_TRUE(doc["hop"].as<bool>());
    TEST_ASSERT_EQUAL_UINT32(4294967295u, doc["total"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(4294967295u, doc["dropped"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(wifiSniff::kMaxBatchFrames, static_cast<uint32_t>(doc["frames"].size()));

    std::string expectedHex;
    for (uint8_t i = 0; i < wifiSniff::kMaxFrameBytes; ++i) {
        char byte[3];
        std::snprintf(byte, sizeof(byte), "%02x", static_cast<unsigned>(0xDE + i));
        expectedHex += byte;
    }
    for (uint8_t i = 0; i < wifiSniff::kMaxBatchFrames; ++i) {
        TEST_ASSERT_EQUAL_UINT32(65535, doc["frames"][i]["len"].as<uint32_t>());
        TEST_ASSERT_EQUAL_INT(-128, doc["frames"][i]["rssi"].as<int>());
        TEST_ASSERT_EQUAL_UINT32(13, doc["frames"][i]["ch"].as<uint32_t>());
        TEST_ASSERT_EQUAL_STRING("data", doc["frames"][i]["type"].as<const char *>());
        TEST_ASSERT_EQUAL_UINT32(15, doc["frames"][i]["sub"].as<uint32_t>());
        TEST_ASSERT_EQUAL_STRING(expectedHex.c_str(), doc["frames"][i]["data"].as<const char *>());
    }
}

void test_batch_json_truncates_to_budget_never_overflows() {
    wifiSniff::Batch batch;
    for (uint8_t i = 0; i < wifiSniff::kMaxBatchFrames; ++i) {
        batch.add(makeFrame(1500, wifiSniff::kMaxFrameBytes, 0x00));
    }

    std::string output;
    uint8_t emitted = 0;
    // The header fits in 300 bytes but the whole 6-frame batch does not: the
    // encoder must emit only whole frames and never exceed the budget.
    TEST_ASSERT_TRUE(wifiSniff::formatBatchJson(batch, 1, false, 1, 0, output, 300, &emitted));
    TEST_ASSERT_TRUE(output.size() <= 300);
    TEST_ASSERT_TRUE(emitted >= 1);
    TEST_ASSERT_TRUE(emitted < wifiSniff::kMaxBatchFrames);

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, output) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_UINT32(emitted, static_cast<uint32_t>(doc["frames"].size()));
    for (uint8_t i = 0; i < emitted; ++i) {
        TEST_ASSERT_EQUAL_UINT32(1500, doc["frames"][i]["len"].as<uint32_t>());
        TEST_ASSERT_EQUAL_STRING("data", doc["frames"][i]["type"].as<const char *>());
    }

    std::string tiny = "sentinel";
    TEST_ASSERT_FALSE(wifiSniff::formatBatchJson(batch, 1, false, 1, 0, tiny, 10, nullptr));
    TEST_ASSERT_EQUAL_STRING("sentinel", tiny.c_str());
}

void test_batch_json_hexes_only_stored_bytes() {
    wifiSniff::Batch batch;
    wifiSniff::Frame frame;
    frame.sigLen = 1500; // on-air length far beyond what we stored
    frame.storedLen = 2;
    frame.data[0] = 0xDE;
    frame.data[1] = 0xAD;
    frame.type = wifiSniff::kFrameTypeManagement;
    frame.subtype = 8;
    batch.add(frame);

    std::string output;
    TEST_ASSERT_TRUE(wifiSniff::formatBatchJson(batch, 1, false, 1, 0, output));

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, output) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(doc["frames"].size()));
    TEST_ASSERT_EQUAL_STRING("dead", doc["frames"][0]["data"].as<const char *>());
    TEST_ASSERT_EQUAL_UINT32(1500, doc["frames"][0]["len"].as<uint32_t>());
    TEST_ASSERT_EQUAL_STRING("mgmt", doc["frames"][0]["type"].as<const char *>());
    TEST_ASSERT_EQUAL_UINT32(8, doc["frames"][0]["sub"].as<uint32_t>());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_classify_frame_control_bits);
    RUN_TEST(test_frame_type_names);
    RUN_TEST(test_ring_is_fifo_and_preserves_frame_fields);
    RUN_TEST(test_ring_keeps_one_slot_free_and_counts_drops);
    RUN_TEST(test_ring_wraps_and_reset_clears);
    RUN_TEST(test_batch_stops_at_capacity);
    RUN_TEST(test_batch_json_empty_batch);
    RUN_TEST(test_batch_json_emits_all_frames_within_bound);
    RUN_TEST(test_batch_json_truncates_to_budget_never_overflows);
    RUN_TEST(test_batch_json_hexes_only_stored_bytes);
    return UNITY_END();
}
