#include "core/wifi_sniff.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace wifiSniff {

namespace {

constexpr size_t kJsonCapacity = 1024;
constexpr size_t kFrameJsonCapacity = 192;

bool appendFmt(char *buffer, size_t capacity, size_t &used, const char *format, ...) {
    if (used >= capacity) return false;
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buffer + used, capacity - used, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= capacity - used) return false;
    used += static_cast<size_t>(written);
    return true;
}

void hexEncode(const uint8_t *data, size_t length, char *out) {
    static const char kDigits[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
        out[i * 2] = kDigits[data[i] >> 4];
        out[i * 2 + 1] = kDigits[data[i] & 0x0F];
    }
    out[length * 2] = '\0';
}

// Serializes one frame into `buffer`; returns the length (excluding the NUL),
// or 0 when it does not fit in kFrameJsonCapacity.
size_t frameToJson(const Frame &frame, char *buffer) {
    char hex[kMaxFrameBytes * 2 + 1];
    hexEncode(frame.data, frame.storedLen, hex);
    const int written = snprintf(buffer, kFrameJsonCapacity,
                                 "{\"len\":%u,\"rssi\":%d,\"ch\":%u,\"type\":\"%s\",\"sub\":%u,\"data\":\"%s\"}",
                                 static_cast<unsigned>(frame.sigLen), static_cast<int>(frame.rssi),
                                 static_cast<unsigned>(frame.channel), frameTypeName(frame.type),
                                 static_cast<unsigned>(frame.subtype), hex);
    if (written < 0 || static_cast<size_t>(written) >= kFrameJsonCapacity) return 0;
    return static_cast<size_t>(written);
}

} // namespace

void classifyFrameControl(uint8_t frameControl0, uint8_t &type, uint8_t &subtype) {
    type = static_cast<uint8_t>((frameControl0 >> 2) & 0x03);
    subtype = static_cast<uint8_t>((frameControl0 >> 4) & 0x0F);
}

const char *frameTypeName(uint8_t type) {
    switch (type) {
    case kFrameTypeManagement: return "mgmt";
    case kFrameTypeControl: return "ctrl";
    case kFrameTypeData: return "data";
    case kFrameTypeExtension: return "ext";
    default: return "ext";
    }
}

void FrameRing::reset() {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
    dropped_.store(0, std::memory_order_relaxed);
}

bool FrameRing::push(const Frame &frame) {
    const uint32_t head = head_.load(std::memory_order_relaxed);
    const uint32_t next = (head + 1) & (kRingCapacity - 1);
    if (next == tail_.load(std::memory_order_acquire)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    slots_[head] = frame;
    head_.store(next, std::memory_order_release);
    return true;
}

bool FrameRing::pop(Frame &frame) {
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    frame = slots_[tail];
    tail_.store((tail + 1) & (kRingCapacity - 1), std::memory_order_release);
    return true;
}

bool Batch::add(const Frame &frame) {
    if (full()) return false;
    frames[count++] = frame;
    return true;
}

bool formatBatchJson(const Batch &batch, uint16_t channel, bool hop, uint32_t total, uint32_t dropped,
                     std::string &out, size_t maxBytes, uint8_t *emitted) {
    if (maxBytes > kJsonCapacity) maxBytes = kJsonCapacity;
    if (emitted) *emitted = 0;

    char buffer[kJsonCapacity];
    size_t used = 0;
    if (!appendFmt(buffer, maxBytes, used,
                   "{\"kind\":\"wifi_sniff\",\"channel\":%u,\"hop\":%s,\"total\":%lu,\"dropped\":%lu,\"frames\":[",
                   static_cast<unsigned>(channel), hop ? "true" : "false", static_cast<unsigned long>(total),
                   static_cast<unsigned long>(dropped))) {
        return false;
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < batch.count; ++i) {
        char frameBuffer[kFrameJsonCapacity];
        const size_t length = frameToJson(batch.frames[i], frameBuffer);
        if (length == 0) break; // cannot represent this frame; stop before the bound
        const size_t separator = count == 0 ? 0 : 1;
        // Reserve 2 bytes for the closing "]}" so the sample is always valid.
        if (used + separator + length + 2 > maxBytes) break;
        if (separator != 0) buffer[used++] = ',';
        std::memcpy(buffer + used, frameBuffer, length);
        used += length;
        ++count;
    }
    if (!appendFmt(buffer, maxBytes, used, "]}")) return false;

    if (emitted) *emitted = count;
    out.assign(buffer, used);
    return true;
}

} // namespace wifiSniff
