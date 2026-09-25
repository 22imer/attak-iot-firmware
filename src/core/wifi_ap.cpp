#include "wifi_ap.h"

#include <WiFi.h>

namespace wifiAp {

void begin(const DeviceConfig &config) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(config.apSsid.c_str(), config.apPassword.c_str());
    Serial.printf(
        "wifiAp: AP \"%s\" up at %s\n", config.apSsid.c_str(), WiFi.softAPIP().toString().c_str()
    );
}

} // namespace wifiAp
