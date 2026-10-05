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

} // namespace wifiScan
