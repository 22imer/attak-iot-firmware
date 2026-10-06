#pragma once

#include <string>

#include "storage.h"

// Owns the device's own WiFi AP. This namespace is the ONLY place that starts
// or stops the AP: modules never call softAP/teardown, they only run their
// actions under a RadioArbiter WifiExclusive lease and the loop drives
// suspend()/restore() around it.
//
// The credential policy and the effective-config cache below are portable (no
// Arduino/WiFi includes) so they are covered by the native tests; the ESP32
// backend in wifi_ap.cpp performs the actual softAP calls.

namespace wifiAp {

// SSID/password actually used by the backend. An empty password means an open
// AP (matching WiFi.softAP()'s nullptr passphrase semantics).
struct ApCredentials {
    std::string ssid;
    std::string password;

    bool open() const { return password.empty(); }
};

// WPA2 needs an 8..63 char key; empty means open. Anything else (e.g. a
// hand-edited /config.json) would make softAP fail, so callers fall back
// instead of bringing up no AP at all.
inline bool passwordUsable(const std::string &password) {
    return password.empty() || (password.size() >= 8 && password.size() <= 63);
}

// Effective credentials for the configured AP, preserving the existing
// fallback semantics: configured SSID when non-empty else the compiled-in
// default; usable configured password else the default; unusable default =>
// open AP. `fallback` is injectable so the last-resort branch is testable.
// Never logs or returns anything beyond the values that bring up the AP.
inline ApCredentials resolveCredentials(const DeviceConfig &config, const DeviceConfig &fallback = DeviceConfig{}) {
    ApCredentials out;
    out.ssid = config.apSsid.empty() ? fallback.apSsid : config.apSsid;

    out.password = config.apPassword;
    if (!passwordUsable(out.password)) out.password = fallback.apPassword;
    if (!passwordUsable(out.password)) out.password.clear(); // last resort: open AP
    return out;
}

// Remembers the effective credentials once at begin() so restore() brings the
// AP back with exactly the settings that were live before suspension
// (including the open-AP fallback), and tracks whether the operator has
// explicitly requested the AP. Portable state with no hardware includes.
//
// `requested` is boot-default false and is only changed by an explicit
// `ap on` / `ap off` request; suspend()/restore() never touch it, so a
// radio-exclusive window cannot silently turn the AP back on.
class ConfigCache {
public:
    void remember(const ApCredentials &credentials) {
        credentials_ = credentials;
        valid_ = true;
    }

    // Borrowed view of the remembered credentials (no copy/allocation);
    // nullptr before any remember().
    const ApCredentials *get() const { return valid_ ? &credentials_ : nullptr; }

    void setRequested(bool requested) { requested_ = requested; }
    bool requested() const { return requested_; }

private:
    ApCredentials credentials_;
    bool requested_ = false;
    bool valid_ = false;
};

// Caches the effective config only. Never starts the AP: the AP stays down
// until an explicit `ap on` request, so boot needs no WiFi radio (USB dashboard
// only). requested() is false after begin().
void begin(const DeviceConfig &config);

// Explicit operator request (`ap on` over the Serial console): marks the AP
// requested and brings it up from the cached effective config, including the
// open-AP fallback. Idempotent while already running. Returns true when the AP
// is running.
bool requestOn();

// Explicit operator request (`ap off` over the Serial console): marks the AP
// not requested and tears the radio down. Returns true when the radio is off.
bool requestOff();

// True when the operator has explicitly requested the AP (boot default false).
bool requested();

// Actual AP running state (not "the radio is on"): true only when a bring-up
// succeeded and the radio is still in AP mode. A failed bring-up reports false
// even if the backend left the radio powered.
bool running();

// Bounded single-attempt AP teardown: returns true when the AP/radio is off.
// Inspects the live WiFi mode first, so a failed bring-up that left the radio
// on is still torn down. No internal retry loop; the loop owns any cadence.
bool suspend();

// Bounded single-attempt AP bring-up from the cached effective config, but
// only when the AP is explicitly requested: an unrequested AP is left off and
// this still returns true so the arbiter can return to Idle. No internal retry
// loop.
bool restore();

} // namespace wifiAp
