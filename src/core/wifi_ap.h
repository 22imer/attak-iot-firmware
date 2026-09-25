#pragma once

#include "storage.h"

namespace wifiAp {
    // Brings up the device's own WiFi AP (v1 has no STA/captive-portal
    // provisioning — connect directly to this AP to reach the dashboard).
    void begin(const DeviceConfig &config);
}
