#include "wifi_module.h"

#include <WiFi.h>

namespace wifiModule {

namespace {
ModuleStatus currentStatus = {"wifi", false, false, "off", "", 0};
bool scanInFlight = false;
}

void begin() {
    // Nothing to init — the radio is already brought up by wifiAp::begin();
    // WiFi.scanNetworks() below switches it to AP_STA for the duration of
    // a scan and back on its own, without disrupting the dashboard AP.
}

void setEnabled(bool enabled) {
    currentStatus.enabled = enabled;
    currentStatus.connected = enabled;
    currentStatus.detail = enabled ? "ready" : "off";
    currentStatus.lastUpdateMs = millis();
    if (!enabled && scanInFlight) {
        WiFi.scanDelete();
        scanInFlight = false;
    }
}

void handleAction(const std::string &action) {
    if (!currentStatus.enabled || action != "scan" || scanInFlight) return;
    WiFi.scanNetworks(/*async=*/true);
    scanInFlight = true;
    currentStatus.detail = "scanning";
    currentStatus.lastUpdateMs = millis();
}

void poll() {
    if (!currentStatus.enabled || !scanInFlight) return;

    int16_t result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) return;

    scanInFlight = false;
    currentStatus.lastUpdateMs = millis();

    if (result == WIFI_SCAN_FAILED) {
        currentStatus.detail = "scan failed";
        currentStatus.output = "";
        return;
    }

    currentStatus.detail = "scan complete";
    std::string ssids;
    for (int16_t i = 0; i < result; i++) {
        if (i > 0) ssids += ", ";
        ssids += WiFi.SSID(i).c_str();
    }
    currentStatus.output = ssids;
    WiFi.scanDelete();
}

ModuleStatus status() { return currentStatus; }

} // namespace wifiModule
