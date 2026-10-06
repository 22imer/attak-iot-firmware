#include "wifi_ap.h"

#include <WiFi.h>

namespace wifiAp {

namespace {

// Effective config remembered at begin(); restore() reuses it verbatim.
ConfigCache g_cache;

// True once a bring-up attempt succeeded and no successful suspend has torn it
// down since. Never used as a proxy for "the radio is off": a failed bring-up
// leaves this false while the radio may still be powered.
bool g_apUp = false;

// Live backend view of whether the radio is still in AP mode.
bool apModeActive() {
    const wifi_mode_t mode = WiFi.getMode();
    return mode == WIFI_AP || mode == WIFI_AP_STA;
}

bool bringUp(const ApCredentials &credentials) {
    // softAP() with a nullptr passphrase starts an open network and also
    // enables AP mode internally, so a single call is one complete attempt.
    return WiFi.softAP(credentials.ssid.c_str(), credentials.open() ? nullptr : credentials.password.c_str());
}

} // namespace

void begin(const DeviceConfig &config) {
    if (!config.apPassword.empty() && !passwordUsable(config.apPassword)) {
        Serial.printf("wifiAp: configured password rejected (len=%u, need 0 or 8..63) — using default\n",
                      static_cast<unsigned>(config.apPassword.length()));
    }

    // Cache-only: remember the effective credentials but do NOT start the AP.
    // The dashboard is reached over USB, so boot needs no WiFi radio. An
    // operator brings the AP up explicitly with `ap on` over the Serial console.
    g_cache.remember(resolveCredentials(config));
    g_cache.setRequested(false);
    g_apUp = false;
    if (WiFi.getMode() != WIFI_OFF) WiFi.mode(WIFI_OFF); // deterministic: no AP at boot

    Serial.println("wifiAp: AP not started (requested off) — send 'ap on' over Serial to start it");
}

bool requestOn() {
    g_cache.setRequested(true);
    if (running()) return true;

    const ApCredentials *cached = g_cache.get(); // borrowed, no copy
    if (!cached) {
        Serial.println("wifiAp: ap on ignored — no cached AP config");
        return false;
    }

    ApCredentials effective = *cached;
    bool up = bringUp(effective);
    if (!up && !effective.open()) {
        Serial.println("wifiAp: softAP failed with configured settings — retrying as open AP");
        effective.password.clear();
        up = bringUp(effective);
    }

    // Cache what actually took effect (or the last attempted fallback) so a
    // later restore brings the AP back with the same credentials. remember()
    // never clears the explicit request intent.
    g_cache.remember(effective);
    g_apUp = up;

    if (up) {
        Serial.printf("wifiAp: AP \"%s\" up at %s%s\n", effective.ssid.c_str(), WiFi.softAPIP().toString().c_str(),
                      effective.open() ? " (open)" : "");
    } else {
        Serial.println("wifiAp: softAP failed — no access point is running");
    }
    return up;
}

bool requestOff() {
    g_cache.setRequested(false);
    if (WiFi.getMode() == WIFI_OFF) {
        g_apUp = false;
        return true;
    }
    const bool down = WiFi.mode(WIFI_OFF); // single attempt, no retry
    if (down) g_apUp = false;
    Serial.println(down ? "wifiAp: AP off (operator request)" : "wifiAp: AP off failed");
    return down;
}

bool requested() { return g_cache.requested(); }

bool running() { return g_apUp && apModeActive(); }

bool suspend() {
    // Report "off" only when the radio really is off. A failed bring-up sets
    // g_apUp false but can leave AP mode powered, so inspect the live mode
    // instead of trusting the flag.
    if (WiFi.getMode() == WIFI_OFF) {
        g_apUp = false;
        return true;
    }
    const bool down = WiFi.mode(WIFI_OFF); // single attempt, no retry
    if (down) {
        g_apUp = false;
        Serial.println("wifiAp: AP suspended for exclusive radio use");
    } else {
        Serial.println("wifiAp: AP suspend failed");
    }
    return down;
}

bool restore() {
    if (!g_cache.requested()) {
        // The operator did not request the AP (or explicitly turned it off):
        // leave the radio down. Report success so the arbiter can return to
        // Idle instead of retrying a bring-up nobody asked for.
        g_apUp = false;
        return true;
    }
    if (running()) return true; // AP already serving

    const ApCredentials *cached = g_cache.get(); // borrowed, no copy
    if (!cached) {
        Serial.println("wifiAp: restore skipped — no cached AP config");
        return false;
    }

    const bool up = bringUp(*cached); // single attempt, no retry
    g_apUp = up;
    if (up) {
        Serial.printf("wifiAp: AP restored at %s%s\n", WiFi.softAPIP().toString().c_str(),
                      cached->open() ? " (open)" : "");
    } else {
        Serial.println("wifiAp: AP restore failed");
    }
    return up;
}

} // namespace wifiAp
