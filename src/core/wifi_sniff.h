// Portable WiFi raw-frame sniff buffer + aggregation (no Arduino/ESP-IDF
// dependency, so the ring/truncation/bounds behavior is native-testable).
//
// The firmware backend (src/modules/wifi_module.cpp) installs a real
// promiscuous RX callback that fills a fixed single-producer/single-consumer
// ring; this file owns the ring, the 802.11 frame-control classification
// (generic metadata only — never addresses, payload semantics or credentials)
// and the bounded JSON encoding of one streamed sample.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/action_params.h"

namespace wifiSniff {

inline constexpr uint8_t kMaxFrameBytes = 32; // raw prefix stored per frame
inline constexpr uint8_t kRingCapacity = 16;  // power of two; one slot kept free
inline constexpr uint8_t kMaxBatchFrames = 6; // frames per streamed sample
inline constexpr uint8_t kFirstChannel = 1;
inline constexpr uint8_t kLastChannel = 13;
inline constexpr uint32_t kHopIntervalMs = 500;

// 802.11 frame-control type field (bits 2..3 of frame-control byte 0). Plain
// named integers: explicit, fixed layout, no bit fields/enum-width ambiguity.
inline constexpr uint8_t kFrameTypeManagement = 0;
inline constexpr uint8_t kFrameTypeControl = 1;
inline constexpr uint8_t kFrameTypeData = 2;
inline constexpr uint8_t kFrameTypeExtension = 3;

// ParamSpec order for the `wifi_sniff` descriptor (parent's action_catalog.cpp).
inline constexpr ParamSpec kSniffParams[] = {
    {"channel", "Kênh", ParamType::Integer, false, 1, 13, 0},
    {"hop", "Nhảy kênh", ParamType::Boolean, false, 0, 0, 0},
};
inline constexpr size_t kSniffParamCount = sizeof(kSniffParams) / sizeof(kSniffParams[0]);
enum SniffParam : size_t { ParamChannel = 0, ParamHop = 1 };

struct Frame {
    uint16_t sigLen = 0; // on-air length incl. FCS
    int8_t rssi = 0;
    uint8_t channel = 0;
    uint8_t storedLen = 0; // bytes actually copied into `data`
    uint8_t type = kFrameTypeManagement;
    uint8_t subtype = 0;
    uint8_t data[kMaxFrameBytes] = {};
};

// Derives type/subtype from the first 802.11 frame-control byte. Pure bit
// extraction; never inspects addresses or upper-layer content.
void classifyFrameControl(uint8_t frameControl0, uint8_t &type, uint8_t &subtype);
const char *frameTypeName(uint8_t type);

// Fixed-capacity single-producer (WiFi RX task) / single-consumer (loop) ring.
// No allocation in push(); a full ring drops the frame and bumps the counter.
class FrameRing {
  public:
    void reset(); // caller must have stopped the producer first

    bool push(const Frame &frame);
    bool pop(Frame &frame);

    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

  private:
    Frame slots_[kRingCapacity];
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};
    std::atomic<uint32_t> dropped_{0};
};

// Accumulates up to kMaxBatchFrames drained frames for one sample.
struct Batch {
    Frame frames[kMaxBatchFrames];
    uint8_t count = 0;

    bool full() const { return count >= kMaxBatchFrames; }
    void clear() { count = 0; }
    bool add(const Frame &frame);
};

// Encodes {"kind":"wifi_sniff","channel":C,"hop":H,"total":T,"dropped":D,
// "frames":[{"len":L,"rssi":R,"ch":K,"type":"data","sub":N,"data":"<hex>"}]}.
// Emits as many of the batch's frames as fit in `maxBytes` (reported via
// `emitted`) so the sample can never exceed the output bound; returns false
// only when even the header does not fit.
bool formatBatchJson(const Batch &batch, uint16_t channel, bool hop, uint32_t total, uint32_t dropped,
                     std::string &out, size_t maxBytes = 1024, uint8_t *emitted = nullptr);

} // namespace wifiSniff
