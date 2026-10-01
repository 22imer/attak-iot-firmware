// WiFi scan — the 5th "module," using the ESP32-S3's own onboard radio
// (not one of the 4 purchased breakout modules). Toggling it on does not
// disrupt the dashboard's own AP: WiFi.scanNetworks() switches the radio
// to AP_STA internally, scans, then returns to AP-only. Deauth/evil-portal/
// promiscuous-mode attacks are explicitly out of scope for this pass —
// Bruce's own code tears its dashboard AP down before running those,
// which this project's toggle model doesn't support yet.
#pragma once

#include "core/module_status.h"

namespace wifiModule {
    void begin();
    void setEnabled(bool enabled);
    void handleAction(const std::string &action); // "scan"
    void poll();
    ModuleStatus status();
}
