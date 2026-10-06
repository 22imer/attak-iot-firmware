#include "usb_dhcp.h"

#include <cstring>

namespace usbDhcp {
namespace {
constexpr uint8_t kServer[] = {192, 168, 7, 1};
constexpr uint8_t kLease[] = {192, 168, 7, 2};
constexpr uint8_t kMask[] = {255, 255, 255, 0};
constexpr uint8_t kCookie[] = {99, 130, 83, 99};
constexpr size_t kBootpSize = 240;
constexpr Reply kConsumed{true, 0};

uint16_t get16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }
void put16(uint8_t *p, uint16_t value) { p[0] = value >> 8; p[1] = value; }
bool zeroAddress(const uint8_t *p) { return (p[0] | p[1] | p[2] | p[3]) == 0; }

struct Options {
    uint8_t type = 0;
    const uint8_t *requested = nullptr;
    const uint8_t *server = nullptr;
};

bool parseOptions(const uint8_t *p, size_t size, Options &options) {
    size_t pos = kBootpSize;
    while (pos < size) {
        const uint8_t code = p[pos++];
        if (code == 255) return options.type != 0;
        if (code == 0) continue;
        if (pos == size) return false;
        const uint8_t length = p[pos++];
        if (length > size - pos) return false;
        if (code == 53) {
            if (length != 1 || options.type != 0) return false;
            options.type = p[pos];
        } else if (code == 50) {
            if (length != 4 || options.requested) return false;
            options.requested = p + pos;
        } else if (code == 54) {
            if (length != 4 || options.server) return false;
            options.server = p + pos;
        } else if (code == 52) {
            // Option-overloaded BOOTP fields are not used by the target laptop;
            // do not accidentally process an incomplete option set.
            return false;
        }
        pos += length;
    }
    return false;
}

void appendOption(uint8_t *out, size_t &pos, uint8_t code, const uint8_t *value, uint8_t size) {
    out[pos++] = code;
    out[pos++] = size;
    memcpy(out + pos, value, size);
    pos += size;
}
} // namespace

Reply respond(const uint8_t *frame, size_t size, const uint8_t deviceMac[6], uint8_t *out, size_t capacity) {
    if (!frame || size < 42 || get16(frame + 12) != 0x0800) return {false, 0};
    const uint8_t *ip = frame + 14;
    const size_t headerSize = (ip[0] & 15) * 4;
    if ((ip[0] >> 4) != 4 || headerSize < 20 || headerSize > size - 22 || ip[9] != 17)
        return {false, 0};
    const uint8_t *udp = ip + headerSize;
    if (get16(udp) != 68 || get16(udp + 2) != 67) return {false, 0};

    // DHCP is intercepted before lwIP: never bind UDP port 67 or use IDF 4.4's
    // singleton server, which belongs to the optional WiFi AP.
    const size_t ipSize = get16(ip + 2);
    const size_t udpSize = get16(udp + 4);
    if ((get16(ip + 6) & 0x3fff) != 0 || ipSize > size - 14 || ipSize < headerSize + 8 ||
        udpSize < 8 + kBootpSize || udpSize != ipSize - headerSize)
        return kConsumed;
    const uint8_t *bootp = udp + 8;
    const size_t bootpSize = udpSize - 8;
    if (bootp[0] != 1 || bootp[1] != 1 || bootp[2] != 6 || !zeroAddress(bootp + 24) ||
        memcmp(bootp + 236, kCookie, 4) != 0)
        return kConsumed;
    // Only a directly attached Ethernet client; do not allocate leases to a
    // relay or multicast/zero hardware address.
    uint8_t macBits = 0;
    for (size_t i = 0; i < 6; ++i) macBits |= bootp[28 + i];
    if (macBits == 0 || (bootp[28] & 1) != 0) return kConsumed;

    Options options;
    if (!parseOptions(bootp, bootpSize, options)) return kConsumed;
    uint8_t responseType;
    if (options.type == 1) {
        responseType = 2; // DISCOVER -> OFFER
    } else if (options.type == 3) {
        if (options.server && memcmp(options.server, kServer, 4) != 0) return kConsumed;
        const uint8_t *requested = options.requested ? options.requested : bootp + 12;
        if (zeroAddress(requested)) return kConsumed;
        responseType = memcmp(requested, kLease, 4) == 0 ? 5 : 6; // ACK / NAK
    } else {
        return kConsumed; // RELEASE/DECLINE and unrelated DHCP messages need no reply
    }
    if (!out || !deviceMac || capacity < kReplyCapacity) return kConsumed;

    memset(out, 0, kReplyCapacity);
    memset(out, 255, 6); // Ethernet broadcast, including renewals/NAKs
    memcpy(out + 6, deviceMac, 6);
    put16(out + 12, 0x0800);
    uint8_t *replyIp = out + 14;
    replyIp[0] = 0x45;
    put16(replyIp + 2, kReplyCapacity - 14);
    replyIp[8] = 64;
    replyIp[9] = 17;
    memcpy(replyIp + 12, kServer, 4);
    memset(replyIp + 16, 255, 4);
    uint32_t sum = 0;
    for (size_t i = 0; i < 20; i += 2) sum += get16(replyIp + i);
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    put16(replyIp + 10, uint16_t(~sum));

    uint8_t *replyUdp = replyIp + 20;
    put16(replyUdp, 67);
    put16(replyUdp + 2, 68);
    put16(replyUdp + 4, kReplyCapacity - 34);
    // IPv4 UDP checksum zero is valid; the USB link already protects frames.
    uint8_t *reply = replyUdp + 8;
    reply[0] = 2;
    reply[1] = 1;
    reply[2] = 6;
    memcpy(reply + 4, bootp + 4, 4); // transaction ID
    put16(reply + 10, 0x8000); // broadcast response
    memcpy(reply + 28, bootp + 28, 6);
    if (responseType != 6) memcpy(reply + 16, kLease, 4);
    memcpy(reply + 236, kCookie, 4);
    size_t pos = kBootpSize;
    appendOption(reply, pos, 53, &responseType, 1);
    appendOption(reply, pos, 54, kServer, 4);
    if (responseType != 6) {
        const uint8_t leaseSeconds[] = {0, 0, 14, 16}; // 3600 seconds
        appendOption(reply, pos, 51, leaseSeconds, 4);
        appendOption(reply, pos, 1, kMask, 4);
    }
    reply[pos] = 255;
    return {true, kReplyCapacity};
}
} // namespace usbDhcp
