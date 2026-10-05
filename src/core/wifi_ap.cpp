#include "wifi_ap.h"

#include <WiFi.h>

namespace wifiAp {

namespace {

constexpr size_t kMinWpaPasswordLen = 8; // WPA2 PSK minimum; shorter => softAP fails

// WPA2 needs an 8..63 char key; an empty key means an open AP. Anything in
// between (e.g. a hand-edited /config.json) silently breaks softAP, so treat it
// as invalid and fall back rather than bringing up no AP at all.
bool passwordUsable(const std::string &password) {
    return password.empty() || (password.length() >= kMinWpaPasswordLen && password.length() <= 63);
}

bool bringUp(const char *ssid, const char *password) {
    // softAP() with an empty/nullptr password starts an open network.
    const bool open = password == nullptr || password[0] == '\0';
    return WiFi.softAP(ssid, open ? nullptr : password);
}

} // namespace

void begin(const DeviceConfig &config) {
    WiFi.mode(WIFI_AP);

    const DeviceConfig defaults; // compiled-in fallback SSID/password
    const char *ssid = config.apSsid.empty() ? defaults.apSsid.c_str() : config.apSsid.c_str();

    std::string password = config.apPassword;
    if (!passwordUsable(password)) {
        Serial.printf("wifiAp: configured password rejected (len=%u, need 0 or 8..63) — using default\n",
                      static_cast<unsigned>(password.length()));
        password = defaults.apPassword;
        if (!passwordUsable(password)) password.clear(); // last resort: open AP
    }

    bool up = bringUp(ssid, password.c_str());
    if (!up) {
        Serial.println("wifiAp: softAP failed with configured settings — retrying as open AP");
        up = bringUp(ssid, nullptr);
    }

    if (up) {
        Serial.printf("wifiAp: AP \"%s\" up at %s%s\n", ssid, WiFi.softAPIP().toString().c_str(),
                      password.empty() ? " (open)" : "");
    } else {
        Serial.println("wifiAp: softAP failed — no access point is running");
    }
}

} // namespace wifiAp
