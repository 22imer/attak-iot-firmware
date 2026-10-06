// Portable 802.11 management-frame builders for the disruptive WiFi payloads
// (PLAN §2.4: wifi_beacon / wifi_deauth). No Arduino/WiFi dependency, so the
// frame layout and the MAC/SSID helpers are native-testable; the firmware
// backend (src/modules/wifi_module.cpp) only copies these bytes into
// esp_wifi_80211_tx(WIFI_IF_AP, ...).
//
// Frames carry no FCS: the ESP32 driver appends it. The builders never allocate
// and never fail silently — a too-small output buffer returns 0.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wifiAttack {

// 24-byte management header + 12 fixed beacon fields + up to a 34-byte SSID IE
// + 6-byte rates IE + 3-byte DS IE.
inline constexpr size_t kMaxBeaconBytes = 96;
inline constexpr size_t kDeauthBytes = 26;
inline constexpr size_t kMaxSsidBytes = 32;
inline constexpr size_t kMacTextBytes = 18; // "AA:BB:..:FF" + NUL

inline constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Deterministic locally-administered unicast BSSID for rotation index `index`
// (bit 0 clear = unicast, bit 1 set = locally administered).
void deriveBssid(uint32_t index, uint8_t out[6]);

// Deterministic lure SSID for rotation index `index`. Writes a NUL-terminated
// string of at most kMaxSsidBytes bytes and returns its byte length; returns 0
// (and leaves `out` undefined) when `cap` is too small.
size_t lureSsid(uint32_t index, char *out, size_t cap);

// Builds one beacon for an open (no privacy) ESS. `ssid` may be nullptr only
// when ssidLen == 0 (hidden SSID). Returns the frame length, or 0 when cap is
// too small or ssidLen > kMaxSsidBytes.
size_t buildBeacon(const uint8_t bssid[6], uint8_t channel, const char *ssid, size_t ssidLen, uint32_t sequence,
                   uint8_t *out, size_t cap);

// Builds one deauthentication frame (management, reason code little-endian).
// Returns kDeauthBytes, or 0 when cap is too small.
size_t buildDeauth(const uint8_t dest[6], const uint8_t src[6], const uint8_t bssid[6], uint16_t reason,
                   uint8_t *out, size_t cap);

// Parses "AA:BB:CC:DD:EE:FF" ('-' also accepted as a separator, case
// insensitive) into out[6]. False on any malformed input.
bool parseMac(std::string_view text, uint8_t out[6]);

// Formats out[6] as uppercase colon-separated text; needs cap >= kMacTextBytes.
bool formatMac(const uint8_t mac[6], char *out, size_t cap);

} // namespace wifiAttack
