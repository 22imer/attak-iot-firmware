// Pure selection of the strongest scan results. No WiFi/Arduino dependency so
// the ordering/truncation boundary is native-testable. Holds only candidate
// indices + the scan fields used for ordering (RSSI, BSSID), not SSID strings.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace wifiScan {

struct Candidate {
    size_t index = 0;               // index into the raw scan result list
    int rssi = 0;                   // dBm
    std::array<uint8_t, 6> bssid{}; // used as the stable tie-break
};

class TopNetworks {
  public:
    static constexpr size_t kMaxNetworks = 32;

    // Keeps the best kMaxNetworks by RSSI descending, BSSID ascending on ties.
    void consider(const Candidate &candidate);

    size_t size() const { return size_; }
    const Candidate &at(size_t i) const { return items_[i]; }
    bool truncated() const { return total_ > kMaxNetworks; }

  private:
    std::array<Candidate, kMaxNetworks> items_{};
    size_t size_ = 0;
    size_t total_ = 0;
};
// Auth-mode constants mirror `wifi_auth_mode_t` in the ESP-IDF
// esp_wifi_types.h shipped with the Arduino core. They are duplicated here (as
// plain uint8_t) so this header stays free of WiFi/Arduino includes and the
// mapping stays native-testable; the driver passes WiFi.encryptionType()
// straight through. Values come from the installed core header
// (framework-arduinoespressif32 4.2.x, tools/sdk/esp32s3/include/esp_wifi).
enum AuthMode : uint8_t {
    kAuthOpen = 0,
    kAuthWep = 1,
    kAuthWpaPsk = 2,
    kAuthWpa2Psk = 3,
    kAuthWpaWpa2Psk = 4,
    kAuthWpa2Enterprise = 5,
    kAuthWpa3Psk = 6,
    kAuthWpa2Wpa3Psk = 7,
    kAuthWapiPsk = 8,
    kAuthWpa3Ent192 = 9,
};

// Human-readable name of the AP's security class ("WPA2-PSK", "WPA3-PSK",
// "OPEN", ...). Unknown/out-of-range values collapse to "UNKNOWN" rather than
// leaking a raw integer into the payload the operator reads.
const char *securityClass(uint8_t authMode);

// Whether the class actually protects traffic. WPA/WPA2/WPA3-PSK, enterprise
// and WAPI count; only open (and the unknown class, which we must not claim is
// protected) do not.
bool securityClassIsSecure(uint8_t authMode);


} // namespace wifiScan
