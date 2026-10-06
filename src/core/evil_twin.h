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

} // namespace evilTwin
