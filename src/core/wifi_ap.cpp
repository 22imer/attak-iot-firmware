#include "wifi_ap.h"

#include <WiFi.h>
#include <esp_wifi.h>

#ifdef ENABLE_DISRUPTIVE
#include "core/evil_twin.h"
#endif
namespace wifiAp {

namespace {

// Effective config remembered at begin(); restore() reuses it verbatim.
ConfigCache g_cache;

// True once a bring-up attempt succeeded and no successful suspend has torn it
// down since. Never used as a proxy for "the radio is off": a failed bring-up
// leaves this false while the radio may still be powered.
bool g_apUp = false;
#ifdef ENABLE_DISRUPTIVE
// Portal AP ownership is temporary and never changes the operator's AP intent.
bool g_portalActive = false;
bool g_portalRestarted = false;
wifi_mode_t g_portalPreviousMode = WIFI_OFF;
uint8_t g_portalPreviousChannel = 1;
#endif

// Live backend view of whether the radio is still in AP mode.
bool apModeActive() {
    const wifi_mode_t mode = WiFi.getMode();
    return mode == WIFI_AP || mode == WIFI_AP_STA;
}

bool bringUp(const ApCredentials &credentials, uint8_t channel = 1) {
    // A nullptr passphrase starts an open AP. Set the channel at bring-up so
    // the first beacon already uses the requested/restored channel.
    return WiFi.softAP(credentials.ssid.c_str(), credentials.open() ? nullptr : credentials.password.c_str(), channel);
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

#ifdef ENABLE_DISRUPTIVE
bool beginPortal(const std::string &ssid, uint8_t channel) {
    if (g_portalActive || (!ssid.empty() && !evilTwin::cloneSsidUsable(ssid)) ||
        (channel != 0 && !evilTwin::channelUsable(channel))) return false;
    const ApCredentials *cached = g_cache.get();
    if (!cached) return false;

    g_portalPreviousMode = WiFi.getMode();
    g_portalPreviousChannel = 1;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (g_portalPreviousMode != WIFI_OFF &&
        esp_wifi_get_channel(&g_portalPreviousChannel, &second) != ESP_OK) return false;
    const uint8_t effectiveChannel = channel == 0 ? g_portalPreviousChannel : channel;
    const ApCredentials portal = portalCredentials(*cached, ssid);
    g_portalRestarted = portalNeedsRestart(cached, ssid, running());
    g_portalActive = true;

    bool up = false;
    if (!g_portalRestarted) {
        // Already an open AP with the right name: only the channel changes, so
        // no client is dropped and the live AP keeps serving.
        up = esp_wifi_set_channel(effectiveChannel, WIFI_SECOND_CHAN_NONE) == ESP_OK;
    } else {
        // Restart with the open portal credentials: a cloned name, an AP that
        // was down, or an AP that was password-protected (whose password the
        // portal must never reuse).
        up = suspend() && bringUp(portal, effectiveChannel);
        g_apUp = up;
    }
    if (!up) {
        endPortal();
        Serial.println("wifiAp: portal AP failed — rollback attempted");
        return false;
    }
    Serial.printf("wifiAp: portal AP \"%s\" on channel %u (%s, open)\n", portal.ssid.c_str(),
                  static_cast<unsigned>(effectiveChannel), channel == 0 ? "kept" : "operator");
    return true;
}

void endPortal() {
    if (!g_portalActive) return;
    g_portalActive = false;
    bool restored = false;
    if (!g_portalRestarted) {
        restored = esp_wifi_set_channel(g_portalPreviousChannel, WIFI_SECOND_CHAN_NONE) == ESP_OK;
    } else if (suspend()) {
        const ApCredentials *cached = g_cache.get();
        const bool hadAp = (g_portalPreviousMode & WIFI_AP) != 0;
        restored = WiFi.mode(g_portalPreviousMode);
        if (restored && hadAp) restored = cached && bringUp(*cached, g_portalPreviousChannel);
        if (restored && !hadAp && g_portalPreviousMode != WIFI_OFF)
            restored = esp_wifi_set_channel(g_portalPreviousChannel, WIFI_SECOND_CHAN_NONE) == ESP_OK;
        g_apUp = hadAp && restored;
    }
    Serial.println(restored ? "wifiAp: radio restored after portal" : "wifiAp: radio restore after portal failed");
}

namespace {
bool g_twinActive = false; // evil-twin scenario owns the portal AP
}

bool startTwin(const std::string &ssid, uint8_t channel) {
    // Reuse the proven portal AP lifecycle (open clone + admin snapshot/restore).
    // The orchestrator validates ssid (1..32) and channel (1..13) via the Plan
    // before calling, so a non-clone (empty ssid) never reaches here.
    const bool up = beginPortal(ssid, channel);
    g_twinActive = up;
    return up;
}

bool stopTwin() {
    endPortal(); // restores the admin AP / prior radio; idempotent
    g_twinActive = false;
    return true;
}

bool twinActive() { return g_twinActive; }
#endif

} // namespace wifiAp
