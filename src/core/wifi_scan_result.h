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

} // namespace wifiScan
