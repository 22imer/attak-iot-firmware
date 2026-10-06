#pragma once

#include <cstddef>
#include <cstdint>

// One directly attached laptop. The USB lease is independent of ESP-IDF 4.4's
// singleton AP DHCP server. Responses never advertise a router or DNS server.
namespace usbDhcp {
struct Reply {
    bool handled;
    size_t size;
};
constexpr size_t kReplyCapacity = 342;

// Consumes an Ethernet frame; writes a complete broadcast OFFER/ACK/NAK frame.
// handled=true with size=0 means DHCP was consumed without replying. Other
// traffic stays on the normal lwIP input path. No heap allocation or retained
// input pointers; out must not overlap frame.
Reply respond(const uint8_t *frame, size_t size, const uint8_t deviceMac[6], uint8_t *out, size_t capacity);
} // namespace usbDhcp
