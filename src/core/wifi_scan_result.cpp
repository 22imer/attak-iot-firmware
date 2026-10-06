#include "wifi_scan_result.h"

namespace wifiScan {

void TopNetworks::consider(const Candidate &candidate) {
    ++total_;

    size_t pos = size_;
    for (size_t i = 0; i < size_; ++i) {
        const bool better = candidate.rssi > items_[i].rssi ||
                            (candidate.rssi == items_[i].rssi && candidate.bssid < items_[i].bssid);
        if (better) {
            pos = i;
            break;
        }
    }
    if (pos >= kMaxNetworks) return;

    const size_t last = (size_ < kMaxNetworks) ? size_ : kMaxNetworks - 1;
    for (size_t i = last; i > pos; --i) items_[i] = items_[i - 1];
    items_[pos] = candidate;
    if (size_ < kMaxNetworks) ++size_;
}

const char *securityClass(uint8_t authMode) {
    switch (authMode) {
    case kAuthOpen:
        return "OPEN";
    case kAuthWep:
        return "WEP";
    case kAuthWpaPsk:
        return "WPA-PSK";
    case kAuthWpa2Psk:
        return "WPA2-PSK";
    case kAuthWpaWpa2Psk:
        return "WPA/WPA2-PSK";
    case kAuthWpa2Enterprise:
        return "WPA2-ENTERPRISE";
    case kAuthWpa3Psk:
        return "WPA3-PSK";
    case kAuthWpa2Wpa3Psk:
        return "WPA2/WPA3-PSK";
    case kAuthWapiPsk:
        return "WAPI-PSK";
    case kAuthWpa3Ent192:
        return "WPA3-ENTERPRISE-192BIT";
    default:
        return "UNKNOWN";
    }
}

bool securityClassIsSecure(uint8_t authMode) { return authMode != kAuthOpen && authMode <= kAuthWpa3Ent192; }

} // namespace wifiScan
